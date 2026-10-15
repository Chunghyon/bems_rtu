#ifndef HH_COMMANDS_H__
#define HH_COMMANDS_H__

#include "esp_log.h"
#include "HH_parameters.h"

extern void menu_set_current_time(void);
extern void menu_debug_log_onoff(void);
extern void menu_change_esplog_level(void);

extern void cmd_debug_log_onoff(bool onoff);
extern void cmd_change_esplog_level(esp_log_level_t level);
extern void cmd_erase_bkup_flash(void);
extern void cmd_set_self_eth_ip(uint32_t ip, uint32_t netmask, uint32_t gw);

#endif

