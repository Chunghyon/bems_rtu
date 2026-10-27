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
 * @brief Full recovery: RST pin pulse + uninstall/reinstall Ethernet driver.
 *        Use this when the driver internal state is inconsistent.
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
