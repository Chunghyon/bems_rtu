#include "modbus_rtu.h"
#include "rtu_pins.h"

#include <string.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "lwip/sockets.h"

static const char *TAG = "modbus";

static uint16_t regs[MODBUS_REG_COUNT];
static uart_port_t s_slave_uart;
static int s_slave_de = -1;
static uart_port_t s_master_uart;
static int s_master_de = -1;
static modbus_meas_fn s_meas_fn;
static uint16_t s_poll_ok;
static uint16_t s_poll_fail;

static uint16_t mb_crc(const uint8_t *buf, int len)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
        }
    }
    return crc;
}

static void put_u32(int addr, uint32_t v)
{
    if (addr < 0 || addr + 1 >= MODBUS_REG_COUNT) return;
    regs[addr]     = (uint16_t)(v >> 16);
    regs[addr + 1] = (uint16_t)(v & 0xFFFF);
}

static void put_i32(int addr, int32_t v)
{
    put_u32(addr, (uint32_t)v);
}

static void refresh_regs(void)
{
    modbus_meas_t m = {0};
    if (s_meas_fn) s_meas_fn(&m);

    regs[0] = 1;
    regs[1] = MODBUS_SLAVE_ID;
    regs[2] = 1;

    for (int p = 0; p < 3; p++) {
        put_u32(10 + p * 2, (uint32_t)(m.vrms[p] * 100.0f + 0.5f));
        put_u32(20 + p * 2, (uint32_t)(m.irms[p] * 1000.0f + 0.5f));
        put_i32(30 + p * 2, (int32_t)(m.kw[p] * 1000.0f));
        regs[40 + p] = (uint16_t)(int16_t)(m.pf[p] * 1000.0f);
    }
    for (int i = 0; i < 4; i++) put_u32(50 + i * 2, m.pulse[i]);
    for (int i = 0; i < 8; i++) {
        put_i32(100 + i * 2, m.adc1[i]);
        put_i32(120 + i * 2, m.adc2[i]);
    }
    regs[200] = s_poll_ok;
    regs[201] = s_poll_fail;
}

/* Build a response PDU into out[]. Returns length, or 0 on ignore. */
static int handle_pdu(const uint8_t *pdu, int pdu_len, uint8_t *out)
{
    if (pdu_len < 1) return 0;
    uint8_t fc = pdu[0];
    refresh_regs();

    if (fc == 0x03 || fc == 0x04) {
        if (pdu_len < 5) return 0;
        uint16_t addr = (pdu[1] << 8) | pdu[2];
        uint16_t qty  = (pdu[3] << 8) | pdu[4];
        if (qty == 0 || qty > 64 || addr + qty > MODBUS_REG_COUNT) {
            out[0] = fc | 0x80;
            out[1] = 0x02;
            return 2;
        }
        out[0] = fc;
        out[1] = (uint8_t)(qty * 2);
        for (int i = 0; i < qty; i++) {
            out[2 + i * 2]     = regs[addr + i] >> 8;
            out[3 + i * 2] = regs[addr + i] & 0xFF;
        }
        return 2 + qty * 2;
    }

    if (fc == 0x06) {
        if (pdu_len < 5) return 0;
        uint16_t addr = (pdu[1] << 8) | pdu[2];
        uint16_t val  = (pdu[3] << 8) | pdu[4];
        if (addr >= MODBUS_REG_COUNT) {
            out[0] = fc | 0x80;
            out[1] = 0x02;
            return 2;
        }
        regs[addr] = val;
        memcpy(out, pdu, 5);
        return 5;
    }

    out[0] = fc | 0x80;
    out[1] = 0x01;
    return 2;
}

static void rtu_tx(uart_port_t uart, int de, const uint8_t *frame, int len)
{
    if (de >= 0) gpio_set_level(de, 1);
    uart_write_bytes(uart, frame, len);
    uart_wait_tx_done(uart, pdMS_TO_TICKS(100));
    if (de >= 0) gpio_set_level(de, 0);
}

static void task_modbus_slave(void *arg)
{
    uint8_t buf[256];
    int len = 0;
    ESP_LOGI(TAG, "RTU slave id=%d uart=%d baud=%d", MODBUS_SLAVE_ID, s_slave_uart, MODBUS_BAUD);

    while (1) {
        uint8_t b;
        int n = uart_read_bytes(s_slave_uart, &b, 1, pdMS_TO_TICKS(20));
        if (n == 1) {
            if (len < (int)sizeof(buf)) buf[len++] = b;
            continue;
        }
        if (len < 4) { len = 0; continue; }

        uint16_t crc = mb_crc(buf, len - 2);
        uint16_t rx  = buf[len - 2] | (buf[len - 1] << 8);
        if (crc != rx || (buf[0] != MODBUS_SLAVE_ID && buf[0] != 0)) {
            len = 0;
            continue;
        }
        if (buf[0] == 0) { len = 0; continue; }   // broadcast: no reply

        uint8_t pdu_out[128];
        int plen = handle_pdu(buf + 1, len - 3, pdu_out);
        if (plen > 0) {
            uint8_t frame[160];
            frame[0] = MODBUS_SLAVE_ID;
            memcpy(frame + 1, pdu_out, plen);
            uint16_t c = mb_crc(frame, 1 + plen);
            frame[1 + plen]     = c & 0xFF;
            frame[2 + plen] = c >> 8;
            rtu_tx(s_slave_uart, s_slave_de, frame, 3 + plen);
        }
        len = 0;
    }
}

