#include <math.h>
#include <float.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include "hh_ping.h"

#include "lwip/inet_chksum.h"
#include "lwip/ip.h"
#include "lwip/ip4.h"
#include "lwip/err.h"
#include "lwip/icmp.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "hh_COLOR_LOG.h"

static uint16_t ping_seq_num;
static uint8_t stopped = 0;

/*
* Statistics
*/
static uint32_t transmitted = 0;
static uint32_t received = 0;
static float min_time = 0;
static float max_time = 0;
static float mean_time = 0;
static float last_mean_time = 0;
static float var_time = 0;

#define PING_ID 0xAFAF

#define PING_DEFAULT_COUNT    10
#define PING_DEFAULT_INTERVAL  1
#define PING_DEFAULT_SIZE     32
#define PING_DEFAULT_TIMEOUT   1

/*
* Helper functions
*
*/
static void ping_prepare_echo(struct icmp_echo_hdr *iecho, uint16_t len) {
	size_t i;
	size_t data_len = len - sizeof(struct icmp_echo_hdr);

	ICMPH_TYPE_SET(iecho, ICMP_ECHO);
	ICMPH_CODE_SET(iecho, 0);
	iecho->chksum = 0;
	iecho->id = PING_ID;
	iecho->seqno = htons(++ping_seq_num);

	/* fill the additional data buffer with some data */
	for (i = 0; i < data_len; i++) {
		((char*)iecho)[sizeof(struct icmp_echo_hdr) + i] = (char)i;
	}

	iecho->chksum = inet_chksum(iecho, len);
}

static err_t ping_send(int s, ip4_addr_t *addr, int size) {
	struct icmp_echo_hdr *iecho;
	struct sockaddr_in to;
	size_t ping_size = sizeof(struct icmp_echo_hdr) + size;
	int err;

	iecho = (struct icmp_echo_hdr *)mem_malloc((mem_size_t)ping_size);
	if (!iecho) {
		return ERR_MEM;
	}

	ping_prepare_echo(iecho, (uint16_t)ping_size);

	to.sin_len = sizeof(to);
	to.sin_family = AF_INET;
	to.sin_addr.s_addr = addr->addr;

	if ((err = sendto(s, iecho, ping_size, 0, (struct sockaddr*)&to, sizeof(to)))) {
		transmitted++;
	}

	free(iecho);
	return (err ? ERR_OK : ERR_VAL);
}

static bool ping_recv(int s)
{
	bool result = false;
	char buf[64];
	int fromlen, len;
	struct sockaddr_in from;
	struct ip_hdr *iphdr;
	struct icmp_echo_hdr *iecho = NULL;
	char ipa[16];
	struct timeval begin;
	struct timeval end;
	uint64_t micros_begin;
	uint64_t micros_end;
	float elapsed;

	// Register begin time
	gettimeofday(&begin, NULL);

	while ((len = recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr*)&from, (socklen_t*)&fromlen)) > 0) {
		if (len >= (int)(sizeof(struct ip_hdr) + sizeof(struct icmp_echo_hdr))) {
			// Register end time
			gettimeofday(&end, NULL);

			/// Get from IP address
			ip4_addr_t fromaddr;
			fromaddr.addr = from.sin_addr.s_addr;

			strcpy(ipa, inet_ntoa(fromaddr));

			// Get echo
			iphdr = (struct ip_hdr *)buf;
			iecho = (struct icmp_echo_hdr *)(buf + (IPH_HL(iphdr) * 4));

			// Print ....
			if ((iecho->id == PING_ID) && (iecho->seqno == htons(ping_seq_num))) {
				received++;

				// Get elapsed time in milliseconds
				micros_begin = begin.tv_sec * 1000000;
				micros_begin += begin.tv_usec;

				micros_end = end.tv_sec * 1000000;
				micros_end += end.tv_usec;

				elapsed = (float)(micros_end - micros_begin) / (float)1000.0;

				// Update statistics
				// Mean and variance are computed in an incremental way
				if (elapsed < min_time) {
					min_time = elapsed;
				}

				if (elapsed > max_time) {
					max_time = elapsed;
				}

				last_mean_time = mean_time;
				mean_time = (((received - 1) * mean_time) + elapsed) / received;

				if (received > 1) {
					var_time = var_time + ((elapsed - last_mean_time) * (elapsed - mean_time));
				}

				CC_LOGN("%d bytes from %s: icmp_seq=%d time=%.3f ms\r\n",
						len-(int)(sizeof(struct ip_hdr) + sizeof(struct icmp_echo_hdr)), ipa,
						ntohs(iecho->seqno), elapsed
				);
				result = true;
				break;
			}
		}
		else {
			CC_LOGN("Request timeout for icmp_seq %d\r\n", ping_seq_num);
		}
	}

