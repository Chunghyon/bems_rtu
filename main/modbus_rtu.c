#include "project.h"
#include "modbus_rtu.h"

#include <string.h>
#include <errno.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

static const char *TAG = "modbus";

static uint16_t regs[MODBUS_REG_COUNT];
static uart_port_t s_slave_uart;
static int s_slave_de = -1;
static uart_port_t s_master_uart;
static int s_master_de = -1;
static modbus_meas_fn s_meas_fn;
static uint16_t s_poll_ok;
static uint16_t s_poll_fail;

/* ---------- CRC ---------- */
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
        put_i32(36 + p * 2, (int32_t)(m.kvar[p] * 1000.0f));
        put_i32(42 + p * 2, (int32_t)(m.kva[p] * 1000.0f));
        regs[48 + p] = (uint16_t)(int16_t)(m.pf[p] * 1000.0f);
        put_u32(60 + p * 2, (uint32_t)(m.kwh[p] * 1000.0f + 0.5f));
    }
    regs[70] = (uint16_t)(m.freq_hz * 100.0f + 0.5f);
    for (int i = 0; i < 4; i++) put_u32(80 + i * 2, m.pulse[i]);
    for (int i = 0; i < 8; i++) {
        put_i32(100 + i * 2, m.adc1[i]);
        put_i32(120 + i * 2, m.adc2[i]);
    }
    regs[200] = s_poll_ok;
    regs[201] = s_poll_fail;
}

/* ---------- PDU handler (shared by RTU slave and TCP) ---------- */
/* Returns response PDU length, or 0 to ignore. */
static int handle_pdu(const uint8_t *pdu, int pdu_len, uint8_t *out)
{
    if (pdu_len < 1) return 0;
    uint8_t fc = pdu[0];
    refresh_regs();

    /* FC03 Read Holding / FC04 Read Input */
    if (fc == 0x03 || fc == 0x04) {
        if (pdu_len < 5) return 0;
        uint16_t addr = (pdu[1] << 8) | pdu[2];
        uint16_t qty  = (pdu[3] << 8) | pdu[4];
        if (qty == 0 || qty > 125 || addr + qty > MODBUS_REG_COUNT) {
            out[0] = fc | 0x80;
            out[1] = 0x02;   /* Illegal Data Address */
            return 2;
        }
        out[0] = fc;
        out[1] = (uint8_t)(qty * 2);
        for (int i = 0; i < qty; i++) {
            out[2 + i * 2] = regs[addr + i] >> 8;
            out[3 + i * 2] = regs[addr + i] & 0xFF;
        }
        return 2 + qty * 2;
    }

    /* FC06 Write Single Register */
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

    /* FC16 Write Multiple Registers */
    if (fc == 0x10) {
        if (pdu_len < 6) return 0;
        uint16_t addr = (pdu[1] << 8) | pdu[2];
        uint16_t qty  = (pdu[3] << 8) | pdu[4];
        uint8_t  bytes = pdu[5];
        if (qty == 0 || qty > 123 || bytes != qty * 2 ||
            addr + qty > MODBUS_REG_COUNT || pdu_len < 6 + bytes) {
            out[0] = fc | 0x80;
            out[1] = 0x02;
            return 2;
        }
        for (int i = 0; i < qty; i++) {
            regs[addr + i] = (pdu[6 + i * 2] << 8) | pdu[7 + i * 2];
        }
        out[0] = fc;
        out[1] = pdu[1]; out[2] = pdu[2];
        out[3] = pdu[3]; out[4] = pdu[4];
        return 5;
    }

    /* Unsupported function */
    out[0] = fc | 0x80;
    out[1] = 0x01;   /* Illegal Function */
    return 2;
}

/* ---------- RTU helpers ---------- */
static void rtu_tx(uart_port_t uart, int de, const uint8_t *frame, int len)
{
    if (de >= 0) gpio_set_level(de, 1);
    uart_write_bytes(uart, frame, len);
    uart_wait_tx_done(uart, pdMS_TO_TICKS(100));
    /* short turnaround before releasing DE */
    vTaskDelay(pdMS_TO_TICKS(2));
    if (de >= 0) gpio_set_level(de, 0);
}

