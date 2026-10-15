#include "stdlib.h"
#include "string.h"
#include "stdint.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_eth.h"
#include "esp_image_format.h"
#include "esp_ota_ops.h"
#include "esp_log.h"

#include "driver/uart.h"
#include "hh_parameters.h"
#include "hh_util.h"
#include "hh_onepass_common.h"

static const char *TAG = "cc_util";

extern bool debug_log_on;

#if ( configUSE_TRACE_FACILITY == 1 )
static const char *taskState[5] = {"Running", "Ready", "Blocked", "Suspended", "Deleted"};
#endif
char wrong_opcode[8] = {0, };

void ff_init_system_time(void)
{
	setenv("TZ", "KST-9", 1);
	tzset();
	static struct timeval nowTime;
	nowTime.tv_sec = 1577804400; //2020/01/01 00:00:00
	settimeofday(&nowTime, NULL);
}

/**
 * check if specific time is elapsed or not, in tick count
 */
bool is_Time_Elapsed(uint32_t timestamp, uint32_t interval)
{
    bool result = false;
    TickType_t tick = xTaskGetTickCount();

    if((tick - timestamp) >= interval)
    {
        result = true;
    }
    else if((timestamp > tick) && ((timestamp+interval) >= tick))
    {
        result = true;
    }

    return result;
}

uint32_t elapsed_time_ms(uint32_t time_stamp)
{
    uint32_t result = 0;
    TickType_t tick = xTaskGetTickCount();

    if(tick >= time_stamp) result = (tick-time_stamp)*portTICK_PERIOD_MS;
    else result = (UINT32_MAX - (time_stamp-tick))*portTICK_PERIOD_MS;

    return result;
}

uint8_t GetChar(uint32_t msec)
{
	uint8_t keyin=0;
	int result;

	if(!msec)
		result = uart_read_bytes(UART_NUM_0, &keyin, 1, portMAX_DELAY);
	else
		result = uart_read_bytes(UART_NUM_0, &keyin, 1, msec/portTICK_PERIOD_MS);

	if(result < 0) return 0;
	else return keyin;
}

void PutChar(uint8_t keyout)
{
	uart_write_bytes(UART_NUM_0, (const char *)&keyout, 1);
}

/**
 * Get String input through uart, copy the input to pString[] and return the real string length
 */
int GetString(char *pString, uint8_t size)
{
	char pTmp[256] = {0, };
	memset(pString, 0, size);

	for(int n=0; n<size; n++)
	{
		pTmp[n] = GetChar(0);

		if		(pTmp[n] == ESC)
		{
			printf("\n");
			return (-1);
		}
		else if	(pTmp[n] == ENTER)
		{
			if(n > 0)
			{
				pTmp[n] = 0;
				strcpy(pString, pTmp);
				printf("\n");
				return n;
			}
			else
			{
				printf("\n");
				return 0;
			}
		}
		else if (pTmp[n] == BACKSPACE)
		{
			if(n > 0)
			{
				PutChar(BACKSPACE);
				PutChar(SPACE);
				PutChar(BACKSPACE);
				pTmp[n-1] = pTmp[n] = 0;
				n=n-2;
			}
			else
			{
				n=(-1);
			}
		}
		else PutChar(pTmp[n]);
	}
	printf("\n");
	return (-1);	//if lenght >= size of input, return error...
}

/**
 * Same as GetString but display as ****
 */
uint8_t GetPasswd(char *pString, uint8_t size)
{
	char pTmp[256] = {0, };
	memset(pString, 0, size);

	for(int n=0; n<size; n++)
	{
		pTmp[n] = GetChar(0);

		if		(pTmp[n] == ESC) 	return 0;
		else if	(pTmp[n] == ENTER)
		{
			if(n > 0)
			{
				pTmp[n] = 0;
				strcpy(pString, pTmp);
				return n;
			}
			else return 0;
		}
		else if (pTmp[n] == BACKSPACE)
		{
			if(n > 0)
			{
				PutChar(BACKSPACE);
				PutChar(SPACE);
				PutChar(BACKSPACE);
				pTmp[n-1] = pTmp[n] = 0;
				n=n-2;
			}
			else {
				n=(-1);
			}
		}
		else PutChar('*');
	}
	printf("\n");
	return 0;	//if lenght >= size of input, return error...
}

