/* 
 * power_phase_mode.c
 * Drop-in additions / replacements for ads131m08.c
 * 
 * 1. Add near the top (after POWER_PHASES define):
 */
#include "project.h"
#include "board.h"
#include "ads131m08.h"

#include "esp_log.h"

static const char *TAG = "power_phase_mode";

static power_phase_mode_t g_phase_mode = POWER_MODE_THREE;

void ads131m08_set_phase_mode(power_phase_mode_t mode)
{
    if (mode == POWER_MODE_SINGLE || mode == POWER_MODE_THREE) {
        g_phase_mode = mode;
        ESP_LOGI(TAG, "Power phase mode set to %s",
                 (mode == POWER_MODE_SINGLE) ? "SINGLE" : "THREE");
    }
}

power_phase_mode_t ads131m08_get_phase_mode(void)
{
    return g_phase_mode;
}

/*
 * 2. Inside task_power_calc(), replace the phase loops with:
 *
 *   int phases = (g_phase_mode == POWER_MODE_SINGLE) ? 1 : POWER_PHASES;
 *
 *   // ... existing sum arrays of size POWER_PHASES ...
 *
 *   for (uint32_t k = now - n; k < now; k++) {
 *       const power_sample_t *s = &power_ring[k % POWER_RING_SIZE];
 *       for (int p = 0; p < phases; p++) {
 *           double v = (double)s->v[p] * PP_V_PER_COUNT;
 *           double i = (double)s->i[p] * PP_I_PER_COUNT;
 *           sum_v2[p] += v * v;
 *           sum_i2[p] += i * i;
 *           sum_p[p]   += v * i;
 *       }
 *       // zero-crossing still on phase 0
 *       ...
 *   }
 *
 *   for (int p = 0; p < phases; p++) {
 *       // existing RMS / power / energy calculation
 *   }
 *
 *   // Clear unused phases in single mode
 *   for (int p = phases; p < POWER_PHASES; p++) {
 *       power_result[p].vrms = 0.0f;
 *       power_result[p].irms = 0.0f;
 *       power_result[p].kw   = 0.0f;
 *       power_result[p].kvar = 0.0f;
 *       power_result[p].kva  = 0.0f;
 *       power_result[p].pf   = 0.0f;
 *       // kwh is left as-is (or optionally freeze)
 *   }
 *
 * 3. Usage example (console or init):
 *    ads131m08_set_phase_mode(POWER_MODE_SINGLE);   // 단상
 *    ads131m08_set_phase_mode(POWER_MODE_THREE);    // 3상
 */
