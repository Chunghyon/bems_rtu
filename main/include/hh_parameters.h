#ifndef CC_PARAMETERS_H_
#define CC_PARAMETERS_H_

#include "stdio.h"
#include "driver/uart.h"
#include "freertos/portmacro.h"
#include "lwip/apps/sntp.h"
#include "hh_COLOR_LOG.h"
#include "hh_onepass_common.h"
#include "hh_util.h"
#include "hal/gpio_types.h"
#include "driver/ledc.h"

#define PP_ONLY_FOR_DEMO
#define PP_VOICE_FACE_PSRAM_RELEASE

#define PP_DEVICE_NAME_SP			"pHoNe"			//Smart Phone
#define PP_DEVICE_NAME_SB			"BAND_"			//Smart Band
#define PP_DEVICE_NAME_SK			"SmartKey"		//Smart Key
#define PP_DEVICE_NAME_CL			"CarLocator"	//Car Locator
#define IOS_SKIP_DURATION_MS			(1000)

#define NORMAL_DUTY_RED					(0)
#define NORMAL_DUTY_GREEN				(100)

#define GATE_OPEN_ACTIVE_HIGH			true
#define SCANNER_ACTIVE_DURATION_SEC		(3)
#define PP_STANDALONE_CHANNEL_NUMBER	(100)	// fixed channel number for Stand-alone LM
#define PP_LAMP_COUNT					(1)		// fixed lamp count for Stand-alone LM
#define DEFAULT_ENTRANCE_THRESHOLD		(50)	// Gate Entrance RSSI threshold in %
#define DEFAULT_ENTRANCE_DURATION		(5)		// Gate Keep-Open Duration

#define RSSI_MIN						(-100)
#define RSSI_MAX						(-40)

#define LEDC_CHANNEL_FOR_RED		LEDC_CHANNEL_6
#define LEDC_CHANNEL_FOR_GREEN		LEDC_CHANNEL_7

#define OTA_USE_HTTPD

#define OTA_REBOOT_TIMER_MS_START		30000	//ORG value 30000
#define OTA_REBOOT_TIMER_MS_RESTART		5000	//ORG value 5000
#define OTA_RECVTIMEOUT_MS				3000	//ORG value 3000

#define DEDICATED_OTA_TASK

#define PP_DEFAULT_VOC_READ_PERIOD_SEC			(5)
#define PP_OTHER_IP_DERIVED_FROM_MINE			(1)

#define PP_TEST_BOTH_OFF						(1)
#define PP_TEST_STOP_OTHERS_WHILE_TALKING_ON	(0)
#define PP_TEST_MESH_TRAFFIC_CONTROL_ON			(0)
#define PP_TEST_BOTH_ON							(0)

#if PP_TEST_BOTH_OFF	// test if notice_talking_state affects talking quality
	#define PP_STOP_OTHERS_WHILE_TALKING	(0)
	#define PP_MESH_TRAFFIC_CONTROL_220923	(0)
#elif PP_TEST_STOP_OTHERS_WHILE_TALKING_ON
	#define PP_STOP_OTHERS_WHILE_TALKING	(1)
	#define PP_MESH_TRAFFIC_CONTROL_220923	(0)
#elif PP_TEST_MESH_TRAFFIC_CONTROL_ON
	#define PP_STOP_OTHERS_WHILE_TALKING	(0)
	#define PP_MESH_TRAFFIC_CONTROL_220923	(1)
#elif PP_TEST_BOTH_ON
	#define PP_STOP_OTHERS_WHILE_TALKING	(1)
	#define PP_MESH_TRAFFIC_CONTROL_220923	(1)
#endif

#define PP_IP_BY_PROVISION_20220713		(1)
#define PP_CHK_TCP_ELAPSED_TIME 		(0)

#define CHANGE_SPI_ONOFF_RPT_FORMAT
#define APP_TCP_TASK_RESTART

#define USE_CONFIRM_ACK
#define USE_PERIODIC_TCP_COMM
#define USE_ONOFF_DOUBLE_CHECK

#define CC_ADDRESS_OPTIMIZATION			(1)

#define GATEWAY_ID_NOT_ASSIGNED		(0xFF)

