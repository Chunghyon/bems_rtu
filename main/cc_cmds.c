#include "stdbool.h"
#include "stdlib.h"
#include "string.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"

#include "esp_wifi.h"
#include "esp_eth.h"

#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/netdb.h"
#include "lwip/ip.h"

#include "driver/uart.h"

#include "hh_parameters.h"
#include "hh_util.h"
#include "hh_tcp_packet.h"
#include "hh_cmds.h"
#include "hh_nvs.h"

extern void return_to_initial_status(void);

extern esp_log_level_t esp_log_level;
extern esp_netif_ip_info_t self_eth_ip;
#if !PP_IP_BY_PROVISION_20220713
extern cc_server_config_t default_config;
#endif
extern bool gwid_inform_requested;

/**
 * Erase back-up flash
 */
void cmd_erase_bkup_flash(void)
{

}

/**
 * Command to set self static IP address
 */
void cmd_set_self_eth_ip(uint32_t ip, uint32_t netmask, uint32_t gw)
{
	if(ip)		self_eth_ip.ip.addr		 = ip;
	if(netmask)	self_eth_ip.netmask.addr = netmask;
	if(gw)		self_eth_ip.gw.addr		 = gw;
}

/**
 * set current time
 */
void cmd_set_current_time(time_t epoch_time)
{
	struct timeval nowTime;

	nowTime.tv_sec = epoch_time;
	settimeofday(&nowTime, NULL);
}

/**
 * Console menu to set current time
 */
void menu_set_current_time(void)
{
	char* current_time = NULL;
	int32_t keyin_integer;
	struct tm t;
	time_t t_of_day;

	current_time = get_current_time();

	CC_LOGNI("\n********** Set Date and Time Menu ***********\n");
	CC_LOGNI("**** Current data & time : \033[1;32m%s\033[1;37m)\n", current_time);

	while(1)	//Set year
	{
		CC_LOGNI("?? Year?  (yyyy) : ");
		keyin_integer = GetPositiveInteger();
		if(keyin_integer<0)
		{
			CC_LOGNI("\n");
			CC_LOGNI("!! Cancelled. Return to upper menu...\n\n");
			return;
		}
		if(keyin_integer>2038 || keyin_integer == 0)
		{
			CC_LOGNI("\n");
			CC_LOGNI("!! Wrong input. Input again.\n\n");
			continue;
		}
		t.tm_year = keyin_integer - 1900;
		break;
	}

	while(1)	//Set month
	{
		CC_LOGNI("?? Month? (1~12) : ");
		keyin_integer = GetPositiveInteger();
		if(keyin_integer<=0)
		{
			CC_LOGNI("\n");
			CC_LOGNI("!! Cancelled. Return to upper menu...\n\n");
			return;
		}
		if(keyin_integer>12 || keyin_integer == 0)
		{
			CC_LOGNI("\n");
			CC_LOGNI("!! Wrong input. Input again.\n\n");
			continue;
		}
		t.tm_mon = keyin_integer - 1;
		break;
	}

	while(1)	//Set day
	{
		CC_LOGNI("?? Date?  (1~31) : ");
		keyin_integer = GetPositiveInteger();
		if(keyin_integer<0)
		{
			CC_LOGNI("\n");
			CC_LOGNI("!! Cancelled. Return to upper menu...\n\n");
			return;
		}
		if(keyin_integer>31 || keyin_integer == 0)
		{
			CC_LOGNI("\n");
			CC_LOGNI("!! Wrong input. Input again.\n\n");
			continue;
		}
		t.tm_mday = keyin_integer;
		break;
	}

	while(1)	//Set hour
	{
		CC_LOGNI("?? Hour?  (0~23) : ");
		keyin_integer = GetPositiveInteger();
		if(keyin_integer<=0)
		{
			CC_LOGNI("\n");
			CC_LOGNI("!! Cancelled. Return to upper menu...\n\n");
			return;
		}
		if(keyin_integer>31)
		{
			CC_LOGNI("\n");
			CC_LOGNI("!! Wrong input. Input again.\n\n");
			continue;
		}
		t.tm_hour = keyin_integer;
		break;
	}

	while(1)	//Set minute
	{
		CC_LOGNI("?? Minute?(0~60) : ");
		keyin_integer = GetPositiveInteger();
		if(keyin_integer<0)
		{
			CC_LOGNI("\n");
			CC_LOGNI("!! Cancelled. Return to upper menu...\n\n");
			return;
		}
		if(keyin_integer>60)
		{
			CC_LOGNI("\n");
			CC_LOGNI("!! Wrong input. Input again.\n\n");
			continue;
		}
		t.tm_min = keyin_integer;
		break;
	}

	while(1)	//Set second
	{
		CC_LOGNI("?? second?(0~60) : ");
		keyin_integer = GetPositiveInteger();
		if(keyin_integer<0)
		{
			CC_LOGNI("\n");
			CC_LOGNI("!! Cancelled. Return to upper menu...\n\n");
			return;
		}
		if(keyin_integer>60)
		{
			CC_LOGNI("\n");
			CC_LOGNI("!! Wrong input. Input again.\n\n");
			continue;
		}
		t.tm_sec = keyin_integer;
		break;
	}

	t_of_day = mktime(&t);

	cmd_set_current_time(t_of_day);
	{
		time_t now = 0;
		struct tm timeinfo = {0,};
		char time_buffer[100]={0,};

		time(&now);
		localtime_r(&now, &timeinfo);
		strftime(time_buffer, sizeof(time_buffer), "%Y/%m/%d %H:%M:%S", &timeinfo);
		CC_LOGD("Updated Current time : \033[1;32m%s\033[1;37m\n\n", time_buffer);
	}
}

