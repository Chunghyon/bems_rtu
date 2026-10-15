#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "esp_log.h"

#include "nvs_flash.h"
#include "nvs.h"

#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/netdb.h"
#include "lwip/ip.h"

#include "hh_parameters.h"
#include "hh_util.h"
#include "hh_spi.h"
#include "hh_nvs.h"
#include "hh_cmds.h"

static const char *TAG = "nvs";

#define STORAGE_NAMESPACE0			"LM_CONFIG"
#define STORAGE_NAMESPACE_AUP_RES	"AUP_RESULT"

const char * autoup_ch[1] = { "ETH"};
extern ip_thru_ble_t frcd_ip;

nvs_handle handle_nvs0;
nvs_handle handle_nvs_aup;

uint32_t	data_nvs0;

char key0[NVS0_MAX][8];
extern autoup_result_t autoup_result[3][100];	//[0] Eth, [1] Wifi, [3] Caller

/**
 * Non-volatile memory0 initialization for gw information
 */
void ff_init_nvs0(void)
{
	esp_err_t err;

	bool frcd_ip_is_set = false;

	for(int n=0; n<NVS0_MAX; n++) {
		sprintf((char*)key0[n], "KEY0_%d", n);
	}

	err = nvs_open(STORAGE_NAMESPACE0, NVS_READONLY, &handle_nvs0);
	if (err != ESP_OK) {
		CC_LOGN("Opening %s failed! (%s)\n", STORAGE_NAMESPACE0, esp_err_to_name(err));
	}
	else {
		data_nvs0 = 0;

		for(int n=0; n<NVS0_MAX; n++) {
			err = nvs_get_u32(handle_nvs0, key0[n], &data_nvs0);
			if(err==ESP_OK) {
				switch(n) {

#if PP_IP_BY_PROVISION_20220713
				case NVS0_FRCD_LOC_IP :
					memcpy(frcd_ip.loc_ip, &data_nvs0, 4);
					frcd_ip_is_set = true;
					break;

				case NVS0_FRCD_LOC_NM :
					frcd_ip.loc_ip[4] = (uint8_t)data_nvs0;
					break;

				case NVS0_FRCD_LOC_GW :
					memcpy(frcd_ip.loc_gw, &data_nvs0, 4);
					break;

				case NVS0_FRCD_RMT_IP :
					memcpy(frcd_ip.rmt_ip, &data_nvs0, 4);
					break;

#endif
				default :
					break;
				}
			}
			else if(err==ESP_ERR_NVS_NOT_FOUND) {
			}
			else {
				CC_LOGE("Error(%d) reading NVS0!\n", err);
			}
		}//for(int n=0; n<NVS0_MAX; n++)
		nvs_close(handle_nvs0);

		if(frcd_ip_is_set) {
			uint32_t * p_ip = (uint32_t*)&frcd_ip.loc_ip[0];
			uint32_t * p_gw = (uint32_t*)&frcd_ip.loc_gw[0];
			uint32_t nm_b = 0xFFFFFFFF << (32-frcd_ip.loc_ip[4]);	 // netmask big-end
			uint32_t nm_l = __builtin_bswap32(nm_b);
			ESP_LOGI(TAG, "frcd_ip.loc_ip[4] %d, nm_b 0x%08lX, nm_l 0x%08lX\n", frcd_ip.loc_ip[4], nm_b, nm_l);
			cmd_set_self_eth_ip(*p_ip, nm_l, *p_gw);
		}
	}
}

/**
 * Save data to non-volatile memory
 */
