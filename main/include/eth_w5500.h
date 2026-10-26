/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#pragma once

#include "esp_eth_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize Ethernet driver based on Espressif IoT Development Framework Configuration
 *
 * @param[out] eth_handles_out array of initialized Ethernet driver handles
 * @param[out] eth_cnt_out number of initialized Ethernets
 * @return
 *          - ESP_OK on success
 *          - ESP_ERR_INVALID_ARG when passed invalid pointers
 *          - ESP_ERR_NO_MEM when there is no memory to allocate for Ethernet driver handles array
 *          - ESP_FAIL on any other failure
 */
esp_err_t w5500_init(esp_eth_handle_t *eth_handle_out);

/**
 * @brief Read a W5500 common register via SPI (for debug).
 *        Safe to call from console when investigating link issues.
 * @param addr  Register offset (e.g. 0x0039 = VERSIONR, 0x002E = PHYCFGR)
 * @param val   Output byte
 * @return ESP_OK on success
 */
esp_err_t w5500_read_reg(uint16_t addr, uint8_t *val);

/**
 * @brief Read a W5500 register with the Ethernet driver stopped.
 *        Stops the driver, reads, then starts again.
 *        *val is valid when return is ESP_OK (even if restart failed).
 *        If start_ok is non-NULL, it is set false when esp_eth_start fails
 *        (caller should full-recover in that case).
 */
esp_err_t w5500_read_reg_exclusive(esp_eth_handle_t eth_handle, uint16_t addr,
                                   uint8_t *val, bool *start_ok);

/**
 * @brief Stop the driver, dump W5500 interrupt cause (IR / SIR / Sn_IR), restart.
 *        Prints a decoded log. *start_ok false means caller should full-recover.
 */
esp_err_t w5500_dump_int_cause(esp_eth_handle_t eth_handle, bool *start_ok);

/**
 * @brief Write a W5500 common register via SPI (for debug).
 */
esp_err_t w5500_write_reg(uint16_t addr, uint8_t val);

/**
 * @brief Software-reset the W5500 (MR.RST=1) then re-start the Ethernet driver.
 *        Stops the driver, issues soft reset, waits, then calls esp_eth_start()
 *        so MAC/PHY/sockets are re-initialized.
 * @param eth_handle  Active Ethernet handle (must not be NULL)
 * @return ESP_OK on success
 */
esp_err_t w5500_soft_reset(esp_eth_handle_t eth_handle);

/**
 * @brief Full recovery: RST pin pulse + uninstall/reinstall Ethernet driver.
 *        Use this when the driver internal state is inconsistent
 *        (e.g. after a failed soft-reset start).
 *        On success, *eth_handle_io is replaced with the new handle.
 *        Caller must re-attach netif glue and call esp_eth_start().
 * @param eth_handle_io  in/out Ethernet handle
 * @return ESP_OK on success
 */
esp_err_t w5500_reinstall_driver(esp_eth_handle_t *eth_handle_io);

/**
 * @brief Hardware-reset the W5500 via RST pin then re-start the Ethernet driver.
 *        More reliable recovery than soft reset when PHY state is corrupted.
 * @param eth_handle  Active Ethernet handle (must not be NULL)
 * @return ESP_OK on success
 */
esp_err_t w5500_hard_reset_and_start(esp_eth_handle_t eth_handle);

/**
 * @brief De-initialize array of Ethernet drivers
 * @note All Ethernet drivers in the array must be stopped prior calling this function.
 *
 * @param[in] eth_handles array of Ethernet drivers to be de-initialized
 * @param[in] eth_cnt number of Ethernets drivers to be de-initialized
 * @return
 *          - ESP_OK on success
 *          - ESP_ERR_INVALID_ARG when passed invalid pointers
 */
esp_err_t w5500_eth_deinit(esp_eth_handle_t *eth_handle);

esp_err_t tcpip_stack_init(esp_eth_handle_t eth_handle);
#ifdef __cplusplus
}
#endif
