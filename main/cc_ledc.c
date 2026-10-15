#include "hh_parameters.h"
#include "hh_ledc.h"

static TimerHandle_t handle_Rled_timer, handle_Gled_timer;
static int cc_Rblink_duty, cc_Gblink_duty;
static int cc_Rblink_period_ms, cc_Gblink_period_ms;
static int cc_Rblink_count, cc_Gblink_count;
static bool cc_Rblink_timer_started, cc_Gblink_timer_started;

bool cc_Rblink_timer_running, cc_Gblink_timer_running;

static void change_Rled_duty(uint32_t duty)
{
	if(duty>100) duty = 100;
	uint32_t duty_cal = duty*(1<<LEDC_TIMER_7_BIT)/100;
	if(duty_cal>(1<<LEDC_TIMER_7_BIT)) duty_cal = (1<<LEDC_TIMER_7_BIT);

	ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_FOR_RED, duty_cal, 0);
}

static void change_Gled_duty(uint32_t duty)
{
	if(duty>100) duty = 100;
	uint32_t duty_cal = duty*(1<<LEDC_TIMER_7_BIT)/100;
	if(duty_cal>(1<<LEDC_TIMER_7_BIT)) duty_cal = (1<<LEDC_TIMER_7_BIT);

	ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_FOR_GREEN, duty_cal, 0);
}

static void cb_Rled_timer( TimerHandle_t xTimer )
{
	static int timer_count = 0;
	if(cc_Rblink_timer_started) {
		cc_Rblink_timer_started = false;
		timer_count = cc_Rblink_period_ms/10;
		change_Rled_duty(cc_Rblink_duty);
		return;
	}

	if(--timer_count == 0) {
		timer_count = cc_Rblink_period_ms/10;
		--cc_Rblink_count;
		if(cc_Rblink_count == 1) {
			change_Rled_duty(NORMAL_DUTY_RED);
			cc_Rblink_timer_running = false;
			xTimerStop(handle_Rled_timer, pdMS_TO_TICKS(10));
		}
		else if(cc_Rblink_count%2) {
			change_Rled_duty(0);
		}
		else {
			change_Rled_duty(cc_Rblink_duty);
		}
	}
}

static void cb_Gled_timer( TimerHandle_t xTimer )
{
	static int timer_count = 0;
	if(cc_Gblink_timer_started) {
		cc_Gblink_timer_started = false;
		timer_count = cc_Gblink_period_ms/10;
		change_Gled_duty(cc_Gblink_duty);
		return;
	}

	if(--timer_count == 0) {
		timer_count = cc_Gblink_period_ms/10;
		--cc_Gblink_count;
		if(cc_Gblink_count == 1) {
			change_Gled_duty(NORMAL_DUTY_GREEN);
			cc_Gblink_timer_running = false;
			xTimerStop(handle_Gled_timer, pdMS_TO_TICKS(10));
		}
		else if(cc_Gblink_count%2) {
			change_Gled_duty(0);
		}
		else {
			change_Gled_duty(cc_Gblink_duty);
		}
	}
}

void ff_blink_Rled(int duty, int period_ms, int count)
{
	if(cc_Rblink_timer_running) {
		xTimerStop(handle_Rled_timer, pdMS_TO_TICKS(10));
	}
	if(period_ms<10) period_ms = 10;
	cc_Rblink_duty = duty;
	cc_Rblink_period_ms = period_ms;
	cc_Rblink_count = count*2;
	cc_Rblink_timer_started = true;
	xTimerStart(handle_Rled_timer, 0);
	cc_Rblink_timer_running = true;
}

void ff_blink_Gled(int duty, int period_ms, int count)
{
	if(cc_Gblink_timer_running) {
		xTimerStop(handle_Gled_timer, pdMS_TO_TICKS(10));
	}
	if(period_ms<10) period_ms = 10;
	cc_Gblink_duty = duty;
	cc_Gblink_period_ms = period_ms;
	cc_Gblink_count = count*2;
	cc_Gblink_timer_started = true;
	xTimerStart(handle_Gled_timer, 0);
	cc_Gblink_timer_running = true;
}


void ff_ledc_config(void)
{
	ledc_timer_config_t ledc_timer = {
		.duty_resolution	= LEDC_TIMER_7_BIT,		// resolution of PWM duty
		.freq_hz			= 100*1000,				// frequency of PWM signal
		.speed_mode			= LEDC_LOW_SPEED_MODE,	// timer mode
		.timer_num			= LEDC_TIMER_3,			// timer index
	};
	// Set configuration of timer0 for high speed channels
	ledc_timer_config(&ledc_timer);

	ledc_channel_config_t rledc_channel = {
		.channel	= LEDC_CHANNEL_FOR_RED,
		.duty		= 0,
		.gpio_num	= RED_LED,
		.speed_mode	= LEDC_LOW_SPEED_MODE,
		.hpoint		= (1<<(ledc_timer.duty_resolution))-1,
		.timer_sel	= LEDC_TIMER_3,
	};

	ledc_channel_config_t gledc_channel = {
		.channel	= LEDC_CHANNEL_FOR_GREEN,
		.duty		= 0,
		.gpio_num	= GREEN_LED,
		.speed_mode	= LEDC_LOW_SPEED_MODE,
		.hpoint		= (1<<(ledc_timer.duty_resolution))-1,
		.timer_sel	= LEDC_TIMER_3,
	};

	ledc_channel_config(&rledc_channel);
	ledc_channel_config(&gledc_channel);

	// Initialize fade service.
	ledc_fade_func_install(0);

	handle_Rled_timer = xTimerCreate("LedTimerR", pdMS_TO_TICKS(10), pdTRUE, 0, cb_Rled_timer);		// 10msec period
	handle_Gled_timer = xTimerCreate("LedTimerG", pdMS_TO_TICKS(10), pdTRUE, 0, cb_Gled_timer);		// 10msec period

//	change_Rled_duty(NORMAL_DUTY_RED);
//	change_Gled_duty(NORMAL_DUTY_GREEN);
	change_Rled_duty(100);
	change_Gled_duty(0);
}
