#ifndef HH_COLOR_LOG_H_
#define HH_COLOR_LOG_H_

#define CC_USE_COLOR_LOG

#define CC_LOG_DEFAULT_LEVEL	CC_LOG_ALL

#define LAST_PRINT_ROW	38
extern bool debug_log_on;

#define CC_LOGP( format, ... )  do{ printf("\033[1;33m[PRMPT] \033[1;37m" format, ##__VA_ARGS__); fflush(stdout);} while(0)
#define CC_LOGR( format, ... )  do{ printf("\033[1;37m[RSPNS] \033[1;37m" format, ##__VA_ARGS__); fflush(stdout);} while(0)

#define CC_LOGI( format, ... )\
	do{ \
		printf("\033[1;37m[INFOR] \033[1;37m" format, ##__VA_ARGS__);\
		fflush(stdout);\
	} while(0)

#define CC_LOGW( format, ... )\
	do{ \
		printf("\033[1;35m[WARN]  \033[1;37m" format, ##__VA_ARGS__);\
		fflush(stdout);\
	} while(0)

#define CC_LOGE( format, ... )\
	do{ \
		printf("\033[1;31m[ERROR] \033[1;37m" format, ##__VA_ARGS__);\
		fflush(stdout);\
	} while(0)

#define CC_LOGD( format, ... )\
	do{\
		if(!debug_log_on) break;\
		printf("\033[0;37m[DEBUG] (%s:%d)" format, __func__, __LINE__, ##__VA_ARGS__);\
		fflush(stdout);\
	} while(0)

#define CC_LOGN( format, ... ) \
	do{\
		if(!debug_log_on) break;\
		printf("\033[0;37m" format, ##__VA_ARGS__);\
		fflush(stdout);\
	} while(0)

	#define CC_LOGNI( format, ... ) \
	do{\
		printf("\033[1;37m" format, ##__VA_ARGS__);\
		fflush(stdout);\
	} while(0)

	#define CC_LOGNW( format, ... ) \
	do{\
		printf("\033[1;35m" format, ##__VA_ARGS__);\
		printf("\033[1;37m");\
		fflush(stdout);\
	} while(0)

#endif //HH_COLOR_LOG_H_

