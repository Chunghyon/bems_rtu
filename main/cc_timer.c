#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "hh_parameters.h"
#include "hh_util.h"

TimerHandle_t timer0;
TimerHandle_t timer1;

static void cb_timer(TimerHandle_t xTimer)
{

}

void CC_init_timer(void)
{
	timer0 = xTimerCreate("TaggingT0", (1000 / portTICK_PERIOD_MS),
					   pdTRUE, (void *)timer0, cb_timer);
	timer1 = xTimerCreate("TaggingT1", (10000 / portTICK_PERIOD_MS),
				   pdFALSE, (void *)timer1, cb_timer);
}

