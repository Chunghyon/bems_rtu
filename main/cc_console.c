#include "stdbool.h"
#include "stdlib.h"
#include "string.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "esp_wifi.h"
#include "esp_eth.h"
#include "esp_system.h"

#include "lwip/sockets.h"
#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/netdb.h"
#include "lwip/ip.h"

#include "driver/uart.h"
#include "hh_parameters.h"
#include "hh_util.h"

#include "hh_spi.h"
#include "hh_cmds.h"
#include "hh_pkt_util.h"
#include "hh_cmds.h"
#include "hh_nvs.h"
#include "wm8960.h"
#include "prompts_uri.h"

static char *TAG = "console";

extern bool LEARNING_MODE;
extern bool under_face_learning;
extern void voice_record_start(void);
extern void voice_record_stop(void);

bool console_menu_is_running = false;

#ifdef PP_ONLY_FOR_DEMO
extern bool skip_sending_data_server_packet;
#endif

extern ip_thru_ble_t frcd_ip;
extern uint8_t speaker_volume;
extern tone_type_t tone_type_to_play;
extern int g_current_max_face;

#if 0
extern bool loopback_to_codec;
extern void play_tone(float freq, int duration_ms);
#endif
/**
 * Console main menu
 */
void DISPLAY_MAIN_MENU(void)
{
//	printf("*********************************************\n");
	printf("\n");
	fflush(stdout);
}

void print_loc_ip(void)
{
	printf("** IP Addr : \033[1;32m%3d.%3d.%3d.%3d / %u\033[1;37m\n", IP2STR((ip4_addr_t*)&frcd_ip.loc_ip[0]), frcd_ip.loc_ip[4]);
	printf("** GateWay : %3d.%3d.%3d.%3d\n", IP2STR((ip4_addr_t*)&frcd_ip.loc_gw[0]));
	printf("** PC Addr : %3d.%3d.%3d.%3d\n", IP2STR((ip4_addr_t*)&frcd_ip.rmt_ip[0]));
	fflush(stdout);
}

void ff_set_loc_ip(void)
{
	bool ip_is_changed = false;
	bool nm_is_changed = false;
	bool gw_is_changed = false;
	bool rt_is_changed = false;

	uint32_t * p_ip = (uint32_t*)&frcd_ip.loc_ip[0];
	uint32_t * p_gw = (uint32_t*)&frcd_ip.loc_gw[0];
	uint32_t nm_b = 0xFFFFFFFF << (32-frcd_ip.loc_ip[4]);	 // netmask big-end
	uint32_t nm_l = __builtin_bswap32(nm_b);
	uint32_t * p_rt = (uint32_t*)&frcd_ip.rmt_ip[0];

	CC_LOGNI("** Current Local IP info. **\n");
	print_loc_ip();
	
	char in_str[16];
	int result;
	uint32_t new_ip;

	CC_LOGNI("!! IP : ");
	result = GetString(in_str, 16);
	if(result>0) {
		new_ip = inet_addr(in_str);
		if(new_ip == INADDR_NONE) {
			CC_LOGNI("!! Wrong input. Cancelled.\n");
			return;
		}
		if(*p_ip != new_ip) {
			*p_ip = new_ip;
			ip_is_changed = true;
		}
	}
	else if(result==0) {
		/* keep current info...*/
	}
	else {
		CC_LOGNI("!! Cancelled.\n");
		return;
	}

	CC_LOGNI("!! NM : ");
	result = GetString(in_str, 16);
	if(result>0) {
		new_ip = inet_addr(in_str);
		if(new_ip == INADDR_NONE) {
			CC_LOGNI("!! Wrong input. Cancelled.\n");
			return;
		}
		if(nm_l != new_ip) {
			nm_l = new_ip;
			frcd_ip.loc_ip[4] = 32 - __builtin_clz(nm_l);
			CC_LOGNI("frcd_ip.loc_ip[4] %d\n", frcd_ip.loc_ip[4]);
			nm_is_changed = true;
		}
	}
	else if(result==0) {
		/* keep current info...*/
	}
	else {
		CC_LOGNI("!! Cancelled.\n");
		
		return;
	}

	CC_LOGNI("!! GW : ");
	
	result = GetString(in_str, 16);
	if(result>0) {
		new_ip = inet_addr(in_str);
		if(new_ip == INADDR_NONE) {
			CC_LOGNI("!! Wrong input. Cancelled.\n");
			
			return;
		}
		if(*p_gw != new_ip) {
			*p_gw = new_ip;
			gw_is_changed = true;
		}
	}
	else if(result==0) {
		/* keep current info...*/
	}
	else {
		CC_LOGNI("!! Cancelled.\n");
		
		return;
	}

	CC_LOGNI("!! PC : ");
	
	result = GetString(in_str, 16);
	if(result>0) {
		new_ip = inet_addr(in_str);
		if(new_ip == INADDR_NONE) {
			CC_LOGNI("!! Wrong input. Cancelled.\n");
			
			return;
		}
		if(*p_rt != new_ip) {
			*p_rt = new_ip;
			rt_is_changed = true;
		}
	}
	else if(result==0) {
		/* keep current info...*/
	}
	else {
		CC_LOGNI("!! Cancelled.\n");
		
		return;
	}

	if(ip_is_changed || nm_is_changed || gw_is_changed || rt_is_changed) {
		if(ip_is_changed) nvs_add(SEL_NVS0, NVS0_FRCD_LOC_IP);
		if(nm_is_changed) nvs_add(SEL_NVS0, NVS0_FRCD_LOC_NM);
		if(gw_is_changed) nvs_add(SEL_NVS0, NVS0_FRCD_LOC_GW);
		if(rt_is_changed) nvs_add(SEL_NVS0, NVS0_FRCD_RMT_IP);
		CC_LOGNI("** New Local IP info. **\n");
		
		print_loc_ip();

		CC_LOGNI("!! Auto reboot in 1 sec\n");

		vTaskDelay(pdMS_TO_TICKS(1000));
		esp_restart();
	}
	else {
		CC_LOGNI("!! IP is NOT changed.\n");
		
	}
}

