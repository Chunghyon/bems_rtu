#include "webserver.h"
#include "ads131m08.h"
#include "pulse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "web";

bool web_update_enabled = false;
int  web_update_period_sec = 5;

#define NVS_NAMESPACE   "rtu_cfg"
#define NVS_KEY_PERIOD  "web_period"

static httpd_handle_t web_server = NULL;

void webserver_nvs_load(void)
{
    nvs_handle_t handle;
    int32_t val = 0;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        if (nvs_get_i32(handle, NVS_KEY_PERIOD, &val) == ESP_OK) {
            if (val >= 1 && val <= 300) {
                web_update_period_sec = (int)val;
                ESP_LOGI(TAG, "Loaded web update period from NVS: %d s", web_update_period_sec);
            }
        }
        nvs_close(handle);
    }
}

void webserver_nvs_save_period(int period)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_i32(handle, NVS_KEY_PERIOD, period);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    char *buf = malloc(3072);
    if (!buf) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char refresh_tag[64] = "";
    char update_status[40] = "OFF";
    if (web_update_enabled && web_update_period_sec >= 1) {
        snprintf(refresh_tag, sizeof(refresh_tag),
                 "<meta http-equiv='refresh' content='%d'>", web_update_period_sec);
        snprintf(update_status, sizeof(update_status), "ON (%ds)", web_update_period_sec);
    }

    uint32_t pulses[4] = {0};
    pulse_get_counts(pulses);

    int32_t adc1[ADS131M08_NUM_CH] = {0};
    int32_t adc2[ADS131M08_NUM_CH] = {0};
    ads131m08_get_avg(adc1, adc2);

    float vrms[3], irms[3], kw[3], pf[3];
    uint32_t sps = 0;
    ads131m08_get_power(vrms, irms, kw, pf, &sps);

    int len = snprintf(buf, 3072,
        "<!DOCTYPE html><html><head><meta charset='utf-8'>%s"
        "<title>BEMS RTU</title>"
        "<style>"
        "body{font-family:sans-serif;margin:1em;}"
        ".row{display:flex;gap:1em;flex-wrap:nowrap;}"
        ".box{border:1px solid #ccc;padding:0.5em 1em;flex:1;min-width:0;}"
        "table{border-collapse:collapse;width:100%%;}"
        "td,th{border:1px solid #ddd;padding:2px 6px;text-align:right;font-size:0.9em;}"
        "h1{margin:0 0 0.3em 0;font-size:1.3em;}"
        "h2{margin:0 0 0.3em 0;font-size:1em;}"
        "</style></head>"
        "<body><h1>BEMS RTU Status</h1>"
        "<p style='margin:0 0 0.5em 0;'><small>Web update: %s</small></p>"
        "<div class='row'>"
        "<div class='box'><h2>Pulse Counters</h2>"
        "<table><tr><th>CH</th><th>Count</th></tr>"
        "<tr><td>1</td><td>%lu</td></tr><tr><td>2</td><td>%lu</td></tr>"
        "<tr><td>3</td><td>%lu</td></tr><tr><td>4</td><td>%lu</td></tr></table></div>"
        "<div class='box'><h2>ADC1 (1s avg)</h2><table><tr><th>CH</th><th>Value</th></tr>",
        refresh_tag,
        update_status,
        pulses[0], pulses[1], pulses[2], pulses[3]);

    for (int ch = 0; ch < ADS131M08_NUM_CH && len < 3072 - 80; ch++) {
        len += snprintf(buf + len, 3072 - len,
                        "<tr><td>%s</td><td>%ld</td></tr>", ads131m08_ch_name_adc1(ch), adc1[ch]);
    }
    len += snprintf(buf + len, 3072 - len, "</table></div>"
        "<div class='box'><h2>ADC2 (1s avg)</h2><table><tr><th>CH</th><th>Value</th></tr>");

    for (int ch = 0; ch < ADS131M08_NUM_CH && len < 3072 - 80; ch++) {
        len += snprintf(buf + len, 3072 - len,
                        "<tr><td>%s</td><td>%ld</td></tr>", ads131m08_ch_name_adc2(ch), adc2[ch]);
    }
    len += snprintf(buf + len, 3072 - len,
        "</table></div></div>"
        "<div class='box'><h2>3-phase (100 ms)</h2>"
        "<table><tr><th>Ph</th><th>Vrms</th><th>Irms</th><th>kW</th><th>PF</th></tr>"
        "<tr><td>1</td><td>%.2f</td><td>%.3f</td><td>%.3f</td><td>%.3f</td></tr>"
        "<tr><td>2</td><td>%.2f</td><td>%.3f</td><td>%.3f</td><td>%.3f</td></tr>"
        "<tr><td>3</td><td>%.2f</td><td>%.3f</td><td>%.3f</td><td>%.3f</td></tr>"
        "</table><small>%lu sps</small></div>"
        "<p><small>ESP32-S3 RTU</small></p></body></html>",
        vrms[0], irms[0], kw[0], pf[0],
        vrms[1], irms[1], kw[1], pf[1],
        vrms[2], irms[2], kw[2], pf[2],
        (unsigned long)sps);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    free(buf);
    return ESP_OK;
}

void webserver_start(void)
{
    if (web_server) {
        ESP_LOGI(TAG, "Web server already running");
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 8192;

    if (httpd_start(&web_server, &config) == ESP_OK) {
        httpd_uri_t uri_get = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(web_server, &uri_get);
        ESP_LOGI(TAG, "Web server started on port 80");
    } else {
        ESP_LOGE(TAG, "Failed to start web server");
        web_server = NULL;
    }
}

void webserver_stop(void)
{
    if (web_server) {
        httpd_stop(web_server);
        web_server = NULL;
        ESP_LOGI(TAG, "Web server stopped");
    }
}
