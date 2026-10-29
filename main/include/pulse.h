#pragma once

#include <stdint.h>
#include "project.h"

#ifdef __cplusplus
extern "C" {
#endif

void pulse_init(void);
void pulse_start_task(void);
void pulse_get_counts(uint32_t out[4]);

#ifdef __cplusplus
}
#endif