float GetPositiveFloat(void)
{
	char tmpStr[32] = {0,};
	int len = GetString(tmpStr, 32);
	if(len<=0) return -1.0;
	return atof(tmpStr);
}

int GetPositiveInteger(void)
{
	char tmpStr[32] = {0,};

	for(int n=0; n<sizeof(tmpStr); n++)
	{
		tmpStr[n] = GetChar(0);

		if		(tmpStr[n] == ESC) 	return (-1);
		else if	(tmpStr[n] == ENTER)
		{
			if(n > 0)
			{
				tmpStr[n] = 0;
				break;;
			}
			else return -1;
		}
		else if (tmpStr[n] == BACKSPACE)
		{
			if(n > 0)
			{
				PutChar(BACKSPACE);
				PutChar(SPACE);
				PutChar(BACKSPACE);
				tmpStr[n-1] = tmpStr[n] = 0;
				n=n-2;
			}
			else {
				n=(-1);
			}
		}
		else if (tmpStr[n] < '0' || tmpStr[n] > '9')
		{
			n=n-1;
			if(n<-1) n = (-1);
		}
		else PutChar(tmpStr[n]);
	}
	printf("\n");
	return atoi(tmpStr);
}

int GetHexadecimal(void)
{
	char tmpStr[32] = {0,};

	for(int n=0; n<sizeof(tmpStr); n++)
	{
		tmpStr[n] = GetChar(0);

		if		(tmpStr[n] == ESC) 	return (-1);
		else if	(tmpStr[n] == ENTER)
		{
			if(n > 0)
			{
				tmpStr[n] = 0;
				break;;
			}
			else return -1;
		}
		else if (tmpStr[n] == BACKSPACE)
		{
			if(n > 0)
			{
				PutChar(BACKSPACE);
				PutChar(SPACE);
				PutChar(BACKSPACE);
				tmpStr[n-1] = tmpStr[n] = 0;
				n=n-2;
			}
			else {
				n=(-1);
			}
		}
		else if ((tmpStr[n] < '0' || tmpStr[n] > '9') && (tmpStr[n] < 'a' || tmpStr[n] > 'f') && (tmpStr[n] < 'A' || tmpStr[n] > 'F'))
		{
			n=n-1;
			if(n<-1) n = (-1);
		}
		else PutChar(tmpStr[n]);
	}
	printf("\n");
	return (int)strtol(tmpStr, NULL, 16);
}

/**
 * Get multi-digits integer or 1-digit character, return true if ingeter, return false if character
 */
bool GetHybrid(int32_t *output)
{
	char input[16] = {0,};

	for(int i=0; i<16; i++)
	{
		input[i] = (i==0) ? GetChar(0) : GetChar(500);
		if (input[i] == BACKSPACE)
		{
			if(i > 0)
			{
				PutChar(BACKSPACE);
				PutChar(SPACE);
				PutChar(BACKSPACE);
				input[i-1] = input[i] = 0;
				i=i-2;
			}
			else {
				i=(-1);
			}
		}
		else if(input[i] == ENTER)
		{
			if(i > 0)
			{
				input[i] = 0;
				break;;
			}
			else
			{
				*output = (int32_t)ENTER;
				return false;
			}
		}
		else if(input[i] == 0) break; //not input for 500msec
		else if(input[i] < '0' || input[i] > '9' )
		{
			*output = (int32_t)input[0]; //character
			PutChar(input[i]);
			return false;
		}
		else PutChar(input[i]);
	}

	*output = atoi(input);	//integer
	return true;			//
}

/**
 * Query a Task's stauts
 */
