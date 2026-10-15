#ifndef HH_UTIL_H__
#define HH_UTIL_H__

#include <time.h>
#include "hh_onepass_common.h"

#define FREQ_C0			(16.35f)	// DO
#define FREQ_D0 		(18.35f)	// RE
#define FREQ_E0 		(20.60f)	// MI
#define FREQ_F0 		(21.83f)	// FA
#define FREQ_G0 		(24.50f)	// SOL
#define FREQ_A0 		(27.50f)	// LA
#define FREQ_B0 		(30.87f)	// SI
#define FREQ_C1			(32.70f)	// DO
#define FREQ_D1 		(36.71f)	// RE
#define FREQ_E1 		(41.20f)	// MI
#define FREQ_F1 		(43.65f)	// FA
#define FREQ_G1 		(49.00f)	// SOL
#define FREQ_A1 		(55.00f)	// LA
#define FREQ_B1 		(61.74f)	// SI
#define FREQ_C2			(65.41f)	// DO
#define FREQ_D2 		(73.42f)	// RE
#define FREQ_E2 		(82.41f)	// MI
#define FREQ_F2 		(87.31f)	// FA
#define FREQ_G2 		(98.00f)	// SOL
#define FREQ_A2 		(110.00f)	// LA
#define FREQ_B2 		(123.47f)	// SI
#define FREQ_C3			(130.81f)	// DO
#define FREQ_D3 		(146.83f)	// RE
#define FREQ_E3 		(164.81f)	// MI
#define FREQ_F3 		(174.61f)	// FA
#define FREQ_G3 		(196.00f)	// SOL
#define FREQ_A3 		(220.00f)	// LA
#define FREQ_B3 		(246.94f)	// SI
#define FREQ_C4 		(261.63f)	// DO
#define FREQ_D4 		(293.66f)	// RE
#define FREQ_E4 		(329.63f)	// MI
#define FREQ_F4 		(349.23f)	// FA
#define FREQ_G4 		(392.00f)	// SOL
#define FREQ_A4 		(440.00f)	// LA
#define FREQ_B4 		(493.88f)	// SI
#define FREQ_C5			(523.25f)	// DO
#define FREQ_D5 		(587.33f)	// RE
#define FREQ_E5 		(659.25f)	// MI
#define FREQ_F5 		(698.46f)	// FA
#define FREQ_G5 		(783.99f)	// SOL
#define FREQ_A5 		(880.00f)	// LA
#define FREQ_B5 		(987.77f)	// SI
#define FREQ_C6			(1046.50f)	// DO
#define FREQ_D6 		(1174.66f)	// RE
#define FREQ_E6 		(1318.51f)	// MI
#define FREQ_F6 		(1396.91f)	// FA
#define FREQ_G6 		(1567.98f)	// SOL
#define FREQ_A6 		(1760.00f)	// LA
#define FREQ_B6 		(1975.53f)	// SI
#define FREQ_C7			(2093.00f)	// DO
#define FREQ_D7 		(2349.32f)	// RE
#define FREQ_E7 		(2637.02f)	// MI
#define FREQ_F7 		(2793.83f)	// FA
#define FREQ_G7 		(3135.96f)	// SOL
#define FREQ_A7 		(3520.00f)	// LA
#define FREQ_B7 		(3951.07f)	// SI
#define FREQ_C8			(4186.01f)	// DO
#define FREQ_D8 		(4698.63f)	// RE
#define FREQ_E8 		(5274.04f)	// MI
#define FREQ_F8 		(5587.65f)	// FA
#define FREQ_G8 		(6271.93f)	// SOL

#define UNIT_0_625_MS 	625
#define UNIT_1_25_MS	1250
#define UNIT_10_MS		10000
#define MSEC_TO_UNITS(TIME, RESOLUTION) (((TIME) * 1000) / (RESOLUTION))

#define PP_YEAR ( (__DATE__ [7] - '0') * 1000 + (__DATE__ [8] - '0') * 100 \
			    + (__DATE__ [9] - '0') * 10   + (__DATE__ [10] - '0') )
#define PP_MONTH ( __DATE__ [2] == 'n' ? (__DATE__ [1] == 'a' ? 1 : 6) \
				 : __DATE__ [2] == 'b' ? 2 \
				 : __DATE__ [2] == 'r' ? (__DATE__ [0] == 'M' ? 3 : 4) \
				 : __DATE__ [2] == 'y' ? 5 \
				 : __DATE__ [2] == 'l' ? 7 \
				 : __DATE__ [2] == 'g' ? 8 \
				 : __DATE__ [2] == 'p' ? 9 \
				 : __DATE__ [2] == 't' ? 10 \
				 : __DATE__ [2] == 'v' ? 11 : 12)
#define PP_DATE ( (__DATE__ [4] == ' ' ? 0 : __DATE__ [4] - '0') * 10 \
                + (__DATE__ [5] - '0'))

#define PP_HOUR	( (__TIME__[0]-'0')*10 + (__TIME__[1]-'0') )
#define PP_MIN	( (__TIME__[3]-'0')*10 + (__TIME__[4]-'0') )
#define PP_SEC	( (__TIME__[6]-'0')*10 + (__TIME__[7]-'0') )

#define conv_to_epoch_time(x) 		\
{									\
	struct tm t = {0, };			\
	t.tm_year	= PP_YEAR - 1900;	\
	t.tm_mon	= PP_MONTH - 1;		\
	t.tm_mday	= PP_DATE;			\
	t.tm_hour	= PP_HOUR;			\
	t.tm_min	= PP_MIN;			\
	t.tm_sec	= PP_SEC;			\
	x = mktime(&t);					\
}

#define PP_DATE_HOUR (PP_YEAR*1000000 + PP_MONTH*10000 + PP_DATE*100 + PP_HOUR)

typedef struct __attribute((packed))
{
	uint64_t	first_half;		//ch0~ch63
	uint64_t	second_half;	//ch64~ch99
} data_128bit_t;

extern void ff_init_system_time(void);
extern bool is_Time_Elapsed(uint32_t time_stamp, uint32_t interval);
extern uint32_t elapsed_time_ms(uint32_t time_stamp);
extern void PutChar(uint8_t keyout);
extern uint8_t GetChar(uint32_t msec);
extern int GetString(char *pString, uint8_t size);
extern float GetPositiveFloat(void);
extern int GetPositiveInteger(void);
extern int GetHexadecimal(void);
extern bool GetHybrid(int32_t *output);
extern uint8_t GetPasswd(char *pString, uint8_t size);
extern char* get_current_time(void);
extern char* time_convert_to_printable(time_t *time);

#if ( configUSE_TRACE_FACILITY == 1 )
extern void query_task_state(void);
extern bool is_task_active(char* taskName);
#endif

extern uint32_t disp_free_heap_size(int n);
extern void prt_hex_data(char * filename, int start, int size);
extern uint32_t ff_gen_random(uint32_t from, uint32_t to);
extern void ff_dump_buf(uint8_t * p_buf, uint16_t len);
extern char * onezero(uint32_t in);

extern void reverse_128bit(data_128bit_t * p_data);
extern void reverse_64bit(uint64_t * p_data);
extern void reverse_32bit(uint32_t * p_data);
extern void reverse_16bit(uint16_t * p_data);

#ifndef MAC2STR
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#endif

#define SWAP(a, b, type) do { \
    type temp; \
    temp = a;  \
    a = b;     \
    b = temp;  \
} while (0)

#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif
#ifndef MAX
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#endif

extern void show_all_heap_info(const char * str);

#endif //