#define USE_GET_ONOFF_ALL		//
#define SECS_PER_KEEPING_TIME_UNIT		3

#define ONEPASS_HEARTBEAT_TIME_MS		300000	// change to 5mins on 20210605
#define ONEPASS_HEARTBEAT_RANDOMISE		0x0FFF	// period = (ONEPASS_HEARTBEAT_TIME_MS-(ONEPASS_HEARTBEAT_RANDOMISE&esp_random()))
#define CHECK_ONEPASS_RCV_TIMEOUT		0

#define SPI_COMM_DELAY				    10		//Time Gap in msec between two SPI Communication
#define PERIODIC_TASK_PERIOD		  100		//Time interval between onoff check loop
#define RETOUCH_DELAY_RESPONDED		  3000
#define RETOUCH_DELAY_HIBERNATE		 30000

#define WAITING_TIME_FOR_INACTIVE	120000		//waiting time in msec for calling del_active_server();
#define WAITING_TIME_FOR_OOO		240000		//waiting time in msec for calling add_OOO_server();
#define GLOBAL_TCP_TIMEOUT			300000		//Reboot if TCP is not connected for this time in msec

#define TCP_TX_QUEUE_COUNT			512
#define TCP_RX_QUEUE_COUNT			256
#define SPI_QUEUE_COUNT				256
#define CONFIRM_ACK_BUFFER_COUNT 	200

#define DEFAULT_TCP_PACKET_ID		(0xFFFFFFFF)
#define SELF_ID_NOT_ASSIGNED		(0xFFFF)
#define CORNER_ID_NOT_ASSIGNED		(0xFF)

#define DEFAULT_EN_SENSE		true	//sensor on/off, 1/0
#define DEFAULT_EN_RELAY		true	//relaying on/off, 1/0

#define DEFAULT_CO_RANGE		5		//co-working light count, 0~7
#define DEFAULT_KEEPING_TIME	1		//on-duration in secs, 3+(value*3) secs
#define DEFAULT_RISING_TIME		1		//ramping-up time in secs, (value*0.5) secs
#define DEFAULT_FALLING_TIME	1		//ramping down time in secs, (value*0.5) secs

#define DEFAULT_FREQUENCY		15		//PWM frequency, 1~50kHz
#define DEFAULT_BRIGHT_MAX		100		//ramping-up-to value, 0~100
#define DEFAULT_BRIGHT_MIN		10		//ramping-dn-to value, 0~100
#define DEFAULT_BRIGHT_MAN		DEFAULT_BRIGHT_MAX		//manual-on brightness, 0~100
#define DEFAULT_SENSITIVITY		60		//sensor sensitivity, 0~100

#define DEFAULT_ID_SELF			SELF_ID_NOT_ASSIGNED		//light ID
#define DEFAULT_CORNER1			CORNER_ID_NOT_ASSIGNED		//Corner ID1
#define DEFAULT_CORNER2			CORNER_ID_NOT_ASSIGNED		//Corner ID2
#define DEFAULT_CORNER3			CORNER_ID_NOT_ASSIGNED		//Corner ID3

#define FREQ_1MHZ				1000000
#define PP_SPI_SCK_HZ			(4	* FREQ_1MHZ)	//4MHz max

#define STRING_BUFF_SIZE		128

#define DEFAULT_STACK_SIZE			(512*6)
#define BIG_STACK_SIZE				(512*10)

#define ALL							(0xff)
#define ALL_CHANNELS				(0xff)
#define GR_NOT_ASSIGNED				(0xff)
#define ALL_NODES					(0xffff)

#define NAME_AUDIO				"t_AUDIO"
#define STACK_AUDIO				(1024*4)
#define PRIORITY_AUDIO			PP_PERIORITY_MID
#define CORE_AUDIO				1

#define NAME_MENU				"t_MENU"
#define STACK_MENU				512*12
#define PRIORITY_MENU			PP_PERIORITY_MID
#define CORE_MENU				0

#define NAME_PERIOD				"t_PERIOD"
#define STACK_PERIOD			512*8
#define PRIORITY_PERIOD			PP_PERIORITY_MID
#define CORE_PERIOD				0

