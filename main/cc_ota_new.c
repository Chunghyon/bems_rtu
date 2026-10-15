#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "esp_err.h"
#include "esp_log.h"

#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "esp_http_server.h"
#include "esp_netif.h"

#include "esp_ota_ops.h"
#include "esp_image_format.h"

#include "hh_Parameters.h"

static const char *TAG = "ota";

#ifdef OTA_USE_HTTPD

#define URI_UPLOAD_DIR  "/upload"

#define URI_UPDATE_ETH  "/eth/update"
#define URI_UPDATE_WIF  "/wifi/update"
#define URI_UPDATE_CAL  "/caller/update"

#define URI_REBOOT_ETH  "/eth/reboot"
#define URI_REBOOT_WIF  "/wifi/reboot"

#define URI_GETVER_ETH  "/eth/getver"
#define URI_GETVER_WIF  "/wifi/getver"
#define URI_GETVER_CAL  "/caller/getver"
#define URI_SETVER_CAL  "/caller/setver/"

#define URI_AUP_ETH		"/autoup/eth"
#define URI_AUP_WIF		"/autoup/wifi"
#define URI_AUP_CAL		"/autoup/caller"
#define URI_AUP_ALL		"/autoup/all"

#define URI_AUP_RESULT	"/autoup/result"
#define URI_AUP_HALT	"/autoup/halt"

extern uint32_t PP_CURRENT_VERSION;

extern bool last_bin_transferred;
extern uint32_t global_eth_timestamp;

uint32_t gg_wifi_version;
uint32_t gg_caller_version;

cc_ota_target_t cc_ota_target = OTA_TARGET_NONE;

bool cc_ota_under_process;
bool autoup_under_process;

static ota_data_t ota_spi_tx;
static ota_data_t ota_spi_rx;
static int send_seq_to_spi;
static TimerHandle_t handle_RebootTimer;

autoup_result_t autoup_result[3][100];

extern uint32_t ff_running_image_size(const esp_partition_t *running);

const esp_partition_t *cc_running_partition;
const esp_partition_t *cc_update_partition;
const esp_partition_t *cc_storage_partition;
esp_ota_handle_t cc_ota_handle;
bool esp_restart_requested;
bool transfer_completed;

/* Max length a file path can have on storage */
#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + CONFIG_SPIFFS_OBJ_NAME_LEN)

/* Max size of an individual file. Make sure this
 * value is same as that set in upload_script.html */
#define MAX_FILE_SIZE   (0x180000) // 1.5 MB = 1572864 B
#define MAX_FILE_SIZE_STR "1536KB"

/* Scratch buffer size */
#define SCRATCH_BUFSIZE  CONFIG_TCP_MSS

struct file_server_data {
	/* Base path of file storage */
	char base_path[ESP_VFS_PATH_MAX + 1];

	/* Scratch buffer for temporary storage during file transfer */
	char scratch[SCRATCH_BUFSIZE];
};

static void cc_autoup_start(httpd_req_t *req, cc_ota_target_t target);
static void cc_autoup_halt(httpd_req_t *req);
static void cc_autoup_result(httpd_req_t *req);