esp_err_t nvs_add(nvs_sel_t nvssel, uint8_t ch)
{
	esp_err_t err = ESP_OK;

	switch(nvssel) {
	case SEL_NVS0 :
		err = nvs_open(STORAGE_NAMESPACE0, NVS_READWRITE, &handle_nvs0);
		if (err != ESP_OK) {
			CC_LOGW("Error (%d) on opening NVS0 handle!\n", err);
			return err;
		}
		else {
			if(ch == ALL) {
				for(int n=0; n<NVS0_MAX; n++) {
					switch(n) {
#ifdef PC_INTERFACE_TEST_VERSION
						case NVS0_HUB_CONFIG0:
						case NVS0_HUB_CONFIG1:
						case NVS0_HUB_CONFIG2:
						case NVS0_HUB_CONFIG3:
						case NVS0_HUB_CONFIG4:
						case NVS0_HUB_CONFIG5:
						case NVS0_HUB_CONFIG6:
						case NVS0_HUB_CONFIG7:
						case NVS0_HUB_CONFIG8:
						case NVS0_HUB_CONFIG9:
						case NVS0_HUB_CONFIGA:
						memcpy(&data_nvs0, &test_hub_config[n-NVS0_HUB_CONFIG0], sizeof(hub_config_t));
						break;
#endif

					default :
						break;
					}

					err = nvs_set_u32(handle_nvs0, key0[n], data_nvs0);
					if(err != ESP_OK) {
						CC_LOGE("NVS0[%d] Write failed!\n", n);
						break;
					}
					else {
						err = nvs_commit(handle_nvs0);
						if(err != ESP_OK) {
							CC_LOGE("NVS0 Write/Commit Failed!\n");
							break;
						}
					}//else
				}//for

				nvs_close(handle_nvs0);
				return err;
			}
			else if(ch < NVS0_MAX) {
				switch(ch) {
				case NVS0_FRCD_LOC_IP :
					memcpy(&data_nvs0, frcd_ip.loc_ip, 4);
					break;
				case NVS0_FRCD_LOC_NM :
					data_nvs0 = frcd_ip.loc_ip[4];
					break;
				case NVS0_FRCD_LOC_GW :
					memcpy(&data_nvs0, frcd_ip.loc_gw, 4);
					break;
				case NVS0_FRCD_RMT_IP :
					memcpy(&data_nvs0, frcd_ip.rmt_ip, 4);
					break;
				default :
					break;
				}

				err = nvs_set_u32(handle_nvs0, key0[ch], data_nvs0);
				if(err != ESP_OK) {
					CC_LOGE("NVS0 Write failed!\n");
				}
				else {
					err = nvs_commit(handle_nvs0);
					if(err != ESP_OK) {
						CC_LOGE("NVS0 Write failed!\n");
					}
				}
				nvs_close(handle_nvs0);
				return err;
			}
			else {
				CC_LOGE("Wrong CH number.\n");
				nvs_close(handle_nvs0);
				return ESP_ERR_INVALID_SIZE;
			}
		}
		break;

	default :
		break;
	}
	return err;
}

/**
 * del non-volatile memory
 */
esp_err_t nvs_del(nvs_sel_t nvssel, uint8_t ch)
{
	esp_err_t err = ESP_OK;

	switch(nvssel) {
	case SEL_NVS0 :

		err = nvs_open(STORAGE_NAMESPACE0, NVS_READWRITE, &handle_nvs0);

		if (err != ESP_OK) {
			CC_LOGE("Error (%d) opening NVS0 handle!\n", err);
			return err;
		}
		else {
			if(ch == ALL) {
				CC_LOGD("Erasing all configuration information...\n");
				err = nvs_erase_all(handle_nvs0);
			}
			else if(ch < NVS0_MAX) {
				err = nvs_erase_key(handle_nvs0, key0[ch]);
			}
			else {
				CC_LOGE("Wrong NVS0 CH number.\n");
				nvs_close(handle_nvs0);
				return ESP_ERR_INVALID_SIZE;
			}

			if(err == ESP_OK) {
				err = nvs_commit(handle_nvs0);
				if(err != ESP_OK)
				{
					CC_LOGE("NVS0 Commit Failed!\n");
				}
			}

			nvs_close(handle_nvs0);
			return err;
		}
		break;

	default :
		break;
	}
	return err;
}
/**
 * Initialize the system
 */
