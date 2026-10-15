#ifndef HH_NVS_H__
#define HH_NVS_H__

#include "HH_parameters.h"

#define NVS_STORE_PERIOD_MS	10000	//in msec
typedef enum
{
	SEL_NVS0,
	SEL_NVS_MAX
} nvs_sel_t;


typedef enum
{
	NVS0_LM_PARAMETERS,
	NVS0_SITE_CODE,

	NVS0_FRCD_LOC_IP,
	NVS0_FRCD_LOC_NM,
	NVS0_FRCD_LOC_GW,
	NVS0_FRCD_RMT_IP,	// voip ip

	NVS0_MAX
} nvs0_type_t;

extern esp_err_t nvs_add(nvs_sel_t nvssel, uint8_t ch);
extern esp_err_t nvs_del(nvs_sel_t nvssel, uint8_t ch);

extern void cc_init_nvs_autoup_result(void);
extern esp_err_t ff_store_autoup_result(uint8_t target);
extern esp_err_t ff_erase_autoup_result(uint8_t target);

#endif //HH_NVS_H__