#if ( configUSE_TRACE_FACILITY == 1 )
void query_task_state(void)
{
	UBaseType_t taskCount;

	taskCount = uxTaskGetNumberOfTasks();
	printf("!!!! uxTaskGetNumberOfTasks():%d\n", taskCount);
	printf("!!!! uxTaskGetSystemState()\n");
	TaskStatus_t* TaskStatusArray = malloc(sizeof(TaskStatus_t) * taskCount);
	if(TaskStatusArray){
	uxTaskGetSystemState((TaskStatus_t * const)TaskStatusArray, (const UBaseType_t)taskCount, NULL );
	printf("----------------------------------------\n");
	for(int n=0; n<taskCount; n++) {
//		if(TaskStatusArray[n].pcTaskName[1] == '_')
		printf("%2d %-12s(%p) : #%2d, %-9s(%d), pri %2d(%lu)\n",
				n, TaskStatusArray[n].pcTaskName, TaskStatusArray[n].xHandle, TaskStatusArray[n].xTaskNumber,
				taskState[(int)TaskStatusArray[n].eCurrentState],
				(int)TaskStatusArray[n].eCurrentState, TaskStatusArray[n].uxCurrentPriority, TaskStatusArray[n].usStackHighWaterMark);
	}
	CC_LOGN("----------------------------------------\n");
	free(TaskStatusArray);
	}
	else {
		printf("malloc failed...\n");
	}
}

/**
 * Check if a task is active
 */
bool is_task_active(char* taskName)
{
	bool result = false;
	TaskStatus_t* TaskStatusArray = NULL;
	UBaseType_t taskCount = 0;

	taskCount = uxTaskGetNumberOfTasks();
	if(taskCount) TaskStatusArray = malloc(sizeof(TaskStatus_t) * taskCount);

	if(TaskStatusArray != NULL) {
		uxTaskGetSystemState((TaskStatus_t * const)TaskStatusArray, (const UBaseType_t)taskCount, NULL );
		for(int n=0; n<taskCount; n++)
		{
			if(!strcmp(taskName, TaskStatusArray[n].pcTaskName))
			{
				result = true;
				break;
			}
		}
		free(TaskStatusArray);
		return result;
	}
	return result;
}
#endif

/**
 * convert tm type to printable string
 */
char* time_convert_to_printable(time_t *time)
{
	struct tm timeinfo = {0,};
	static char print_buffer[36]={0,};

	localtime_r(time, &timeinfo);
	strftime(print_buffer, sizeof(print_buffer), "%a, %b.%d, %H:%M:%S", &timeinfo);
	return print_buffer;
}

/**
 * get current time
 */
char* get_current_time(void)
{
	time_t now = 0;
	struct tm timeinfo = {0,};
	static char print_buffer[64]={0,};

	time(&now);
	localtime_r(&now, &timeinfo);
	strftime(print_buffer, sizeof(print_buffer), "%Y/%m/%d %H:%M:%S", &timeinfo);
//	strftime(print_buffer, sizeof(print_buffer), "%a, %b.%d, %H:%M:%S", &timeinfo);
//	printf("!! Current time : %s\n", time_buffer);
	return print_buffer;
}

void show_all_heap_info(const char * str)
{
	size_t inter_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
	size_t inter_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
	size_t inter_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

	size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
	size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
	size_t psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

	CC_LOGNW("** Heap information (%s)**\n", str);
	CC_LOGNW("Int. total : %7d, free %7d, largest block %7d\n", inter_total, inter_free, inter_largest);
	CC_LOGNW("Ext. total : %7d, free %7d, largest block %7d\n", psram_total, psram_free, psram_largest);
	CC_LOGNW("fRTOS free : %7d\n", xPortGetFreeHeapSize());
	CC_LOGNW("-------------------------------\n");
}

uint32_t disp_free_heap_size(int n)
{
	uint32_t free_heap_size;
	free_heap_size = heap_caps_get_free_size(MALLOC_CAP_8BIT);
	CC_LOGN("[%d]free_heap_size : %lu bytes\n", n, free_heap_size);
	return free_heap_size;
}

void prt_hex_data(char * filename, int start, int size)
{
	uint8_t rd_data[size];
	int fd = open(filename, 0);
	if (fd == -1) {
		return;
	}
	lseek(fd, start, SEEK_SET);
	read(fd, rd_data, size);
	close(fd);

	for(int i=0; i<size; i++) {
		if(i%8==0) CC_LOGN("\n");
		else CC_LOGN(" ");
		CC_LOGN("0x%02X,", rd_data[i]);
	}
}