void return_to_initial_status(void)
{
	esp_err_t err = ESP_OK;

	err = nvs_open(STORAGE_NAMESPACE0, NVS_READWRITE, &handle_nvs0);
	if (err == ESP_OK) {
		CC_LOGD("Erasing all configurations...\n");
		err = nvs_erase_all(handle_nvs0);
		if(err == ESP_OK) {
			err = nvs_commit(handle_nvs0);
			if(err == ESP_OK)
			{
				CC_LOGI("all Configurations are erased successfully.\n");
			}
		}
		nvs_close(handle_nvs0);
	}

	err = nvs_open(STORAGE_NAMESPACE_AUP_RES, NVS_READWRITE, &handle_nvs_aup);
	if (err == ESP_OK) {
		CC_LOGD("Erasing AutoUP results...\n");
		err = nvs_erase_all(handle_nvs_aup);
		if(err == ESP_OK) {
			err = nvs_commit(handle_nvs_aup);
			if(err == ESP_OK)
			{
				CC_LOGI("all IDs are erased successfull.\n");
			}
		}
		nvs_close(handle_nvs_aup);
	}
}

void cc_init_nvs_autoup_result(void)
{
	esp_err_t err;
	err = nvs_open(STORAGE_NAMESPACE_AUP_RES, NVS_READONLY, &handle_nvs_aup);
	if (err != ESP_OK) {
		CC_LOGD("Error (0x%04X) opening %s handle!\n", err, STORAGE_NAMESPACE_AUP_RES);
		return;
	}

	CC_LOGD("Loading %s value...\n", autoup_ch[0]);
	size_t len;
	err = nvs_get_blob(handle_nvs_aup, autoup_ch[0], NULL, &len);
	if(err != ESP_OK) {
		CC_LOGN("%s load result 0x%04X\n", autoup_ch[0], err);
	}
	else {
		err = nvs_get_blob(handle_nvs_aup, autoup_ch[0], autoup_result[0], &len);
		if(err!=ESP_OK) {
			CC_LOGE("Error(0x%04X) on loading nvs value!\n", err);
		}
	}

	nvs_close(handle_nvs_aup);
}

esp_err_t ff_store_autoup_result(uint8_t target)	// 0 eth, 1 wifi, 2 caller, 3 all
{
	esp_err_t err = ESP_OK;

	if(target > 3) {
		CC_LOGE("ESP_ERR_NVS_PART_NOT_FOUND\n");
		return ESP_ERR_NVS_PART_NOT_FOUND;
	}

	err = nvs_open(STORAGE_NAMESPACE_AUP_RES, NVS_READWRITE, &handle_nvs_aup);
	if (err != ESP_OK) {
		CC_LOGW("Error (%d) on opening STORAGE_NAMESPACE_AUP_RES!\n", err);
		return err;
	}

	err = nvs_set_blob(handle_nvs_aup, autoup_ch[target], autoup_result[target], sizeof(autoup_result[target]));
	if(err != ESP_OK) {
		CC_LOGE("NVS_STORAGE_NAMESPACE_AUP_RES[%d] Write failed!\n", target);
	}
	else {
		err = nvs_commit(handle_nvs_aup);
		if(err != ESP_OK) {
			CC_LOGE("NVS_STORAGE_NAMESPACE_AUP_RES Write/Commit Failed!\n");
		}
		else
			CC_LOGN("nvs store %s OK\n", autoup_ch[target]);
	}

	nvs_close(handle_nvs_aup);
	return err;
}

esp_err_t ff_erase_autoup_result(uint8_t target)	// 0 eth, 1 wifi, 3 caller, 4 all
{
	esp_err_t err = ESP_OK;

	if(target > 3) {
		return ESP_ERR_NVS_PART_NOT_FOUND;
	}

	err = nvs_open(STORAGE_NAMESPACE_AUP_RES, NVS_READWRITE, &handle_nvs_aup);
	if (err != ESP_OK) {
		CC_LOGW("Error (%d) on opening STORAGE_NAMESPACE_AUP_RES!\n", err);
		return err;
	}

	if(target == 3) {
		memset(autoup_result, 0, sizeof(autoup_result));
		err = nvs_erase_all(handle_nvs_aup);
	}
	else {
		memset(autoup_result[target], 0, sizeof(autoup_result[target]));
		err = nvs_erase_key(handle_nvs_aup, autoup_ch[target]);
	}
	if(err == ESP_OK) err = nvs_commit(handle_nvs_aup);

	nvs_close(handle_nvs_aup);
	return err;
}

