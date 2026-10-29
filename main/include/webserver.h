#pragma once

#include <stdbool.h>
#include "project.h"

#ifdef __cplusplus
extern "C" {
#endif

extern bool web_update_enabled;
extern int  web_update_period_sec;

void webserver_nvs_load(void);
void webserver_nvs_save_period(int period);
void webserver_start(void);
void webserver_stop(void);

#ifdef __cplusplus
}
#endif
