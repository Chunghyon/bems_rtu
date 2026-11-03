#include "webserver.h"
#include "ads131m08.h"
#include "pulse.h"
#include "pca9555.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "web";

// Show raw (average) value, and V/A as RMS
static void format_eng_rms(char *buf, size_t n, float rms_counts, bool is_current, int ch_code)
{
    if (is_current) {
        float scale = (ch_code >= 0 && ch_code <= 2) ? PP_I_PER_COUNT_10OHM : PP_I_PER_COUNT_49R9;
        float a = rms_counts * scale;
        snprintf(buf, n, "%.4f A", a);
    } else {
        float v = rms_counts * PP_V_PER_COUNT;
        snprintf(buf, n, "%.4f V", v);
    }
}

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

static esp_err_t control_post_handler(httpd_req_t *req)
{
    char buf[256] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    // Simple form parsing: action=xxx&key=val...
    char action[32] = {0};
    if (httpd_query_key_value(buf, "action", action, sizeof(action)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing action");
        return ESP_FAIL;
    }

    if (strcmp(action, "period") == 0) {
        char val[16] = {0};
        if (httpd_query_key_value(buf, "period", val, sizeof(val)) == ESP_OK) {
            int p = atoi(val);
            if (p >= 1 && p <= 300) {
                web_update_period_sec = p;
                webserver_nvs_save_period(p);
                ESP_LOGI(TAG, "WEB period set to %d s", p);
            }
        }
    } else if (strcmp(action, "phase") == 0) {
        char val[8] = {0};
        if (httpd_query_key_value(buf, "mode", val, sizeof(val)) == ESP_OK) {
            if (val[0] == '1') { ads131m08_set_phase_mode(POWER_MODE_SINGLE); ESP_LOGI(TAG, "WEB phase mode = SINGLE"); }
            else if (val[0] == '3') { ads131m08_set_phase_mode(POWER_MODE_THREE); ESP_LOGI(TAG, "WEB phase mode = THREE"); }
        }
    } else if (strcmp(action, "current") == 0) {
        char c0[8]={0}, c1[8]={0}, c2[8]={0};
        httpd_query_key_value(buf, "c0", c0, sizeof(c0));
        httpd_query_key_value(buf, "c1", c1, sizeof(c1));
        httpd_query_key_value(buf, "c2", c2, sizeof(c2));
        ads131m08_set_current_source(CURRENT_SRC_CUSTOM, atoi(c0), atoi(c1), atoi(c2));
        ESP_LOGI(TAG, "WEB current channels = %s, %s, %s", c0, c1, c2);
    } else if (strcmp(action, "voltage") == 0) {
        char v0[8]={0}, v1[8]={0}, v2[8]={0};
        httpd_query_key_value(buf, "v0", v0, sizeof(v0));
        httpd_query_key_value(buf, "v1", v1, sizeof(v1));
        httpd_query_key_value(buf, "v2", v2, sizeof(v2));
        ads131m08_set_voltage_channels(atoi(v0), atoi(v1), atoi(v2));
        ESP_LOGI(TAG, "WEB voltage channels = %s, %s, %s", v0, v1, v2);
    } else if (strcmp(action, "common") == 0) {
        // Expect common=1&mode=c|v|d&range=l|h (range only for voltage)
        char cm[8]={0}, md[8]={0}, rg[8]={0};
        if (httpd_query_key_value(buf, "common", cm, sizeof(cm)) == ESP_OK &&
            httpd_query_key_value(buf, "mode", md, sizeof(md)) == ESP_OK) {
            int common = atoi(cm);
            char mode = md[0];
            char range = 'l';
            if (httpd_query_key_value(buf, "range", rg, sizeof(rg)) == ESP_OK) range = rg[0];
            if (common >= 1 && common <= 6) {
                cv_sel_t cv = CV_SEL_CURRENT;
                di_sel_t di = DI_SEL_ANALOG;
                r_sel_t rs = (range == 'h') ? R_SEL_99K8 : R_SEL_49K9;  // apply range for both current and voltage
                if (mode == 'v') {
                    cv = CV_SEL_VOLTAGE;
                } else if (mode == 'd' && common >= 3) {
                    di = DI_SEL_DIGITAL;
                    rs = R_SEL_49K9;  // irrelevant for DI
                } else if (mode != 'c') {
                    // invalid
                }
                if (!(common <= 2 && mode == 'd')) {
                    pca9555_config_common((common_sel_t)common, di, cv, rs);
                    ESP_LOGI(TAG, "WEB common%d -> mode=%c range=%c (cv=%d di=%d rs=%d)",
                             common, mode, range, cv, di, rs);
                    // DI mode does not use ADC: turn off corresponding ADC2 channel
                    if (mode == 'd' && common >= 3 && common <= 6) {
                        int adc_ch = common - 3;  // C3→ADC2_CH0 ... C6→ADC2_CH3
                        ads131m08_set_channel_enable(2, adc_ch, false);
                        ESP_LOGI(TAG, "WEB DI mode: ADC2 CH%d forced OFF", adc_ch);
                    }
                    nvs_handle_t h;
                    if (nvs_open("rtu_cfg", NVS_READWRITE, &h) == ESP_OK) {
                        char key[8];
                        snprintf(key, sizeof(key), "cm%d", common);
                        uint8_t val = (uint8_t)((di << 4) | (cv & 0x3) | ((rs & 1) << 3));
                        nvs_set_u8(h, key, val);
                        nvs_commit(h);
                        nvs_close(h);
                    }
                }
            }
        }
    } else if (strcmp(action, "relay") == 0) {
        char rly[8]={0}, st[8]={0};
        if (httpd_query_key_value(buf, "relay", rly, sizeof(rly)) == ESP_OK &&
            httpd_query_key_value(buf, "state", st, sizeof(st)) == ESP_OK) {
            int relay = atoi(rly);
            int state = atoi(st);
            if (relay >= 1 && relay <= 16 && (state == 0 || state == 1)) {
                pca9555_set_pin((pca9555_port_t)(relay - 1), state);
                ESP_LOGI(TAG, "WEB relay %d -> %s", relay, state ? "NO" : "NC");
                // Keep common-channel NVS in sync if this pin belongs to a common
                // (optional but keeps boot restore consistent)
            }
        }
    } else if (strcmp(action, "toggle") == 0) {
        char what[16]={0};
        if (httpd_query_key_value(buf, "what", what, sizeof(what)) == ESP_OK) {
            if (strcmp(what, "adc") == 0) { adc_log_enabled = !adc_log_enabled; ESP_LOGI(TAG, "WEB toggle ADC log -> %s", adc_log_enabled ? "ON" : "OFF"); }
            else if (strcmp(what, "web") == 0) { web_update_enabled = !web_update_enabled; ESP_LOGI(TAG, "WEB toggle web update -> %s", web_update_enabled ? "ON" : "OFF"); }
            else if (strcmp(what, "phase") == 0) { phase_log_enabled = !phase_log_enabled; ESP_LOGI(TAG, "WEB toggle phase log -> %s", phase_log_enabled ? "ON" : "OFF"); }
        }
    } else if (strcmp(action, "calib") == 0) {
        ads131m08_calibrate_measure(64);
        ESP_LOGI(TAG, "WEB full calibration done");
    } else if (strcmp(action, "ch_en") == 0) {
        char adc_s[8]={0}, ch_s[8]={0}, en_s[8]={0};
        if (httpd_query_key_value(buf, "adc", adc_s, sizeof(adc_s)) == ESP_OK &&
            httpd_query_key_value(buf, "ch", ch_s, sizeof(ch_s)) == ESP_OK &&
            httpd_query_key_value(buf, "en", en_s, sizeof(en_s)) == ESP_OK) {
            int adc = atoi(adc_s);
            int ch = atoi(ch_s);
            bool en = (atoi(en_s) != 0);
            ads131m08_set_channel_enable(adc, ch, en);
            ESP_LOGI(TAG, "WEB ADC%d CH%d %s", adc, ch, en ? "ON" : "OFF");
        }
    } else if (strcmp(action, "calib_ch") == 0) {
        char adc_s[8]={0}, ch_s[8]={0};
        if (httpd_query_key_value(buf, "adc", adc_s, sizeof(adc_s)) == ESP_OK &&
            httpd_query_key_value(buf, "ch", ch_s, sizeof(ch_s)) == ESP_OK) {
            int adc = atoi(adc_s);
            int ch = atoi(ch_s);
            ads131m08_calibrate_channel(adc, ch, 64);
            ESP_LOGI(TAG, "WEB channel calibration ADC%d ch%d", adc, ch);
        }
    }

    // Redirect back to main page
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    char *buf = malloc(32768);
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
    float rms1[ADS131M08_NUM_CH] = {0}, rms2[ADS131M08_NUM_CH] = {0};
    ads131m08_get_rms(rms1, rms2);

    float vrms[3], irms[3], kw[3], pf[3];
    uint32_t sps = 0;
    ads131m08_get_power(vrms, irms, kw, pf, &sps);

    power_phase_mode_t pmode = ads131m08_get_phase_mode();
    int c0,c1,c2, v0,v1,v2;
    ads131m08_get_current_channels(&c0,&c1,&c2);
    ads131m08_get_voltage_channels(&v0,&v1,&v2);

    int len = snprintf(buf, 32768,
        "<!DOCTYPE html><html><head><meta charset='utf-8'>%s"
        "<title>BEMS RTU</title>"
        "<style>"
        "body{font-family:sans-serif;margin:1em;}"
        ".row{display:flex;gap:1em;flex-wrap:nowrap;}"
        ".box{border:1px solid #ccc;padding:0.5em 1em;flex:1;min-width:0;}"
        "table{border-collapse:collapse;width:100%%;table-layout:fixed;}"
        "td,th{border:1px solid #ddd;padding:2px 4px;text-align:right;font-size:0.85em;white-space:nowrap;overflow:hidden;}"
        ".pulse-box{flex:0 0 130px;}"
        ".pulse-box td:first-child,.pulse-box th:first-child{width:28px;}"
        ".pulse-box td:last-child,.pulse-box th:last-child{width:60px;}"
        ".cv-box,.common-box{flex:1;min-width:200px;}"
        ".cv-box td:first-child,.common-box td:first-child{width:38%%;text-align:left;}"
        ".cv-box td:nth-child(2),.common-box td:nth-child(2){width:48%%;}"
        ".cv-box td:last-child,.common-box td:last-child{width:14%%;text-align:center;}"
        "h1{margin:0 0 0.3em 0;font-size:1.3em;}"
        "h2{margin:0.4em 0;font-size:1.1em;}"
        "form{margin:0.2em 0;}"
        "input,select,button{margin:0.1em;padding:2px 6px;}"
        "input[type=number]{width:2.2em;}"
        ".btn-on{background:#4CAF50;color:#fff;border:1px solid #388E3C;width:3.2em;}"
        ".btn-off{background:#9E9E9E;color:#fff;border:1px solid #757575;width:3.2em;}"
        ".btn-no{background:#78909C;color:#fff;border:1px solid #546E7A;width:3.2em;}"
        ".btn-nc{background:#FF9800;color:#fff;border:1px solid #F57C00;width:3.2em;}"
        ".off-row{opacity:0.4;color:#888;}"
        ".off-row button{display:none;}"
        ".line{white-space:nowrap;margin:0.3em 0;}"
        ".relay-row{display:flex;flex-wrap:nowrap;gap:4px;margin:4px 0;}"
        ".relay-item{flex:0 0 88px;text-align:center;}"
        ".relay-desc{margin-left:8px;align-self:center;font-size:0.85em;color:#555;}"
        ".relay-item form{margin:0;}"
        ".relay-item button{margin-left:2px;}"
        "</style>"
        "<script>"
        "var refreshPaused=false;"
        "document.addEventListener('focusin',function(e){"
        "  if(e.target.tagName==='SELECT'||e.target.tagName==='INPUT'){"
        "    var m=document.querySelector('meta[http-equiv=refresh]');"
        "    if(m){m.remove();refreshPaused=true;}"
        "  }"
        "});"
        "document.addEventListener('focusout',function(){"
        "  if(refreshPaused){setTimeout(function(){location.reload();},1500);}"
        "});"
        "document.addEventListener('change',function(e){"
        "  if(e.target.name==='mode'){"
        "    var form=e.target.form;"
        "    var range=form.querySelector('select[name=range]');"
        "    if(range){"
        "      if(e.target.value==='d'){range.disabled=true;range.style.opacity='0.4';}"
        "      else{range.disabled=false;range.style.opacity='1';}"
        "    }"
        "  }"
        "});"
        "window.addEventListener('load',function(){"
        "  document.querySelectorAll('select[name=mode]').forEach(function(sel){"
        "    var range=sel.form.querySelector('select[name=range]');"
        "    if(range && sel.value==='d'){range.disabled=true;range.style.opacity='0.4';}"
        "  });"
        "});"
        "</script>"
        "</head><body>"
        "<h1>BEMS RTU</h1>"
        "<p>Web update: %s | Phase: %s | ADC log: %s | Phase log: %s</p>"
        "<div style='margin:0.3em 0;'>"
        "<form method='POST' action='/control' style='display:inline;'>"
        "<input type='hidden' name='action' value='toggle'><input type='hidden' name='what' value='adc'>"
        "<button type='submit'>Toggle ADC log</button></form> "
        "<form method='POST' action='/control' style='display:inline;'>"
        "<input type='hidden' name='action' value='toggle'><input type='hidden' name='what' value='web'>"
        "<button type='submit'>Toggle web update</button></form> "
        "<form method='POST' action='/control' style='display:inline;'>"
        "<input type='hidden' name='action' value='toggle'><input type='hidden' name='what' value='phase'>"
        "<button type='submit'>Toggle phase log</button></form> "
        "<form method='POST' action='/control' style='display:inline;'>"
        "<input type='hidden' name='action' value='calib'>"
        "<button type='submit'>전체Calibrate</button></form>"
        "</div>",
        refresh_tag, update_status,
        (pmode == POWER_MODE_SINGLE) ? "SINGLE" : "THREE",
        adc_log_enabled ? "ON" : "OFF",
        phase_log_enabled ? "ON" : "OFF");

    // Control panel
    len += snprintf(buf + len, 32768 - len,
        "<div class='box'><h2>Control</h2>"
        "<div class='line'>"
        "<form method='POST' action='/control' style='display:inline;'>"
        "<input type='hidden' name='action' value='period'>"
        "Web period (s): <input name='period' type='number' min='1' max='300' value='%d' size='4' onchange='this.form.submit()'>"
        "</form>"
        "&nbsp;&nbsp;"
        "<form method='POST' action='/control' style='display:inline;'>"
        "<input type='hidden' name='action' value='phase'>"
        "Phase mode: <select name='mode' onchange='this.form.submit()'><option value='1'%s>Single</option><option value='3'%s>Three</option></select>"
        "</form>"
        "</div>"
        "<div class='line'>"
        "<form method='POST' action='/control' style='display:inline;'>"
        "<input type='hidden' name='action' value='current'>"
        "Current ch: <input name='c0' type='number' value='%d' size='2'> "
        "<input name='c1' type='number' value='%d' size='2'> "
        "<input name='c2' type='number' value='%d' size='2'> "
        "<button type='submit'>Set</button></form>"
        "&nbsp;&nbsp;"
        "<form method='POST' action='/control' style='display:inline;'>"
        "<input type='hidden' name='action' value='voltage'>"
        "Voltage ch: <input name='v0' type='number' value='%d' size='2'> "
        "<input name='v1' type='number' value='%d' size='2'> "
        "<input name='v2' type='number' value='%d' size='2'> "
        "<button type='submit'>Set</button></form>"
        "</div>",
        web_update_period_sec,
        (pmode == POWER_MODE_SINGLE) ? " selected" : "",
        (pmode == POWER_MODE_THREE) ? " selected" : "",
        c0, c1, c2, v0, v1, v2);

    // ADC channel enable/disable
    len += snprintf(buf + len, 32768 - len, "<div style='margin-top:0.4em;'><b>ADC1 CH enable</b><br>");
    for (int ch = 0; ch < 8 && len < 32768 - 120; ch++) {
        bool on = ads131m08_get_channel_enable(1, ch);
        len += snprintf(buf + len, 32768 - len,
            "<form method='POST' action='/control' style='display:inline-block;margin:1px;'>"
            "<input type='hidden' name='action' value='ch_en'>"
            "<input type='hidden' name='adc' value='1'>"
            "<input type='hidden' name='ch' value='%d'>"
            "<input type='hidden' name='en' value='%d'>"
            "CH%d <button type='submit' class='%s'>%s</button></form> ",
            ch, on ? 0 : 1, ch, on ? "btn-on" : "btn-off", on ? "ON" : "OFF");
    }
    len += snprintf(buf + len, 32768 - len, "<br><b>ADC2 CH enable</b><br>");
    for (int ch = 0; ch <= 3 && len < 32768 - 120; ch++) {
        bool on = ads131m08_get_channel_enable(2, ch);
        len += snprintf(buf + len, 32768 - len,
            "<form method='POST' action='/control' style='display:inline-block;margin:1px;'>"
            "<input type='hidden' name='action' value='ch_en'>"
            "<input type='hidden' name='adc' value='2'>"
            "<input type='hidden' name='ch' value='%d'>"
            "<input type='hidden' name='en' value='%d'>"
            "CH%d <button type='submit' class='%s'>%s</button></form> ",
            ch, on ? 0 : 1, ch, on ? "btn-on" : "btn-off", on ? "ON" : "OFF");
    }
    len += snprintf(buf + len, 32768 - len, "</div>");

    // Common 1~6 individual controls (reflect actual PCA9555 pin state)
    len += snprintf(buf + len, 32768 - len, "<div style='margin-top:0.5em;'><b>Common channels</b><br>");
    len += snprintf(buf + len, 32768 - len, "<div class='common-row'>");
    for (int cm = 1; cm <= 6 && len < 32768 - 250; cm++) {
        // Derive current mode/range from actual pin levels
        pca9555_port_t cur_port = (pca9555_port_t)(PCA_CUR_SEL1 + (cm - 1));
        pca9555_port_t v_port   = (pca9555_port_t)(PCA_10V_SEL1 + (cm - 1));
        int cur_level = pca9555_get_pin(cur_port);   // 1=current(49.9ohm), 0=open
        int v_level   = pca9555_get_pin(v_port);     // 1=49.9k, 0=99.8k
        int di_level  = 0;
        if (cm >= 3) {
            pca9555_port_t di_port;
            if (cm == 3) di_port = PCA_DI_SEL1;
            else if (cm == 4) di_port = PCA_DI_SEL2;
            else if (cm == 5) di_port = PCA_DI_SEL3;
            else di_port = PCA_DI_SEL4;
            di_level = pca9555_get_pin(di_port);     // 1=pulse/DI, 0=ADC
        }

        char mode = 'c';
        char range = (v_level == 1) ? 'l' : 'h';   // 1=49.9k=Low, 0=99.8k=High
        if (cm >= 3 && di_level == 1) {
            mode = 'd';
        } else if (cur_level == 1) {
            mode = 'c';
        } else {
            mode = 'v';
        }

        // C1~C2 dim when corresponding ADC1 CH6/CH7 is OFF
        // C3~C6 do NOT dim when ADC2 is OFF (DI mode intentionally turns ADC off)
        bool cm_on = true;
        if (cm <= 2) cm_on = ads131m08_get_channel_enable(1, 5 + cm);  // common1~2 = ADC1 ch6~7
        // common3~6: always enabled in UI (DI doesn't use ADC)

        const char *extra = cm_on ? "" : "opacity:0.4;pointer-events:none;";
        len += snprintf(buf + len, 32768 - len,
            "<form method='POST' action='/control' style='display:inline-block;margin:2px;%s'>"
            "<input type='hidden' name='action' value='common'>"
            "<input type='hidden' name='common' value='%d'>"
            "C%d: <select name='mode' onchange='this.form.submit()'>"
            "<option value='c'%s>Current</option>"
            "<option value='v'%s>Voltage</option>"
            "%s"
            "</select>"
            "<select name='range' onchange='this.form.submit()'>"
            "<option value='l'%s>Low (49.9k)</option>"
            "<option value='h'%s>High (99.8k)</option>"
            "</select>"
            "</form>",
            extra, cm, cm,
            (mode == 'c') ? " selected" : "",
            (mode == 'v') ? " selected" : "",
            (cm >= 3) ? (mode == 'd' ? "<option value='d' selected>DI</option>" : "<option value='d'>DI</option>") : "",
            (range == 'l') ? " selected" : "",
            (range == 'h') ? " selected" : "");
        if (cm == 2) {
            len += snprintf(buf + len, 32768 - len, "</div><div class='common-row'>");
        }
    }
    len += snprintf(buf + len, 32768 - len, "</div></div>");

    // Relays grouped by function
    len += snprintf(buf + len, 32768 - len, "<div style='margin-top:0.5em;'><b>Relays</b><br>");

    // R01~R06 : 10V select
    len += snprintf(buf + len, 32768 - len, "<div class='relay-row'>");
    for (int r = 1; r <= 6 && len < 32768 - 150; r++) {
        int st = pca9555_get_pin((pca9555_port_t)(r - 1));
        len += snprintf(buf + len, 32768 - len,
            "<div class='relay-item'><form method='POST' action='/control'>"
            "<input type='hidden' name='action' value='relay'>"
            "<input type='hidden' name='relay' value='%d'>"
            "<input type='hidden' name='state' value='%d'>"
            "R%02d <button type='submit' class='%s'>%s</button></form></div>",
            r, st ? 0 : 1, r, st ? "btn-no" : "btn-nc", st ? "NO" : "NC");
    }
    len += snprintf(buf + len, 32768 - len, "<span class='relay-desc'>(NO 49.9K, NC 99.8K)</span></div>");

    // R09~R14 : Current/Voltage
    len += snprintf(buf + len, 32768 - len, "<div class='relay-row'>");
    for (int r = 9; r <= 14 && len < 32768 - 150; r++) {
        int st = pca9555_get_pin((pca9555_port_t)(r - 1));
        len += snprintf(buf + len, 32768 - len,
            "<div class='relay-item'><form method='POST' action='/control'>"
            "<input type='hidden' name='action' value='relay'>"
            "<input type='hidden' name='relay' value='%d'>"
            "<input type='hidden' name='state' value='%d'>"
            "R%02d <button type='submit' class='%s'>%s</button></form></div>",
            r, st ? 0 : 1, r, st ? "btn-no" : "btn-nc", st ? "NO" : "NC");
    }
    len += snprintf(buf + len, 32768 - len, "<span class='relay-desc'>(NO Current, NC Voltage)</span></div>");

    // R07/08/15/16 : DI select (aligned under R11~R14)
    len += snprintf(buf + len, 32768 - len, "<div class='relay-row'>");
    len += snprintf(buf + len, 32768 - len, "<div class='relay-item' style='visibility:hidden;'></div>");
    len += snprintf(buf + len, 32768 - len, "<div class='relay-item' style='visibility:hidden;'></div>");
    int di_relays[] = {7, 8, 15, 16};
    for (int i = 0; i < 4 && len < 32768 - 150; i++) {
        int r = di_relays[i];
        int st = pca9555_get_pin((pca9555_port_t)(r - 1));
        len += snprintf(buf + len, 32768 - len,
            "<div class='relay-item'><form method='POST' action='/control'>"
            "<input type='hidden' name='action' value='relay'>"
            "<input type='hidden' name='relay' value='%d'>"
            "<input type='hidden' name='state' value='%d'>"
            "R%02d <button type='submit' class='%s'>%s</button></form></div>",
            r, st ? 0 : 1, r, st ? "btn-no" : "btn-nc", st ? "NO" : "NC");
    }
    len += snprintf(buf + len, 32768 - len, "<span class='relay-desc'>(NO Digital, NC Analog)</span></div></div>");

    // Data sections
    len += snprintf(buf + len, 32768 - len,
        "<div class='row'>"
        "<div class='box pulse-box'><h2>Pulse</h2><table><tr><th>CH</th><th>Count</th></tr>");

    // Pulse 1~4 correspond to Common3~6 DI mode
    for (int p = 0; p < 4 && len < 32768 - 80; p++) {
        int cm = p + 3;  // common3~6
        pca9555_port_t di_port;
        if (cm == 3) di_port = PCA_DI_SEL1;
        else if (cm == 4) di_port = PCA_DI_SEL2;
        else if (cm == 5) di_port = PCA_DI_SEL3;
        else di_port = PCA_DI_SEL4;
        int di_level = pca9555_get_pin(di_port);  // 0 = DI/pulse
        bool is_di = (di_level == 1);
        if (is_di) {
            len += snprintf(buf + len, 32768 - len,
                "<tr><td>%d</td><td>%lu</td></tr>", p + 1, pulses[p]);
        } else {
            len += snprintf(buf + len, 32768 - len,
                "<tr class='off-row'><td>%d</td><td>-</td></tr>", p + 1);
        }
    }
    len += snprintf(buf + len, 32768 - len, "</table></div>"
        "<div class='box cv-box'><h2>Current/Voltage</h2><table><tr><th>CH</th><th>Value</th><th></th></tr>");

    // ADC1 ch0~5 : current + voltage
    for (int ch = 0; ch <= 5 && len < 32768 - 200; ch++) {
        bool on = ads131m08_get_channel_enable(1, ch);
        if (on) {
            char eng[24];
            bool is_cur = (ch <= 2);
            format_eng_rms(eng, sizeof(eng), rms1[ch], is_cur, ch);
            len += snprintf(buf + len, 32768 - len,
                "<tr><td>%s</td><td>%ld <small>(%s)</small></td>"
                "<td><form method='POST' action='/control' style='display:inline;margin:0;'>"
                "<input type='hidden' name='action' value='calib_ch'>"
                "<input type='hidden' name='adc' value='1'>"
                "<input type='hidden' name='ch' value='%d'>"
                "<button type='submit'>Cal</button></form></td></tr>",
                ads131m08_ch_name_adc1(ch), adc1[ch], eng, ch);
        } else {
            len += snprintf(buf + len, 32768 - len,
                "<tr class='off-row'><td>%s</td><td>-</td><td></td></tr>",
                ads131m08_ch_name_adc1(ch));
        }
    }
    len += snprintf(buf + len, 32768 - len, "</table></div>"
        "<div class='box common-box'><h2>Common</h2><table><tr><th>CH</th><th>Value</th><th></th></tr>");

    // ADC1 ch6~7 : common1~2
    for (int ch = 6; ch <= 7 && len < 32768 - 200; ch++) {
        bool on = ads131m08_get_channel_enable(1, ch);
        if (on) {
            // Determine current vs voltage from CUR_SEL
            pca9555_port_t cur_port = (pca9555_port_t)(PCA_CUR_SEL1 + (ch - 6));
            int cur_level = pca9555_get_pin(cur_port);
            bool is_cur = (cur_level == 1);
            char eng[24];
            format_eng_rms(eng, sizeof(eng), rms1[ch], is_cur, 8 + (ch - 6));
            len += snprintf(buf + len, 32768 - len,
                "<tr><td>%s</td><td>%ld <small>(%s)</small></td>"
                "<td><form method='POST' action='/control' style='display:inline;margin:0;'>"
                "<input type='hidden' name='action' value='calib_ch'>"
                "<input type='hidden' name='adc' value='1'>"
                "<input type='hidden' name='ch' value='%d'>"
                "<button type='submit'>Cal</button></form></td></tr>",
                ads131m08_ch_name_adc1(ch), adc1[ch], eng, ch);
        } else {
            len += snprintf(buf + len, 32768 - len,
                "<tr class='off-row'><td>%s</td><td>-</td><td></td></tr>",
                ads131m08_ch_name_adc1(ch));
        }
    }
    // ADC2 ch0~3 : common3~6
    for (int ch = 0; ch <= 3 && len < 32768 - 200; ch++) {
        bool on = ads131m08_get_channel_enable(2, ch);
        if (on) {
            pca9555_port_t cur_port = (pca9555_port_t)(PCA_CUR_SEL1 + 2 + ch);
            int cur_level = pca9555_get_pin(cur_port);
            bool is_cur = (cur_level == 1);
            char eng[24];
            format_eng_rms(eng, sizeof(eng), rms2[ch], is_cur, 10 + ch);
            len += snprintf(buf + len, 32768 - len,
                "<tr><td>%s</td><td>%ld <small>(%s)</small></td>"
                "<td><form method='POST' action='/control' style='display:inline;margin:0;'>"
                "<input type='hidden' name='action' value='calib_ch'>"
                "<input type='hidden' name='adc' value='2'>"
                "<input type='hidden' name='ch' value='%d'>"
                "<button type='submit'>Cal</button></form></td></tr>",
                ads131m08_ch_name_adc2(ch), adc2[ch], eng, ch);
        } else {
            len += snprintf(buf + len, 32768 - len,
                "<tr class='off-row'><td>%s</td><td>-</td><td></td></tr>",
                ads131m08_ch_name_adc2(ch));
        }
    }
    len += snprintf(buf + len, 32768 - len,
        "</table></div></div>"
        "<div class='box'><h2>3-phase (100 ms)</h2>"
        "<table><tr><th>Ph</th><th>Vrms</th><th>Irms</th><th>kW</th><th>PF</th></tr>"
        "<tr><td>1</td><td>%.2f</td><td>%.3f</td><td>%.3f</td><td>%.3f</td></tr>"
        "<tr><td>2</td><td>%.2f</td><td>%.3f</td><td>%.3f</td><td>%.3f</td></tr>"
        "<tr><td>3</td><td>%.2f</td><td>%.3f</td><td>%.3f</td><td>%.3f</td></tr>"
        "</table><small>%lu sps</small></div>"
        "<p><small>ESP32-S3 RTU — Control panel mirrors console commands</small></p></body></html>",
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
    config.stack_size = 32768;
    config.max_uri_handlers = 8;
    config.max_req_hdr_len = 1024;   // increase to avoid 431
    config.max_uri_len = 512;

    if (httpd_start(&web_server, &config) == ESP_OK) {
        httpd_uri_t uri_get = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(web_server, &uri_get);

        httpd_uri_t uri_post = {
            .uri = "/control",
            .method = HTTP_POST,
            .handler = control_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(web_server, &uri_post);

        ESP_LOGI(TAG, "Web server started on port 80 (with control GUI)");
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