/**
 * log on/off
 * @param[in]	onoff	true to turn-ON, false to turn-OFF
 * @return		none
 */
void cmd_debug_log_onoff(bool onoff)
{
	debug_log_on = onoff;
}

/**
 * Console menu to turn on/off log
 */
void menu_debug_log_onoff(void)
{
	if(debug_log_on) CC_LOGNI("!! Debug Log is OFF now.\n");
	cmd_debug_log_onoff(!debug_log_on);
	if(debug_log_on) CC_LOGNI("!! Debug Log is ON now.\n");
}

void cmd_change_esplog_level(esp_log_level_t level)
{
	esp_log_level = level;
	esp_log_level_set("*", esp_log_level);
}

void menu_change_esplog_level(void)
{
	uint8_t key_in;
	uint8_t org_level = esp_log_level;

	CC_LOGNI("?? Select ESP_LOG_LEVEL, Debug(\033[1;33m0\033[1;37m), Info(\033[1;33m1\033[1;37m), "
			"Warning(\033[1;33m2\033[1;37m), Error(\033[1;33m3\033[1;37m), None(\033[1;33m4\033[1;37m) : ");
	key_in = GetChar(0);
	if(ESC == key_in)
	{
		CC_LOGNI("\n");
		CC_LOGNI("!! Cancelled...\n");
		return;
	}
	CC_LOGNI("%c\n", key_in);
	if(key_in>'4' || key_in<'0')
	{
		CC_LOGNI("!! Wrong number. Return to upper menu...\n\n");
		return;
	}
	key_in = key_in - '0';

	cmd_change_esplog_level((esp_log_level_t)key_in);

	if		(org_level == 0) CC_LOGNI("ESP_ORG_LEVEL = DEBUG --> ");
	else if (org_level == 1) CC_LOGNI("ESP_ORG_LEVEL = INFO --> ");
	else if (org_level == 2) CC_LOGNI("ESP_ORG_LEVEL = WARNING --> ");
	else if (org_level == 3) CC_LOGNI("ESP_ORG_LEVEL = ERROR --> ");
	else if (org_level == 4) CC_LOGNI("ESP_ORG_LEVEL = NONE --> ");

	if		(esp_log_level == 0) CC_LOGNI("ESP_NEW_LEVEL = DEBUG\n");
	else if (esp_log_level == 1) CC_LOGNI("ESP_NEW_LEVEL = INFO\n");
	else if (esp_log_level == 2) CC_LOGNI("ESP_NEW_LEVEL = WARNING\n");
	else if (esp_log_level == 3) CC_LOGNI("ESP_NEW_LEVEL = ERROR\n");
	else if (esp_log_level == 4) CC_LOGNI("ESP_NEW_LEVEL = NONE\n");

}