static void cb_reboot( TimerHandle_t xTimer )
{
	CC_LOGNI("\n");
	CC_LOGNI("!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
	CC_LOGNI("!!! OTA Update Corrupted !!!\n");
	CC_LOGNI("!!!!  System Rebooting  !!!!\n");
	CC_LOGNI("!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
	vTaskDelay(pdMS_TO_TICKS(1000));
	esp_restart();
}

static void start_reboot_timer(void) // reboot if fw is not updated within 30 secs
{
	if(handle_RebootTimer) xTimerDelete(handle_RebootTimer, 0);
	handle_RebootTimer = xTimerCreate("RebootTimer", pdMS_TO_TICKS(OTA_REBOOT_TIMER_MS_START), pdFALSE, 0, cb_reboot);
	xTimerStart(handle_RebootTimer, 0);
}

static void restart_reboot_timer(void) // reboot if fw is not updated within specific secs
{
	xTimerStop(handle_RebootTimer, 0);
	if(xTimerGetPeriod(handle_RebootTimer) != pdMS_TO_TICKS(OTA_REBOOT_TIMER_MS_RESTART))
		xTimerChangePeriod(handle_RebootTimer, pdMS_TO_TICKS(OTA_REBOOT_TIMER_MS_RESTART), pdMS_TO_TICKS(100));
	xTimerStart(handle_RebootTimer, 0);
}


/* Handler to redirect incoming GET request for /index.html to /
 * This can be overridden by uploading file with same name */
esp_err_t index_html_get_handler(httpd_req_t *req)
{
	CC_LOGN("[%s]\n", __func__);
	httpd_resp_set_status(req, "307 Temporary Redirect");
	httpd_resp_set_hdr(req, "Location", "/");
	httpd_resp_send(req, NULL, 0);  // Response body can be empty
	return ESP_OK;
}

/* Handler to respond with an icon file embedded in flash.
 * Browsers expect to GET website icon at URI /favicon.ico.
 * This can be overridden by uploading file with same name */
esp_err_t favicon_get_handler(httpd_req_t *req)
{
	CC_LOGN("[%s]\n", __func__);
	extern const unsigned char favicon_ico_start[] asm("_binary_favicon_ico_start");
	extern const unsigned char favicon_ico_end[]   asm("_binary_favicon_ico_end");
	const size_t favicon_ico_size = (favicon_ico_end - favicon_ico_start);
	httpd_resp_set_type(req, "image/x-icon");
	httpd_resp_send(req, (const char *)favicon_ico_start, favicon_ico_size);
	return ESP_OK;
}

static esp_err_t http_resp_dir_html(httpd_req_t *req, const char *dirpath)
{
	CC_LOGN("[%s]\n", __func__);
//	char entrypath[FILE_PATH_MAX];
//	char entrysize[16];
//	const char *entrytype;
//	struct dirent *entry;
//	struct stat entry_stat;

	/* Send HTML file header */
	httpd_resp_sendstr_chunk(req, "<!DOCTYPE html><html><body>");

	/* Get handle to embedded file upload script */
	extern const unsigned char upload_script_start[] asm("_binary_upload_script_html_start");
	extern const unsigned char upload_script_end[]   asm("_binary_upload_script_html_end");
	const size_t upload_script_size = (upload_script_end - upload_script_start);

	/* Add file upload form and script which on execution sends a POST request to URI_UPLOAD_DIR */
	httpd_resp_send_chunk(req, (const char *)upload_script_start, upload_script_size);

	/* Send remaining chunk of HTML file to complete it */
	httpd_resp_sendstr_chunk(req, "</body></html>");

	/* Send empty chunk to signal HTTP response completion */
	httpd_resp_sendstr_chunk(req, NULL);
	
	return ESP_OK;
}

#define IS_FILE_EXT(filename, ext) \
	(strcasecmp(&filename[strlen(filename) - sizeof(ext) + 1], ext) == 0)

/* Set HTTP response content type according to file extension */
esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filename)
{
	CC_LOGN("[%s]\n", __func__);
	if (IS_FILE_EXT(filename, ".pdf")) {
		return httpd_resp_set_type(req, "application/pdf");
	} else if (IS_FILE_EXT(filename, ".html")) {
		return httpd_resp_set_type(req, "text/html");
	} else if (IS_FILE_EXT(filename, ".jpeg")) {
		return httpd_resp_set_type(req, "image/jpeg");
	} else if (IS_FILE_EXT(filename, ".ico")) {
		return httpd_resp_set_type(req, "image/x-icon");
	}
	/* This is a limited set only */
	/* For any other type always set as plain text */
	return httpd_resp_set_type(req, "text/plain");
}

/* Copies the full path into destination buffer and returns
 * pointer to path (skipping the preceding base path) */
static const char* get_path_from_uri(char *dest, const char *base_path, const char *uri, size_t destsize)
{
	const size_t base_pathlen = strlen(base_path);
	size_t pathlen = strlen(uri);

	const char *quest = strchr(uri, '?');
	if (quest) {
		pathlen = MIN(pathlen, quest - uri);
	}
	const char *hash = strchr(uri, '#');
	if (hash) {
		pathlen = MIN(pathlen, hash - uri);
	}

	if (base_pathlen + pathlen + 1 > destsize) {
		/* Full path string won't fit into destination buffer */
		return NULL;
	}

	/* Construct full path (base + path) */
	strcpy(dest, base_path);
	strlcpy(dest + base_pathlen, uri, pathlen + 1);

	/* Return pointer to path, skipping the base */
	return dest + base_pathlen;
}

static bool ota_spi_send_and_wait(void)
{

	return true;
}

/* Handler to parse uri */
static esp_err_t uri_parser_handler(httpd_req_t *req)
{
	CC_LOGN("[%s]\n", __func__);

	char str[48];
	const char * uri = req->uri;

/*
	char filepath[FILE_PATH_MAX];
	FILE *fd = NULL;
	struct stat file_stat;
	const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
											 req->uri, sizeof(filepath));

	if (filename) {
		ESP_LOGE(TAG, "Filename is too long");
		// Respond with 500 Internal Server Error
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
		return ESP_FAIL;
	}
*/
	CC_LOGN("uri \"%s\", len %u\n", uri, strlen(uri));

	/* If name has trailing '/', respond with directory contents */
	if (strlen(uri) == 1 && uri[0] == '/') {

		if(transfer_completed) {
			httpd_resp_sendstr(req, "Upload Success!!\r\n");
			httpd_resp_send_chunk(req, NULL, 0);
			return ESP_OK;
		}
	}

	memset(&ota_spi_tx, 0, sizeof(ota_data_t));

	if(!memcmp(URI_UPDATE_ETH, uri, strlen(URI_UPDATE_ETH))) {
		cc_ota_target = OTA_TARGET_ETH;
		start_reboot_timer();
		cc_ota_under_process = 1;
		http_resp_dir_html(req, uri);
	}
	else if(!memcmp(URI_REBOOT_ETH, uri, strlen(URI_REBOOT_ETH))) {
		sprintf(str, "SmartLM ETH Reboot OK.\r\n");
		httpd_resp_sendstr(req, str);
		CC_LOGNI("!!!!!!! ETH Reboot request by OTA\n");
		vTaskDelay(pdMS_TO_TICKS(1000));
		esp_restart();
	}
	else if(!memcmp(URI_GETVER_ETH, uri, strlen(URI_GETVER_ETH))) {
		sprintf(str, "SmartLM ETH Version : v%lu\r\n", PP_CURRENT_VERSION);
		httpd_resp_sendstr(req, str);
	}
	else if(!memcmp(URI_AUP_ETH, uri, strlen(URI_AUP_ETH))) {
		cc_autoup_start(req, OTA_TARGET_ETH);
	}
	else if(!memcmp(URI_AUP_RESULT, uri, strlen(URI_AUP_RESULT))) {
		cc_autoup_result(req);
	}
	else if(!memcmp(URI_AUP_HALT, uri, strlen(URI_AUP_HALT))) {
		cc_autoup_halt(req);
	}
	else {
		httpd_resp_sendstr(req, "Wrong uri...\r\n");
	}

	/* Respond with an empty chunk to signal HTTP response completion */
#ifdef CONFIG_EXAMPLE_HTTPD_CONN_CLOSE_HEADER
	httpd_resp_set_hdr(req, "Connection", "close");
#endif
	httpd_resp_send_chunk(req, NULL, 0);
	return ESP_OK;
}

static esp_err_t ota_write(cc_ota_target_t ota_target, uint8_t * p_data, int length)
{
	esp_err_t err;

	switch(ota_target)
	{
		case OTA_TARGET_ETH :
			err = esp_ota_write( cc_ota_handle, (const void *)p_data, length);
			if (err != ESP_OK) {
				CC_LOGE("Error: esp ota write failed (%s)!\n", esp_err_to_name(err));
			}
			return err;

		case OTA_TARGET_WIFI :
		case OTA_TARGET_CALLER :
			while(1) {
				memset(&ota_spi_tx, 0, sizeof(ota_data_t));
				ota_spi_tx.header.opcode	= OTA_OPCODE_DATA_SEND;
				ota_spi_tx.seq				= send_seq_to_spi;
				ota_spi_tx.length			= length;
				memcpy(ota_spi_tx.data, p_data, length);
				if( ota_spi_send_and_wait() ) {
					if(ota_spi_rx.seq == send_seq_to_spi) {
						return ESP_OK;
					}
					else continue;
				}
				else return ESP_FAIL;
			}
			break;

		default :
			break;
	}
	return ESP_FAIL;
}

/* Handler to upload a file onto the server */
static esp_err_t upload_post_handler(httpd_req_t *req)
{
	CC_LOGN("[%s]\n", __func__);
	char filepath[FILE_PATH_MAX];
//	struct stat file_stat;

	esp_err_t err;

	/* Skip leading URI_UPLOAD_DIR from URI to get filename */
	/* Note sizeof() counts NULL termination hence the -1 */
	const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
											 req->uri + sizeof(URI_UPLOAD_DIR) - 1, sizeof(filepath));
	if (!filename) {
		/* Respond with 500 Internal Server Error */
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
		return ESP_FAIL;
	}

	CC_LOGN("filename \"%s\", len %u\n", filename, strlen(filename));

	/* Filename cannot have a trailing '/' */
	if (filename[strlen(filename) - 1] == '/') {
		CC_LOGN("Invalid filename : %s\n", filename);
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid filename");
		return ESP_FAIL;
	}

	/* File cannot be larger than a limit */
	if (req->content_len > MAX_FILE_SIZE) {
		CC_LOGN("File too large : %d bytes\n", req->content_len);
		/* Respond with 400 Bad Request */
		httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
							"File size must be less than "
							MAX_FILE_SIZE_STR "!");
		/* Return failure to close underlying connection else the
		 * incoming file content will keep the socket busy */
		return ESP_FAIL;
	}