#if PP_ENABLE_MODBUS_SLAVE
/* ---------- UART1: Modbus RTU Slave ---------- */
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
        /* silence → frame complete */
        if (len < 4) { len = 0; continue; }

        uint16_t crc = mb_crc(buf, len - 2);
        uint16_t rx  = buf[len - 2] | (buf[len - 1] << 8);
        if (crc != rx) {
            len = 0;
            continue;
        }
        /* Accept our ID only (broadcast 0 is ignored for reply) */
        if (buf[0] != MODBUS_SLAVE_ID) {
            len = 0;
            continue;
        }

        uint8_t pdu_out[200];
        int plen = handle_pdu(buf + 1, len - 3, pdu_out);
        if (plen > 0) {
            uint8_t frame[256];
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
#endif

#if PP_ENABLE_MODBUS_TCP_S
/* ---------- Modbus TCP Slave (port 502) ---------- */
/* Read exactly n bytes with timeout. Returns n on success, <0 on error. */
static int recv_full(int fd, uint8_t *buf, int n, int timeout_ms)
{
    int got = 0;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (got < n) {
        int remain = pdTICKS_TO_MS(deadline - xTaskGetTickCount());
        if (remain <= 0) return -1;
        struct timeval tv = { .tv_sec = remain / 1000, .tv_usec = (remain % 1000) * 1000 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        int r = recv(fd, buf + got, n - got, 0);
        if (r <= 0) return -1;
        got += r;
    }
    return got;
}

static void task_modbus_tcp(void *arg)
{
    int listen_fd = -1;
    for (int retry = 0; retry < 30 && listen_fd < 0; retry++) {
        listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd < 0) vTaskDelay(pdMS_TO_TICKS(100));
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
    ESP_LOGI(TAG, "Modbus TCP slave listening on port %d", MODBUS_TCP_PORT);

    while (1) {
        struct sockaddr_in cli;
        socklen_t clilen = sizeof(cli);
        int fd = accept(listen_fd, (struct sockaddr *)&cli, &clilen);
        if (fd < 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        ESP_LOGI(TAG, "Modbus TCP client connected");

        /* Per-client session */
        while (1) {
            uint8_t mbap[7];
            if (recv_full(fd, mbap, 7, 5000) != 7) break;

            uint16_t proto = (mbap[2] << 8) | mbap[3];
            uint16_t length = (mbap[4] << 8) | mbap[5];
            uint8_t  unit = mbap[6];

            /* Protocol must be 0, length = unit + PDU (2..253) */
            if (proto != 0 || length < 2 || length > 253) break;

            uint8_t pdu[253];
            int pdu_len = length - 1;   /* exclude unit id */
            if (recv_full(fd, pdu, pdu_len, 2000) != pdu_len) break;

            /* Unit ID: accept our ID, 0xFF (direct), or 0 (gateway) */
            if (unit != MODBUS_SLAVE_ID && unit != 0xFF && unit != 0) {
                /* ignore, but still need to consume (already did) */
                continue;
            }

            uint8_t pdu_out[256];
            int plen = handle_pdu(pdu, pdu_len, pdu_out);
            if (plen <= 0) continue;

            uint8_t resp[264];
            /* Echo transaction ID */
            resp[0] = mbap[0];
            resp[1] = mbap[1];
            /* Protocol = 0 */
            resp[2] = 0;
            resp[3] = 0;
            /* Length = unit + PDU */
            uint16_t rlen = plen + 1;
            resp[4] = (uint8_t)(rlen >> 8);
            resp[5] = (uint8_t)(rlen & 0xFF);
            /* Unit ID echo */
            resp[6] = unit;
            memcpy(resp + 7, pdu_out, plen);

            if (send(fd, resp, 7 + plen, 0) < 0) break;
        }

        close(fd);
        ESP_LOGI(TAG, "Modbus TCP client closed");
    }
}
#endif

#if PP_ENABLE_MODBUS_MASTER
/* ---------- UART2: Modbus RTU Master ---------- */
/* Poll one or more downstream devices. Results stored starting at reg 202. */

typedef struct {
    uint8_t  slave;
    uint16_t start;
    uint16_t qty;
    uint16_t dest_reg;   /* where to store results in our map */
} master_poll_t;

/* Default poll list: one meter at slave 1, regs 0-9 → our regs 202-211 */
static const master_poll_t s_polls[] = {
    { 1, 0, 10, 202 },
    /* Add more entries here if needed, e.g. {2, 0, 10, 220} */
};
#define MASTER_POLL_COUNT  (sizeof(s_polls) / sizeof(s_polls[0]))
#define MASTER_POLL_INTERVAL_MS  2000

static int master_transaction(uint8_t slave, uint16_t start, uint16_t qty,
                              uint16_t *out_regs, int max_regs)
{
    /* Build request: addr + FC03 + start + qty + CRC */
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

    /* Expected response: slave + FC + bytecount + data + CRC
       bytecount = qty*2, total = 5 + qty*2 */
    int expect = 5 + qty * 2;
    if (expect > 256) expect = 256;

    uint8_t rx[256];
    int n = uart_read_bytes(s_master_uart, rx, expect, pdMS_TO_TICKS(300));
    if (n < 5) return -1;

    /* Basic framing check */
    if (rx[0] != slave) return -1;
    if (rx[1] == (0x03 | 0x80)) {
        /* Exception response */
        return -2;
    }
    if (rx[1] != 0x03) return -1;

    int nbytes = rx[2];
    if (n < 5 + nbytes) return -1;

    uint16_t crc = mb_crc(rx, n - 2);
    uint16_t got = rx[n - 2] | (rx[n - 1] << 8);
    if (crc != got) return -1;

    int regs_n = nbytes / 2;
    if (regs_n > max_regs) regs_n = max_regs;
    for (int i = 0; i < regs_n; i++) {
        out_regs[i] = (rx[3 + i * 2] << 8) | rx[4 + i * 2];
    }
    return regs_n;
}

static void task_modbus_master(void *arg)
{
    ESP_LOGI(TAG, "RTU master on UART%d, %d poll(s) every %d ms",
             s_master_uart, MASTER_POLL_COUNT, MASTER_POLL_INTERVAL_MS);

    while (1) {
        for (int p = 0; p < MASTER_POLL_COUNT; p++) {
            const master_poll_t *item = &s_polls[p];
            uint16_t tmp[64];
            int n = master_transaction(item->slave, item->start, item->qty,
                                       tmp, item->qty);
            if (n > 0) {
                for (int i = 0; i < n && (item->dest_reg + i) < MODBUS_REG_COUNT; i++) {
                    regs[item->dest_reg + i] = tmp[i];
                }
                if (s_poll_ok < 0xFFFF) s_poll_ok++;
                ESP_LOGD(TAG, "Master poll slave=%d start=%d qty=%d OK (%d regs)",
                         item->slave, item->start, item->qty, n);
            } else {
                if (s_poll_fail < 0xFFFF) s_poll_fail++;
                ESP_LOGW(TAG, "Master poll slave=%d start=%d failed (code=%d)",
                         item->slave, item->start, n);
            }
            /* Small gap between polls if multiple */
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        vTaskDelay(pdMS_TO_TICKS(MASTER_POLL_INTERVAL_MS));
    }
}
#endif

/* ---------- Public start ---------- */
void modbus_start(uart_port_t slave_uart, int slave_de_pin,
                  uart_port_t master_uart, int master_de_pin,
                  modbus_meas_fn meas_fn)
{
    s_slave_uart = slave_uart;
    s_slave_de = slave_de_pin;
    s_master_uart = master_uart;
    s_master_de = master_de_pin;
    s_meas_fn = meas_fn;

#if PP_ENABLE_MODBUS_TCP_S
    xTaskCreate(task_modbus_tcp, "mb_tcp", 6144, NULL, 5, NULL);
#endif
#if PP_ENABLE_MODBUS_SLAVE
    xTaskCreate(task_modbus_slave, "mb_slave", 4096, NULL, 6, NULL);
#endif
#if PP_ENABLE_MODBUS_MASTER
     xTaskCreate(task_modbus_master, "mb_master", 4096, NULL, 4, NULL);
#endif
}