#if PP_ENABLE_MODBUS_TCP
static void task_modbus_tcp(void *arg)
{
    int listen_fd = -1;
    // TCP/IP 스택이 완전히 준비될 때까지 재시도
    for (int retry = 0; retry < 30 && listen_fd < 0; retry++) {
        listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd < 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    if (listen_fd < 0) {
        ESP_LOGE(TAG, "TCP socket failed");
        vTaskDelete(NULL);
        return;
    }
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(MODBUS_TCP_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(listen_fd, 2) != 0) {
        ESP_LOGE(TAG, "TCP bind/listen %d failed errno=%d", MODBUS_TCP_PORT, errno);
        close(listen_fd);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Modbus TCP listening on %d", MODBUS_TCP_PORT);

    while (1) {
        int fd = accept(listen_fd, NULL, NULL);
        if (fd < 0) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        ESP_LOGI(TAG, "Modbus TCP client connected");
        uint8_t req[260];
        while (1) {
            int n = recv(fd, req, 7, MSG_WAITALL);
            if (n != 7) break;
            uint16_t mb_len = (req[4] << 8) | req[5];
            if (mb_len < 2 || mb_len > 250) break;
            int got = 0;
            while (got < mb_len - 1) {
                int r = recv(fd, req + 7 + got, (mb_len - 1) - got, 0);
                if (r <= 0) { got = -1; break; }
                got += r;
            }
            if (got < 0) break;
            uint8_t pdu_out[200];
            int plen = handle_pdu(req + 7, mb_len - 1, pdu_out);
            if (plen <= 0) continue;
            uint8_t resp[220];
            resp[0] = req[0]; resp[1] = req[1];
            resp[2] = 0; resp[3] = 0;
            resp[4] = (uint8_t)((plen + 1) >> 8);
            resp[5] = (uint8_t)((plen + 1) & 0xFF);
            resp[6] = req[6];
            memcpy(resp + 7, pdu_out, plen);
            if (send(fd, resp, 7 + plen, 0) < 0) break;
        }
        close(fd);
        ESP_LOGI(TAG, "Modbus TCP client closed");
    }
}
#endif

/* Port 2: poll one downstream meter (report: Modbus polling 0.5~10 s). */
static void task_modbus_master(void *arg)
{
    const uint8_t slave = 1;
    const uint16_t start = 0;
    const uint16_t qty = 10;
    ESP_LOGI(TAG, "RTU master polling uart=%d slave=%d every 2s", s_master_uart, slave);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        uint8_t req[8] = {
            slave, 0x03,
            (uint8_t)(start >> 8), (uint8_t)start,
            (uint8_t)(qty >> 8), (uint8_t)qty,
        };
        uint16_t c = mb_crc(req, 6);
        req[6] = c & 0xFF;
        req[7] = c >> 8;

        uart_flush_input(s_master_uart);
        rtu_tx(s_master_uart, s_master_de, req, 8);

        uint8_t rx[64];
        int n = uart_read_bytes(s_master_uart, rx, sizeof(rx), pdMS_TO_TICKS(200));
        if (n >= 5 && rx[0] == slave && rx[1] == 0x03) {
            uint16_t crc = mb_crc(rx, n - 2);
            uint16_t got = rx[n - 2] | (rx[n - 1] << 8);
            if (crc == got) {
                int nbytes = rx[2];
                int regs_n = nbytes / 2;
                if (regs_n > 10) regs_n = 10;
                for (int i = 0; i < regs_n; i++) {
                    regs[202 + i] = (rx[3 + i * 2] << 8) | rx[4 + i * 2];
                }
                if (s_poll_ok < 0xFFFF) s_poll_ok++;
                continue;
            }
        }
        if (s_poll_fail < 0xFFFF) s_poll_fail++;
    }
}

void modbus_start(uart_port_t slave_uart, int slave_de_pin,
                  uart_port_t master_uart, int master_de_pin,
                  modbus_meas_fn meas_fn)
{
    s_slave_uart = slave_uart;
    s_slave_de = slave_de_pin;
    s_master_uart = master_uart;
    s_master_de = master_de_pin;
    s_meas_fn = meas_fn;

    xTaskCreate(task_modbus_slave, "mb_slave", 4096, NULL, 6, NULL);
#if PP_ENABLE_MODBUS_TCP
    xTaskCreate(task_modbus_tcp, "mb_tcp", 4096, NULL, 5, NULL);
#endif
    xTaskCreate(task_modbus_master, "mb_master", 4096, NULL, 4, NULL);
}