uint32_t ff_gen_random(uint32_t from, uint32_t to)
{
	if(from >= to) return from;
	int diff = to - from;
	return (esp_random() % diff) + from;
}

void ff_dump_buf(uint8_t * p_buf, uint16_t len)
{
	for(int i=0; i<len; i++) {
		if(i%16 == 0) {
			CC_LOGN("%04X ", i);
		}
		CC_LOGN(" %02x", p_buf[i]);
		if(i%8 == 7) CC_LOGN(" ");
		if(i%16 == 15) CC_LOGN("\n");
	}
	CC_LOGN("\n");
}

uint32_t ff_running_image_size(const esp_partition_t *running)
{
	esp_image_metadata_t data;

	const esp_partition_pos_t running_pos  = {
		.offset = running->address,
		.size = running->size,
	};

	data.start_addr = running_pos.offset;

	esp_image_verify(ESP_IMAGE_VERIFY, &running_pos, &data);

	return data.image_len;
}

char * onezero(uint32_t in)
{
	return in ? "\033[1;31m1\033[1;37m" : "0";
}

/**
 * reverse the order 0->127, 1->126,...,127->0
 */
void reverse_128bit(data_128bit_t * p_data)
{
	uint8_t tmp;
	uint8_t * p_tmp_data = (uint8_t*)p_data;

	tmp = *(p_tmp_data+0);
	*(p_tmp_data+0) = *(p_tmp_data+15);
	*(p_tmp_data+15) = tmp;

	tmp = *(p_tmp_data+1);
	*(p_tmp_data+1) = *(p_tmp_data+14);
	*(p_tmp_data+14) = tmp;

	tmp = *(p_tmp_data+2);
	*(p_tmp_data+2) = *(p_tmp_data+13);
	*(p_tmp_data+13) = tmp;

	tmp = *(p_tmp_data+3);
	*(p_tmp_data+3) = *(p_tmp_data+12);
	*(p_tmp_data+12) = tmp;

	tmp = *(p_tmp_data+4);
	*(p_tmp_data+4) = *(p_tmp_data+11);
	*(p_tmp_data+11) = tmp;

	tmp = *(p_tmp_data+5);
	*(p_tmp_data+5) = *(p_tmp_data+10);
	*(p_tmp_data+10) = tmp;

	tmp = *(p_tmp_data+6);
	*(p_tmp_data+6) = *(p_tmp_data+9);
	*(p_tmp_data+9) = tmp;

	tmp = *(p_tmp_data+7);
	*(p_tmp_data+7) = *(p_tmp_data+8);
	*(p_tmp_data+8) = tmp;
}

void reverse_64bit(uint64_t * p_data)
{
	uint8_t tmp;
	uint8_t * p_tmp_data = (uint8_t*)p_data;

	tmp = *(p_tmp_data+0);
	*(p_tmp_data+0) = *(p_tmp_data+7);
	*(p_tmp_data+7) = tmp;

	tmp = *(p_tmp_data+1);
	*(p_tmp_data+1) = *(p_tmp_data+6);
	*(p_tmp_data+6) = tmp;

	tmp = *(p_tmp_data+2);
	*(p_tmp_data+2) = *(p_tmp_data+5);
	*(p_tmp_data+5) = tmp;

	tmp = *(p_tmp_data+3);
	*(p_tmp_data+3) = *(p_tmp_data+4);
	*(p_tmp_data+4) = tmp;
}

void reverse_32bit(uint32_t * p_data)
{
	uint8_t tmp;
	uint8_t * p_tmp_data = (uint8_t*)p_data;
	tmp = *p_tmp_data;
	*p_tmp_data = *(p_tmp_data+3);
	*(p_tmp_data+3) = tmp;
	tmp = *(p_tmp_data+1);
	*(p_tmp_data+1) = *(p_tmp_data+2);
	*(p_tmp_data+2) = tmp;
}

void reverse_16bit(uint16_t * p_data)
{
	uint8_t tmp;
	uint8_t * p_tmp_data = (uint8_t*)p_data;
	tmp = *p_tmp_data;
	*p_tmp_data = *(p_tmp_data+1);
	*(p_tmp_data+1) = tmp;
}