//	if (len < 0) {
//		CC_LOGN("Request timeout for icmp_seq %d\r\n", ping_seq_num);
//	}

	return result;
}
#if false
bool ff_ping_start(uint32_t adr, int count, int interval, int size, int timeout) {
	struct sockaddr_in address;
	ip4_addr_t ping_target;
	int s;
	// Get default values if argument are not provided
	if (count == 0) {
		count = PING_DEFAULT_COUNT;
	}

	if (interval == 0) {
		interval = PING_DEFAULT_INTERVAL;
	}

	if (size == 0) {
		size = PING_DEFAULT_SIZE;
	}

	if (timeout == 0) {
		timeout = PING_DEFAULT_TIMEOUT;
	}

	// Create socket
	if ((s = socket(AF_INET, SOCK_RAW, IP_PROTO_ICMP)) < 0) {
		CC_LOGN("socket create failed\n");
		return false;
	}

	address.sin_addr.s_addr = adr;
	ping_target.addr = address.sin_addr.s_addr;

	// Setup socket
	struct timeval tout;

	// Timeout
	tout.tv_sec = timeout;
	tout.tv_usec = 0;

	if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tout, sizeof(tout)) < 0) {
		closesocket(s);
		CC_LOGN("setsockopt failed\n");
		return false;
	}

	stopped = 0;
	transmitted = 0;
	received = 0;
	min_time = 1.E+9;// FLT_MAX;
	max_time = 0.0;
	mean_time = 0.0;
	var_time = 0.0;

	// Begin ping ...
	char ipa[16];

	strcpy(ipa, inet_ntoa(ping_target));
	CC_LOGN("PING %s: %d data bytes\r\n",  ipa, size);

	ping_seq_num = 0;

	while ((ping_seq_num < count) && (!stopped)) {
		if (ping_send(s, &ping_target, size) == ERR_OK) {
			ping_recv(s);
		}
		vTaskDelay( pdMS_TO_TICKS(interval*1000L));
	}

	closesocket(s);

	CC_LOGN("%d packets transmitted, %d packets received, %.1f%% packet loss\r\n",
		transmitted,
		received,
		((((float)transmitted - (float)received) / (float)transmitted) * 100.0)
	);

	if (received) {
		struct ping_resp pingresp;
		CC_LOGN("round-trip min/avg/max/stddev = %.3f/%.3f/%.3f/%.3f ms\r\n", min_time, mean_time, max_time, sqrt(var_time / received));
		pingresp.total_count = 10;
		pingresp.timeout_count = 10;
		pingresp.total_bytes = 1;
		pingresp.total_time = mean_time;
		pingresp.ping_err = 0;

		return true;
	//	ping_o->sent_function(ping_o, (uint8*)&pingresp);
	}
	return false;
}
#endif
bool ping_regist_recv(struct ping_option *ping_opt, ping_recv_function ping_recv)
{
	if (ping_opt == NULL)
		return false;

	ping_opt->recv_function = ping_recv;
	return true;
}

bool ping_regist_sent(struct ping_option *ping_opt, ping_sent_function ping_sent)
{
	if (ping_opt == NULL)
		return false;

	ping_opt->sent_function = ping_sent;
	return true;
}

bool ff_is_address_alive(uint32_t adr)
{
	bool result = false;
	ip4_addr_t ping_target;
	int ping_socket;

	int count		= 3;
	int size		= 32;
	int interval_ms	= 500;
	int timeout_ms	= 500;

	// Create socket
	if ((ping_socket = socket(AF_INET, SOCK_RAW, IP_PROTO_ICMP)) < 0) {
		CC_LOGN("socket create failed\n");
		return false;
	}

	ping_target.addr = adr;

	struct timeval tout;
	tout.tv_sec = timeout_ms>=1000 ? (timeout_ms/1000) : 0;
	tout.tv_usec = (timeout_ms%1000)*1000;

	if (setsockopt(ping_socket, SOL_SOCKET, SO_RCVTIMEO, &tout, sizeof(tout)) < 0) {
		closesocket(ping_socket);
		CC_LOGN("setsockopt failed\n");
		return false;
	}

	stopped = 0;
	transmitted = 0;
	received = 0;
	min_time = 1.E+9;// FLT_MAX;
	max_time = 0.0;
	mean_time = 0.0;
	var_time = 0.0;

	// Begin ping ...
	char ipa[16];

	strcpy(ipa, inet_ntoa(ping_target));
//	CC_LOGN("PING %s: %d data bytes\r\n",  ipa, size);

	ping_seq_num = 0;

	while ((ping_seq_num < count) && (!stopped)) {
		if (ping_send(ping_socket, &ping_target, size) == ERR_OK) {
			result = ping_recv(ping_socket);
			if(result) {
				break;	// break if at least one response from peer
			}
		}
		vTaskDelay( pdMS_TO_TICKS(interval_ms));
	}
	closesocket(ping_socket);

	return result;
}