void task_Menu(void *pvParameters)
{
	while (1)
	{
		uint8_t key;
		key = GetChar(0);
		switch(key)
		{
		case '/' :
			DISPLAY_MAIN_MENU();
			break;

		case 'i' :
		case 'p' :
		{
			ff_set_loc_ip();
			break;
		}

		case 'c' :
			LEARNING_MODE ^= 1;
			if(LEARNING_MODE) {
				printf("!! Learning Mode is \033[1;32mON\033[1;37m\n");
				request_spi_magic_to_master(SPI_MODE_LEARN);
			}
			else {
				printf("!! Learning Mode is \033[1;31mOFF\033[1;37m(Working Mode)\n");
				request_spi_magic_to_master(SPI_MODE_NORMAL);
			}
			break;
		case 's' :
			if(!LEARNING_MODE) {
				printf("!! Under Face Learning can be set only in Learning Mode.\n");
				break;
			}
			under_face_learning ^= 1;
			if(under_face_learning) {
				printf("!! Under Face Learning is \033[1;33mON\033[1;37m\n");
				request_spi_magic_to_master(SPI_LEARN_START);
			}
			else {
				printf("!! Under Face Learning is \033[1;31mOFF\033[1;37m\n");
				request_spi_magic_to_master(SPI_LEARN_STOP);
			}
			break;
		case 'q' :
#ifdef PP_ONLY_FOR_DEMO
			skip_sending_data_server_packet ^= 1;
			if(skip_sending_data_server_packet) {
				printf("!! Skip Sending Data to Server\n");
			}
			else {
				printf("!! Sending Data to Server \n");
			}
#endif
			break;
		case '<' :
			voice_record_start();
			break;
		case '>' :
			voice_record_stop();
			break;
		case 'a' :
		{
			static bool li_to_lo = false;
			li_to_lo = !li_to_lo;
			if(li_to_lo) {
				printf("!! Loopback(Boost Mixer to Line Out) \033[1;32mON\033[1;37m\n");
				wm8960_disableLD2LO();
				wm8960_disableRD2RO();
				wm8960_enableLB2LO();
				wm8960_enableRB2RO();
				wm8960_setLI2LOVOL(WM8960_OUTPUT_MIXER_GAIN_NEG_15DB);
				wm8960_setRI2ROVOL(WM8960_OUTPUT_MIXER_GAIN_NEG_15DB);
				wm8960_setLB2LOVOL(WM8960_OUTPUT_MIXER_GAIN_0DB);
				wm8960_setRB2ROVOL(WM8960_OUTPUT_MIXER_GAIN_0DB);
			}
			else {
				printf("!! Loopback(Boost Mixer to Line Out) \033[1;31mOFF\033[1;37m\n");
				wm8960_setLB2LOVOL(WM8960_OUTPUT_MIXER_GAIN_NEG_15DB);
				wm8960_setRB2ROVOL(WM8960_OUTPUT_MIXER_GAIN_NEG_15DB);
				wm8960_setLI2LOVOL(WM8960_OUTPUT_MIXER_GAIN_0DB);
				wm8960_setRI2ROVOL(WM8960_OUTPUT_MIXER_GAIN_0DB);
				wm8960_disableLB2LO();
				wm8960_disableRB2RO();
				wm8960_enableLD2LO();
				wm8960_enableRD2RO();
			}
			break;
		}
		case 'b' :
		{
			static bool adc_to_dac = false;
			adc_to_dac = !adc_to_dac;
			if(adc_to_dac) {
				printf("!! ADC to DAC loopback is \033[1;32mON\033[1;37m\n");
				wm8960_enableLoopBack();
			}
			else {
				printf("!! ADC to DAC loopback is \033[1;31mOFF\033[1;37m\n");
				wm8960_disableLoopBack();
			}
			break;
		}

		case 'd' :
		{
			static uint8_t sel = WM8960_ADC_LR_NORMAL;
			sel = (sel+1)%4;
			printf("!! ADC data select is set to %d\n", sel);
			wm8960_adc_datasel(sel);
			break;
		}

		case 'h' :
		{
			show_all_heap_info(TAG);
			break;
		}

		case 'R' :
			printf("esp_restart\n");
			esp_restart();
			break;

		case 'S' :
			wm8960_showRegister();
			break;
		
		case '+' :
			speaker_volume++;
			if(speaker_volume > 127) speaker_volume = 127;
			printf("** Speaker Volume : \033[1;32m%d\033[1;37m\n", speaker_volume);
			wm8960_setSpeakerVolume(speaker_volume);
			break;
		case '-' :
			speaker_volume--;
			if(speaker_volume < 47) speaker_volume = 47;
			printf("** Speaker Volume : \033[1;32m%d\033[1;37m\n", speaker_volume);
			wm8960_setSpeakerVolume(speaker_volume);
			break;

		case 't' :
		{
#if 0
			static int tone_sel = -1;
			tone_sel = (tone_sel+1)%3;
			if(tone_sel == 0) {
				play_tone(FREQ_C4, 160);
				play_tone(FREQ_E4, 160);
				play_tone(FREQ_G4, 160);
				play_tone(FREQ_C5, 160);
			}
			else if(tone_sel == 1) {
				play_tone(FREQ_C5, 160);
				play_tone(FREQ_E5, 160);
				play_tone(FREQ_G5, 160);
				play_tone(FREQ_C6, 160);
			}
			else if(tone_sel == 2) {
				play_tone(FREQ_C6, 160);
				play_tone(FREQ_E6, 160);
				play_tone(FREQ_G6, 160);
				play_tone(FREQ_C7, 160);
			}
			//play_tone(FREQ_C5, 160);
			//play_tone(FREQ_G4, 160);
			//play_tone(FREQ_E4, 160);
			//play_tone(FREQ_C4, 160);
#endif
		}
		break;

		case 'w' :
			break;

		case 'x' :
			break;
		case 'y' :
			break;
		case 'z' :
		{
			static tone_type_t tmp_type = TONE_TYPE_DING;
			tmp_type = (tmp_type+1)%TONE_TYPE_MAX;
			tone_type_to_play = tmp_type;
			break;
		}
		default : 
			break;
		}
		console_menu_is_running = false;
	}
}

