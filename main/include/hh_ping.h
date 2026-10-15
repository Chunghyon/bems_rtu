#ifndef HH_PING_H__
#define HH_PING_H__

#include "stdint.h"
#include "stdbool.h"
#include "string.h"

typedef void(*ping_recv_function)(void* arg, void *pdata);
typedef void(*ping_sent_function)(void* arg, void *pdata);

struct ping_option {
	uint32_t count;
	uint32_t ip;
	uint32_t coarse_time;
	ping_recv_function recv_function;
	ping_sent_function sent_function;
	void* reverse;
};

struct ping_resp {
	uint32_t total_count;
	uint32_t resp_time;
	uint32_t seqno;
	uint32_t timeout_count;
	uint32_t bytes;
	uint32_t total_bytes;
	uint32_t total_time;
	int8_t  ping_err;
};

extern bool ff_ping_start(uint32_t adr, int count, int interval, int size, int timeout);
extern bool ff_is_address_alive(uint32_t adr);

#endif //HH_PING_H__