#define NAME_SPI_COMM			"t_SPI_COMM"
#define STACK_SPI_COMM			DEFAULT_STACK_SIZE
#define PRIORITY_SPI_COMM		PP_PERIORITY_MID
#define CORE_SPI_COMM			0

#define NAME_SPI_OPCODE			"t_SPI_OPCODE"
#define STACK_SPI_OPCODE		512*8
#define PRIORITY_SPI_OPCODE		PP_PERIORITY_MID
#define CORE_SPI_OPCODE			0

#define NAME_TCPC				"t_TCPC"
#define STACK_TCPC				512*8
#define PRIORITY_TCPC			PP_PERIORITY_MID
#define CORE_TCPC				0

#define ENTER 					(0x0D)
#define LINEFEED				(0x0A)
#define BACKSPACE				(0x08)
#define ESC						(0x1B)
#define SPACE					(0x20)
#define COMMA					(0x2C)
#define DEL						(0x7F)

#ifndef PIN2STR
#define PIN2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5], (a)[6], (a)[7]
#define PINSTR "%c%c%c%c%c%c%c%c"
#endif

enum
{
	OFF		= false,
	ON		= true,
	FALSE	= false,
	TRUE	= true,
};

enum
{
	SPI_DMA_CHANNEL_NONE,
	SPI_DMA_CHANNEL_1,
	SPI_DMA_CHANNEL_2,
};

#define UART_DATA_LEN 	(256)
#define MAX_APs			(20)

//#define KEY(x) const char* KEY##x = "key"#x

#define IDLE_DATA_LENGTH	3
#define ACTIVE_DATA_LENGTH	7

typedef enum
{
	SEL_TCP,
	SEL_UDP
} sel_tcpudp_t;

#define CLR_SCR()			printf("\033[2J")
#define MOVE_CURSOR(x,y)	printf("\033[%d;%dH", x, y)
#define CLR_LINE()			printf("\033[2K")
#define CLR_LINE_TO_END()	printf("\033[0K")

// Interface for ESP32_CAM
#define GPIO_HANDSHAKE	GPIO_NUM_3
#define SPI_MOSI1		GPIO_NUM_5
#define SPI_MISO1		GPIO_NUM_7
#define SPI_CLK1		GPIO_NUM_6
#define SPI_CS1			GPIO_NUM_10
#define CAM_REQ			GPIO_NUM_46

// Interface for LAN(W5500)
#define RSTn_W5500		GPIO_NUM_18
#define INTn_W5500		GPIO_NUM_45
#define SPI_MOSI		GPIO_NUM_11
#define SPI_MISO		GPIO_NUM_13
#define SPI_CLK			GPIO_NUM_12
#define CSB_W5500		GPIO_NUM_21

// Interface for Gate Control
#define GATE_CONTROL	GPIO_NUM_17

// Interface for Codec
#define SCL1			GPIO_NUM_8
#define SDA1			GPIO_NUM_9
#define I2S_BCLK		GPIO_NUM_41
#define I2S_LRCK		GPIO_NUM_42
#define I2S_DIN			GPIO_NUM_40	//to codec
#define I2S_DOUT		GPIO_NUM_39	//fr codec
#define I2S_MCLK		GPIO_NUM_1

// ETC interface
#define TXD1			GPIO_NUM_15
#define RXD1			GPIO_NUM_16
#define RST_M1			GPIO_NUM_38	//button interrupt_IN

#define RED_LED			GPIO_NUM_19
#define GREEN_LED		GPIO_NUM_20

#define GPIO_OUTPUT_PIN_SEL  (1ULL << RED_LED | 1ULL << GREEN_LED | 1ULL << I2S_MCLK | 1ULL << SPI_CS1 | \
	1ULL << CSB_W5500 | 1ULL << RSTn_W5500 | 1ULL << CAM_REQ | 1ULL << GATE_CONTROL)
#define GPIO_INPUT_PIN_SEL  (1ULL << RST_M1 | 1ULL << INTn_W5500 | 1ULL << GPIO_HANDSHAKE)

#endif //CC_PARAMETERS_H_