/////////////////////////////////////////////////////////////////////////////////////////////
	if(cc_ota_target == OTA_TARGET_ETH) {
		err = esp_ota_begin(cc_update_partition, OTA_SIZE_UNKNOWN, &cc_ota_handle);
		if (err != ESP_OK) {
			CC_LOGW("eth esp_ota_begin failed (%s)\n", esp_err_to_name(err));
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to begin OTA");
			return ESP_FAIL;
		}
		else {
			CC_LOGNI("!!!!!! eth OTA BEGIN OK\n");
		}
	}
	else if(cc_ota_target == OTA_TARGET_WIFI || cc_ota_target== OTA_TARGET_CALLER) {
		char str_target[8] = {0, };
		memset(&ota_spi_tx, 0, sizeof(ota_data_t));
		ota_spi_tx.header.opcode = cc_ota_target == OTA_TARGET_WIFI ? OTA_OPCODE_WIFI_START_REQ : OTA_OPCODE_CALLER_START_REQ;
		
		strcpy(str_target, cc_ota_target == OTA_TARGET_WIFI ? "wifi" : "caller");
		if(ota_spi_send_and_wait()) {
			CC_LOGNI("!!!!!! %s OTA BEGIN OK\n", str_target);
		}
		else {
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to begin OTA");
			return ESP_FAIL;
		}
	}
	else {
		CC_LOGN("Wrong target %u\n", cc_ota_target);
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to begin OTA");
		return ESP_FAIL;
	}

/////////////////////////////////////////////////////////////////////////////////////////////
	CC_LOGN("Receiving file : %s...\n", filename);

	/* Retrieve the pointer to scratch buffer for temporary storage */
	char *buf = ((struct file_server_data *)req->user_ctx)->scratch;
	int received;

	/* Content length of the request gives
	 * the size of the file being uploaded */
	int remaining = req->content_len;
	int total_len = remaining;
	int cnt = 0;
	int transfered = 0;

	send_seq_to_spi = 0;

	CC_LOGN("!! Received Packet\n");
	while (remaining > 0) {
//		CC_LOGN("Remaining size : %d/%d\n", remaining, total_len);
		/* Receive the file part by part into a buffer */
		if ((received = httpd_req_recv(req, buf, MIN(remaining, SCRATCH_BUFSIZE))) <= 0) {
			if (received == HTTPD_SOCK_ERR_TIMEOUT) {
				/* Retry if timeout occurred */
				continue;
			}

			CC_LOGN("File reception failed!\n");
			/* Respond with 500 Internal Server Error */
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive file");
			return ESP_FAIL;
		}
		/////////////////////////////////////////////////////////////////////////////////////////////
		if(received) {
			//err = esp_ota_write( cc_ota_handle, (const void *)buf, received);
			err = ota_write(cc_ota_target, (uint8_t *)buf, received);
			if (err != ESP_OK) {
				CC_LOGE("Error: esp ota write failed (%s)!\n", esp_err_to_name(err));
				httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write OTA");
				return ESP_FAIL;
			}
			restart_reboot_timer();
			send_seq_to_spi++;
		}
/////////////////////////////////////////////////////////////////////////////////////////////
		/* Keep track of remaining size of
		 * the file left to be uploaded */
		remaining -= received;

		cnt++;
		transfered = total_len - remaining;
		CC_LOGN("%6u/%6u", transfered, total_len);
		CC_LOGN("%s", remaining==0 ? "\n" : cnt%5==0 ? ",\n" : ", ");
		if(!debug_log_on && cnt%10==9) {
			CC_LOGNI(".");
		}
}
	CC_LOGN("------------------------\n");
	CC_LOGN("File reception complete.\n");
	transfer_completed = 1;

	/* Redirect onto root to see the updated file list */
	httpd_resp_set_status(req, "303 See Other");
	httpd_resp_set_hdr(req, "Location", "/");
