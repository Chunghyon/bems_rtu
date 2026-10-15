#ifndef HH_LEDC_H_
#define HH_LEDC_H_
#include "driver/ledc.h"
#include "freertos/timers.h"
#include "hh_parameters.h"
extern void ff_ledc_config(void);
extern void ff_blink_Rled(int duty, int period_ms, int count);
extern void ff_blink_Gled(int duty, int period_ms, int count);
#endif // HH_LEDC_H_