#ifdef CONFIG_EXAMPLE_HTTPD_CONN_CLOSE_HEADER
	httpd_resp_set_hdr(req, "Connection", "close");
#endif
	httpd_resp_sendstr(req, "File uploaded successfully\r\n");

/////////////////////////////////////////////////////////////////////////////////////////////
	if(cc_ota_target == OTA_TARGET_ETH) {
		if (esp_ota_end(cc_ota_handle) != ESP_OK) {
			CC_LOGN("\nesp_ota_end failed!");
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to end OTA");
			return ESP_FAIL;
		}
		else {
			err = esp_ota_set_boot_partition(cc_update_partition);
			if (err != ESP_OK) {
				CC_LOGN("\nesp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
				httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set boot partition");
				return ESP_FAIL;
				}
			else
			{
				xTimerStop(handle_RebootTimer, 0);
				CC_LOGNI("\n");
				CC_LOGNI("!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
				CC_LOGNI("!!!! OTA Update Success !!!!\n");
				CC_LOGNI("!!!!  System Rebooting  !!!!\n");
				CC_LOGNI("!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
				esp_restart_requested = 1;
			}
		}
	}
	else if(cc_ota_target == OTA_TARGET_WIFI || cc_ota_target== OTA_TARGET_CALLER) {
		memset(&ota_spi_tx, 0, sizeof(ota_data_t));
		ota_spi_tx.header.opcode = (cc_ota_target == OTA_TARGET_WIFI) ? OTA_OPCODE_WIFI_END_REQ : OTA_OPCODE_CALLER_END_REQ;
		vTaskDelay(pdMS_TO_TICKS(500));
		if( ota_spi_send_and_wait() ) {
			xTimerStop(handle_RebootTimer, 0);
			CC_LOGNI("\n");
			CC_LOGNI("!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
			CC_LOGNI("!!!!    %s    !!!!\n", cc_ota_target==OTA_TARGET_WIFI ? "WiFi Gateway":" For Caller ");
			CC_LOGNI("!!!! OTA Update Success !!!!\n");
			CC_LOGNI("!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
		}
	}
/////////////////////////////////////////////////////////////////////////////////////////////

	return ESP_OK;
}

void ff_ota_partition_init(void)
{
	cc_running_partition = esp_ota_get_running_partition();
	assert(cc_running_partition != NULL);

	cc_update_partition = esp_ota_get_next_update_partition(NULL);
	assert(cc_update_partition != NULL);

	CC_LOGN("\n");

	const esp_partition_t *configured = esp_ota_get_boot_partition();
	if (configured != cc_running_partition) {
		CC_LOGN("!! First Run after erase_flash. Set configured partion as running partition.\n");
		(void) esp_ota_set_boot_partition(cc_running_partition);
	}

	CC_LOGNI("** \033[1;33mRunning\033[1;37m partition : \033[1;33m0x%02X\033[1;37m (offset \033[1;33m0x%08lx\033[1;37m)\n",
			(uint8_t)cc_running_partition->subtype, cc_running_partition->address);

	CC_LOGNI("** \033[1;32mStandby\033[1;37m partition : \033[1;32m0x%02X\033[1;37m (offset \033[1;32m0x%08lx\033[1;37m)\n",
			(uint8_t)cc_update_partition->subtype, cc_update_partition->address);

	CC_LOGN("!! Running_image_size %lu\n", ff_running_image_size(cc_running_partition));
}

/* Function to start the file server */
esp_err_t start_ota_server(void)
{
	ESP_LOGI(TAG, "[%s]", __func__);

	static struct file_server_data *server_data = NULL;
//	const char *base_path = "/data";
	const char *base_path = "";

	if (server_data) {
		ESP_LOGW(TAG, "File server already started");
		return ESP_ERR_INVALID_STATE;
	}

	/* Allocate memory for server data */
	server_data = calloc(1, sizeof(struct file_server_data));
	if (!server_data) {
		ESP_LOGE(TAG, "Failed to allocate memory for server data");
		return ESP_ERR_NO_MEM;
	}

	strlcpy(server_data->base_path, base_path, sizeof(server_data->base_path));

	httpd_handle_t handle = NULL;
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.server_port = ETH_OTA_TCP_SERVER_PORT;

	/* Use the URI wildcard matching function in order to
	 * allow the same handler to respond to multiple different
	 * target URIs which match the wildcard scheme */
	config.uri_match_fn = httpd_uri_match_wildcard;

	ESP_LOGI(TAG, "Starting HTTP Server on port %u", config.server_port);
	esp_err_t ret = httpd_start(&handle, &config);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to start file server!, %s", esp_err_to_name(ret));
		free(server_data);
		return ESP_FAIL;
	}

	/* URI handler for getting uploaded files */
	httpd_uri_t uri_parser = {
		.uri       = "/*",  // Match all URIs of type /path/to/file
		.method    = HTTP_GET,
		.handler   = uri_parser_handler,
		.user_ctx  = server_data    // Pass server data as context
	};
	httpd_register_uri_handler(handle, &uri_parser);

	/* URI handler for uploading files to server */
	httpd_uri_t file_upload = {
		.uri       = URI_UPLOAD_DIR"/*",   // Match all URIs of type URI_UPLOAD_DIR/path/to/file
		.method    = HTTP_POST,
		.handler   = upload_post_handler,
		.user_ctx  = server_data    // Pass server data as context
	};
	httpd_register_uri_handler(handle, &file_upload);

	return ESP_OK;
}

#include "lwip/sockets.h"
#include "hh_ping.h"
#include "hh_nvs.h"

#define COPILOT_RECOMMENDx
#define GW_NUM_MIN	0
#define GW_NUM_MAX	100
autoup_result_t autoup_result[3][100];	//[0] Eth, [1] Wifi, [3] Caller

struct netconn *p_connection_conn_autoup;
//static int current_autoup_gw = 255;
static bool escape_from_autoup_loop;

static uint32_t total_bin_size_eth;
static uint32_t total_bin_size_wifi;
static uint32_t total_bin_size_caller;

static char http_header_string[] =
"POST /upload/autoupdate HTTP/1.1\r\n"
"Connection: keep-alive\r\n"
"Content-Length: %lu\r\n"
"Content-Type: multipart/form-data; boundary=----WebKitFormBoundaryA7dYiuGvPSBaUeql\r\n"
"Accept: */*\r\n"
"X-Requested-With: XMLHttpRequest\r\n\r\n";


static void cc_reboot_gw_wifi(void)
{
	memset(&ota_spi_tx, 0, sizeof(ota_data_t));
	ota_spi_tx.header.opcode = OTA_OPCODE_WIFI_REBOOT_REQ;
	if( ota_spi_send_and_wait() ) {
		CC_LOGN("GW WIFI is rebooting...\n");
	}
}

void get_wifi_caller_version(void)
{
	memset(&ota_spi_tx, 0, sizeof(ota_data_t));

	ota_spi_tx.header.opcode = OTA_OPCODE_WIFI_VER_GET;
	if( ota_spi_send_and_wait() ) {
		gg_wifi_version = ota_spi_rx.version;
	}
	else CC_LOGE("SPI Timeout...\n");

	ota_spi_tx.header.opcode = OTA_OPCODE_CALLER_VER_GET;
	if( ota_spi_send_and_wait() ) {
		gg_caller_version = ota_spi_rx.version;
	}
	else CC_LOGE("SPI Timeout...\n");
}

static void get_total_bin_size(cc_ota_target_t sel)
{
	switch(sel)
	{
	case OTA_TARGET_ALL :
		total_bin_size_eth = ff_running_image_size(cc_running_partition);
		ota_spi_tx.header.opcode = OTA_OPCODE_WIFI_SIZE_REQ;
		if( ota_spi_send_and_wait() ) {
			total_bin_size_wifi = ota_spi_rx.size;
		}
		ota_spi_tx.header.opcode = OTA_OPCODE_CALLER_SIZE_REQ;
		if( ota_spi_send_and_wait() ) {
			total_bin_size_caller = ota_spi_rx.size;
		}
		break;

	case OTA_TARGET_ETH :
		total_bin_size_eth = ff_running_image_size(cc_running_partition);
		break;

	case OTA_TARGET_WIFI :
		ota_spi_tx.header.opcode = OTA_OPCODE_WIFI_SIZE_REQ;
		if( ota_spi_send_and_wait() ) {
			total_bin_size_wifi = ota_spi_rx.size;
		}
		else {
			CC_LOGN("SPI Comm. Error.\n");
		}
		break;

	case OTA_TARGET_CALLER :
		ota_spi_tx.header.opcode = OTA_OPCODE_CALLER_SIZE_REQ;
		if( ota_spi_send_and_wait() ) {
			total_bin_size_caller = ota_spi_rx.size;
		}
		else {
			CC_LOGN("SPI Comm. Error.\n");
		}
		break;

	default :
		break;
	}
}

static bool ff_chk_header_ok(char * str)
{
	char * p_ptr = strstr(str, "OK");
	if(p_ptr) return true;
	else return false;
}

uint32_t ff_extract_version(char * str)
{
	uint32_t version = 0;

	char * p_ptr = strstr(str, ": v");
	if(p_ptr) {
		version = atoi(&p_ptr[3]);
	}

	CC_LOGN("peer_version %lu\n", version);

	return version;
}

autoup_result_t autoup_client_loop(cc_ota_target_t sel, uint32_t ip, uint32_t total_bin_size)
{
	autoup_result_t result = AUTOUP_RESULT_COMM_ERROR;

	if(total_bin_size == 0) return result;

	struct sockaddr_in targetAddr = { 0, };
	targetAddr.sin_family = AF_INET;
	targetAddr.sin_addr.s_addr = ip;
	targetAddr.sin_port = htons(AUTO_UPDATE_SERVER_PORT);

	autoup_status_t autoupStatus = AUTOUP_STATUS_VERSION_HEADER;

	uint32_t bin_offset = 0;

	const uint32_t my_version = (sel==OTA_TARGET_ETH) ? PP_CURRENT_VERSION :
			(sel==OTA_TARGET_WIFI) ? gg_wifi_version : gg_caller_version;

	int sock = -1;

	CC_LOGN("total_bin_size %lu\n", total_bin_size);

	int tmp_cnt = 0;

	while ( !escape_from_autoup_loop )
	{
		sock = -1;
		sock =  socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
		if (sock < 0) {
			CC_LOGE("AutoUp Client : Unable to create socket: errno %d.\n", errno);
			break;
		}

		int err = connect(sock, (struct sockaddr *)&targetAddr, sizeof(targetAddr));
		if (err != 0) {
			CC_LOGE("Socket unable to connect: errno %d\n", errno);
			break;
		}

		char str[INET_ADDRSTRLEN] = {0, };
		inet_ntop(AF_INET, &targetAddr.sin_addr.s_addr, str, INET_ADDRSTRLEN);
		CC_LOGN("\033[1;33m!! TCP Connected to AutoUp Server (%s:%d)\n\033[1;37m",
				str, AUTO_UPDATE_SERVER_PORT);

		bool reconn_needed = false;

		while( !escape_from_autoup_loop && !reconn_needed)
		{
			char str_tx[AUTOUP_PKT_SIZE_MAX] = {0, };
			uint16_t len_tx = 0;
			uint16_t len_bin = 0;

			switch(autoupStatus)
			{
			case AUTOUP_STATUS_SET_CALLER_VERSION :
				CC_LOGN("AUTOUP_STATUS_SET_CALLER_VERSION\n");
				sprintf(str_tx,
					"GET /caller/setver/%lu HTTP/1.1\r\n"
					"Connection: keep-alive\r\n"
					"\r\n", my_version);
				len_tx = strlen(str_tx);
				break;

			case AUTOUP_STATUS_TERMINATE :
				CC_LOGN("AUTOUP_STATUS_TERMINATE\n");
				result = AUTOUP_RESULT_OK;
				escape_from_autoup_loop = true;
				break;

			case AUTOUP_STATUS_VERSION_HEADER :
				CC_LOGN("AUTOUP_STATUS_VERSION_HEADER\n");
				sprintf(str_tx,
					"GET /%s/getver HTTP/1.1\r\n"
					"Connection: keep-alive\r\n"
					"\r\n",
					(sel==OTA_TARGET_ETH) ? "eth" :
					(sel==OTA_TARGET_WIFI) ? "wifi" : "caller");

				len_tx = strlen(str_tx);
				break;

			case AUTOUP_STATUS_BEGIN_HEADER :
				CC_LOGN("AUTOUP_STATUS_BEGIN_HEADER\n");
				sprintf(str_tx,
					"GET /%s/update HTTP/1.1\r\n"
					"Connection: keep-alive\r\n"
					"\r\n",

					(sel==OTA_TARGET_ETH) ? "eth" :
					(sel==OTA_TARGET_WIFI) ? "wifi" : "caller");

				len_tx = strlen(str_tx);
				break;

			case AUTOUP_STATUS_DATA_HEADER :
				CC_LOGN("AUTOUP_STATUS_DATA_HEADER\n");
				sprintf(str_tx, http_header_string, total_bin_size);
				CC_LOGN("str_tx %s\n", str_tx);
				len_tx = strlen(str_tx);
				break;

			case AUTOUP_STATUS_DATA_FIRST :
				CC_LOGN("AUTOUP_STATUS_DATA_FIRST\n");
				len_bin = AUTOUP_PKT_SIZE_MAX;
				len_tx = AUTOUP_PKT_SIZE_MAX;

				if(sel==OTA_TARGET_ETH) {
					if(ESP_OK != esp_partition_read(cc_running_partition, bin_offset, &str_tx[0], len_bin) ) {
						result = AUTOUP_RESULT_COMM_ERROR;
						escape_from_autoup_loop = true;
						break;
					}
				}
				else {
					ota_spi_tx.header.opcode = OTA_OPCODE_TRANSFER_START_REQ;
					if( !ota_spi_send_and_wait() ) {
						result = AUTOUP_RESULT_COMM_ERROR;
						escape_from_autoup_loop = true;
						break;
					}

					ota_spi_tx.header.opcode = (sel==OTA_TARGET_WIFI) ? OTA_OPCODE_WIFI_DATA_REQ : OTA_OPCODE_CALLER_DATA_REQ;
					ota_spi_tx.offset = bin_offset;
					ota_spi_tx.length = len_bin;
					if( ota_spi_send_and_wait() ) {
						memcpy(str_tx, ota_spi_rx.data, ota_spi_tx.length);
					}
					else {
						result = AUTOUP_RESULT_COMM_ERROR;
						escape_from_autoup_loop = true;
						break;
					}
				}

				CC_LOGN("%6lu/%6lu", bin_offset+len_bin, total_bin_size);
				CC_LOGN("%s", (tmp_cnt%4==3) ? "\n" : ", " );
				if(!debug_log_on && tmp_cnt%10==9) CC_LOGNI(".");
				tmp_cnt++;
				break;

			case AUTOUP_STATUS_DATA_REST :
				len_bin = len_tx = (bin_offset + AUTOUP_PKT_SIZE_MAX < total_bin_size) ? AUTOUP_PKT_SIZE_MAX : total_bin_size - bin_offset;
				if(sel==OTA_TARGET_ETH) {
					if(ESP_OK != esp_partition_read(cc_running_partition, bin_offset, str_tx, len_tx) ) {
						result = AUTOUP_RESULT_COMM_ERROR;
						escape_from_autoup_loop = true;
						break;
					}
				}
				else {
					ota_spi_tx.header.opcode = (sel==OTA_TARGET_WIFI) ? OTA_OPCODE_WIFI_DATA_REQ : OTA_OPCODE_CALLER_DATA_REQ;
					ota_spi_tx.offset = bin_offset;
					ota_spi_tx.length = len_bin;
					if( ota_spi_send_and_wait() ) {
						memcpy(str_tx, ota_spi_rx.data, ota_spi_tx.length);
					}
					else {
						result = AUTOUP_RESULT_COMM_ERROR;
						escape_from_autoup_loop = true;
						break;
					}
				}

				if(bin_offset + len_tx == total_bin_size) {
					last_bin_transferred = true;
				}

				CC_LOGN("%6lu/%6lu", bin_offset+len_bin, total_bin_size);
				CC_LOGN("%s", (tmp_cnt%4==3) ? "\n" : ", " );
				if(!debug_log_on && tmp_cnt%10==9) CC_LOGNI(".");
				tmp_cnt++;
				break;

			default :
				CC_LOGN("Wrong autoupStatus %d\n", autoupStatus);
				escape_from_autoup_loop = true;
				break;
			}

			if(reconn_needed || escape_from_autoup_loop) continue;

			int err = send(sock, str_tx, len_tx, 0);
			if (err < 0) {
				CC_LOGE("Error occured during sending: errno %d.\n", errno);
				result = AUTOUP_RESULT_COMM_ERROR;
				escape_from_autoup_loop = true;
				break;
			}

			global_eth_timestamp = xTaskGetTickCount();

			if(autoupStatus == AUTOUP_STATUS_DATA_HEADER) {
				autoupStatus = AUTOUP_STATUS_DATA_FIRST;
				continue;
			}
			else if(autoupStatus == AUTOUP_STATUS_DATA_FIRST) {
				autoupStatus = AUTOUP_STATUS_DATA_REST;
				CC_LOGN("AUTOUP_STATUS_DATA_REST\n");
				bin_offset += len_bin;
				continue;
			}
			else if(autoupStatus == AUTOUP_STATUS_DATA_REST) {
				bin_offset += len_bin;
				if( bin_offset >= total_bin_size) {
					if(sel==OTA_TARGET_CALLER) {
						autoupStatus = AUTOUP_STATUS_SET_CALLER_VERSION;
						reconn_needed = true;
					}
					else {
						autoupStatus = AUTOUP_STATUS_TERMINATE;
					}
				}
				vTaskDelay(pdMS_TO_TICKS(50));
				continue;
			}

			bool escape_recv_loop = false;

			while(!escape_from_autoup_loop && !escape_recv_loop) {
				char str_rx[AUTOUP_PKT_SIZE_MAX] = {0, };
				int len_rx = recv(sock, str_rx, sizeof(str_rx), 0);
				while(len_rx == AUTOUP_PKT_SIZE_MAX) {
					CC_LOGN("rcv data is bigger than %d\n", AUTOUP_PKT_SIZE_MAX);
					memset(str_rx, 0, sizeof(str_rx));
					len_rx = recv(sock, str_rx, sizeof(str_rx), 0);
				}
				CC_LOGN("len_rx %d\n", len_rx);
				if ( len_rx > 0 ) {
					str_rx[len_rx] = '\n';
					CC_LOGN("%s", str_rx);

					switch(autoupStatus)
					{
					case AUTOUP_STATUS_SET_CALLER_VERSION :
						CC_LOGN("[AUTOUP_STATUS_SET_CALLER_VERSION]\n");
						if(ff_chk_header_ok(str_rx)) {
							autoupStatus = AUTOUP_STATUS_TERMINATE;
							escape_recv_loop = true;
						}
						else {
							result = AUTOUP_RESULT_COMM_ERROR;
							escape_from_autoup_loop = true;
						}
						break;

					case AUTOUP_STATUS_VERSION_HEADER :
						CC_LOGN("[AUTOUP_STATUS_VERSION_HEADER]\n");
						if(ff_chk_header_ok(str_rx)) {
							autoupStatus = AUTOUP_STATUS_VERSION_VERIFY;
							continue;
						}
						else {
							result = AUTOUP_RESULT_COMM_ERROR;
							escape_from_autoup_loop = true;
						}
						break;

					case AUTOUP_STATUS_VERSION_VERIFY :
						CC_LOGN("[AUTOUP_STATUS_VERSION_VERIFY]\n");
						CC_LOGN("my_version %lu\n", my_version);
#if !TEST
						autoupStatus = AUTOUP_STATUS_BEGIN_HEADER;
						escape_recv_loop = true;
						reconn_needed = true;
#else
						if(my_version != ff_extract_version(str_rx)) {
							autoupStatus = AUTOUP_STATUS_BEGIN_HEADER;
							escape_recv_loop = true;
							reconn_needed = true;
						}
						else {
							result = AUTOUP_RESULT_SAME_VERSION;
							escape_from_autoup_loop = true;
						}
#endif
						break;

					case AUTOUP_STATUS_BEGIN_HEADER :
						CC_LOGN("[AUTOUP_STATUS_BEGIN_HEADER]\n");
						if(ff_chk_header_ok(str_rx)) {
							autoupStatus = AUTOUP_STATUS_BEGIN_SCRIPT;
							continue;
						}
						else {
							result = AUTOUP_RESULT_COMM_ERROR;
							escape_from_autoup_loop = true;
						}
						break;

					case AUTOUP_STATUS_BEGIN_SCRIPT :
						CC_LOGN("[AUTOUP_STATUS_BEGIN_SCRIPT]\n");
						if(strstr(str_rx, "</script>")) {
							autoupStatus = AUTOUP_STATUS_DATA_HEADER;
							escape_recv_loop = true;
							//reconn_needed = true;
						}
						else {
							result = AUTOUP_RESULT_COMM_ERROR;
							escape_from_autoup_loop = true;
						}
						break;

					case AUTOUP_STATUS_TERMINATE :
						CC_LOGN("[AUTOUP_STATUS_TERMINATE]\n");
						result = AUTOUP_RESULT_OK;
						escape_from_autoup_loop = true;
						break;

					default :
						CC_LOGN("[default]\n");
						result = AUTOUP_RESULT_COMM_ERROR;
						escape_from_autoup_loop = true;
						break;
					}
				}
				else {
					CC_LOGN("receive fail\n");
					result = AUTOUP_RESULT_COMM_ERROR;
					escape_from_autoup_loop = true;
					break;
				}
			}
		}

		if (sock != -1) {
			shutdown(sock, 0);
			close(sock);
			sock = -1;
		}
	}

	if (sock != -1) {
		shutdown(sock, 0);
		close(sock);
		sock = -1;
	}

	escape_from_autoup_loop = false;
	CC_LOGN("!! Escape from Autoup_Loop\n");
	return result;
}

extern esp_netif_ip_info_t self_eth_ip;
void conv_id_to_ip_generalize(uint8_t gwid, uint32_t * p_ip)
{
	uint32_t octec1to3 = self_eth_ip.ip.addr & 0x00FFFFFF;
	if(p_ip) *p_ip = octec1to3 + ((100+gwid) << 24);
}

static bool each_auto_up(cc_ota_target_t target)
{
	bool res = true;
	return res;
}

static void task_autoup_process(void * arg)
{
	CC_LOGN("\033[1;36m!! AutoUpdate TCP_Client Task has started.\n\033[1;37m");

	autoup_under_process = true;
	escape_from_autoup_loop = false;

	cc_ota_target_t target = (cc_ota_target_t)(uint32_t)arg;
	get_total_bin_size(target);

	ff_erase_autoup_result(target-1);

	if(target == OTA_TARGET_ALL) {
		if(false == each_auto_up(OTA_TARGET_ETH)) goto AUTOUP_TERMINATED;
		if(false == each_auto_up(OTA_TARGET_WIFI)) goto AUTOUP_TERMINATED;
		if(false == each_auto_up(OTA_TARGET_CALLER)) goto AUTOUP_TERMINATED;
	}
	else {
		if(false == each_auto_up(target)) goto AUTOUP_TERMINATED;
	}

	AUTOUP_TERMINATED:
	if(target != OTA_TARGET_ETH) {
		cc_reboot_gw_wifi();	// in order to reset cc_spi_length
	}
	autoup_under_process = false;
	vTaskDelete(NULL);
}

char result_char(autoup_result_t res)
{
	switch(res)
	{
	case AUTOUP_RESULT_PING_FAIL : 		return 'P';
	case AUTOUP_RESULT_COMM_ERROR : 	return 'E';
	case AUTOUP_RESULT_SAME_VERSION : 	return 'S';
	case AUTOUP_RESULT_OK : 			return 'O';
	default : 							return 'x';
	}
}

static void cc_autoup_result(httpd_req_t *req)
{
}

static void cc_autoup_halt(httpd_req_t *req)
{
}

void print_wifi_caller_version(void)
{
}

static void cc_autoup_start(httpd_req_t *req, cc_ota_target_t target)
{
	CC_LOGN("[\033[1;31m%s, target %d\033[1;37m]\n", __func__, target);

	bool trigger_acked = false;
	char str[64] = {0,};

	if(autoup_under_process) {
		httpd_resp_sendstr(req, "!! AutoUpdate is already running.\r\n");
		return;
	}

	switch(target)
	{
		case OTA_TARGET_ETH :
			sprintf(str, "!! AutoUpdate ETH v%lu Start OK.\r\n", PP_CURRENT_VERSION);
			httpd_resp_sendstr(req, str);
			trigger_acked = true;
			break;

		case OTA_TARGET_WIFI :
		case OTA_TARGET_CALLER :
			memset(&ota_spi_tx, 0, sizeof(ota_data_t));
			if(target == OTA_TARGET_WIFI) {
				ota_spi_tx.header.opcode = OTA_OPCODE_WIFI_VER_GET;
				if( ota_spi_send_and_wait() ) {
					gg_wifi_version = ota_spi_rx.version;
					sprintf(str, "!! AutoUpdate WiFi v%lu Start OK.\r\n", gg_wifi_version);
					httpd_resp_sendstr(req, str);
					trigger_acked = true;
				}
				else {
					sprintf(str, "!! SPI Timeout...\r\n");
					httpd_resp_sendstr(req, str);
				}
			}
			else {
				ota_spi_tx.header.opcode = OTA_OPCODE_CALLER_VER_GET;
				if( ota_spi_send_and_wait() ) {
					gg_caller_version = ota_spi_rx.version;
					if(gg_caller_version) {
						sprintf(str, "!! AutoUpdate Caller v%lu Start OK.\r\n", gg_caller_version);
						httpd_resp_sendstr(req, str);
						trigger_acked = true;
					}
					else {
						sprintf(str, "!! F/W for Caller is NOT updated.(v0) Start Fail.\r\n");
						httpd_resp_sendstr(req, str);
					}
				}
				else {
					sprintf(str, "!! SPI Timeout...\r\n");
					httpd_resp_sendstr(req, str);
				}
			}
			break;

		case OTA_TARGET_ALL :
			sprintf(str, "!! AutoUpdate ALL Start OK.\r\n");
			httpd_resp_sendstr(req, str);
			trigger_acked = true;
			break;

		default :
			sprintf(str, "!! Wrong target %d\r\n", (int)target);
			httpd_resp_sendstr(req, str);
			break;
	}

	if(trigger_acked) {
		if( pdPASS != xTaskCreatePinnedToCore(task_autoup_process, "t_auClient", 512*16, (void*)(uint32_t)target,
				PRIORITY_TCPC, NULL, CORE_TCPC) ) {
			sprintf(str, "!! AutoUpdate Start Fail.\r\n");
			httpd_resp_sendstr(req, str);
		}
	}
}
#endif //OTA_USE_HTTPD
