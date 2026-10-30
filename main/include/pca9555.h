#pragma once

#include "project.h"

#ifdef __cplusplus
extern "C" {
#endif

// PCA9555PWR ports (16-bit expander)
// Mapping from hardware:
// Port0.0-5 : PCA_10V_SEL1~6  (voltage input resistance select: 0=49.9k, 1=99.8k per side)
// Port0.6-7 : PCA_DI_SEL1~2
// Port1.0-5 : PCA_CUR_SEL1~6  (current termination select: 0=49.9ohm, 1=open)
// Port1.6-7 : PCA_DI_SEL3~4
typedef enum {
    PCA_10V_SEL1 = 0,
    PCA_10V_SEL2,
    PCA_10V_SEL3,
    PCA_10V_SEL4,
    PCA_10V_SEL5,
    PCA_10V_SEL6,
    PCA_DI_SEL1,
    PCA_DI_SEL2,
    PCA_CUR_SEL1,
    PCA_CUR_SEL2,
    PCA_CUR_SEL3,
    PCA_CUR_SEL4,
    PCA_CUR_SEL5,
    PCA_CUR_SEL6,
    PCA_DI_SEL3,
    PCA_DI_SEL4,
    PCA_PORT_COUNT
} pca9555_port_t;

typedef enum {
	COMMON_SEL1 = 1,
	COMMON_SEL2,
	COMMON_SEL3,
	COMMON_SEL4,
	COMMON_SEL5,
	COMMON_SEL6
} common_sel_t;

typedef enum {
    DI_SEL_DIGITAL = 0,
    DI_SEL_ANALOG  = 1
} di_sel_t;

typedef enum {
    CV_SEL_CURRENT = 0,
    CV_SEL_VOLTAGE = 1
} cv_sel_t;

typedef enum {
    R_SEL_49K9  = 0,
    R_SEL_99K8  = 1
} r_sel_t;

void pca9555_init(void);
void pca9555_start_task(void);

// All select relays off (safe default)
void pca9555_deselect_all(void);

// Individual pin control (0 or 1)
void pca9555_set_pin(pca9555_port_t pin, int level);

// Convenience for common channel configuration
// common 1~2 use 10V_SEL + CUR_SEL; common 3~6 also use DI_SEL
// term_49r9: 1 = 49.9ohm termination (CUR_SEL=0), 0 = open (CUR_SEL=1)
// r_49k9:    1 = 49.9k input R (10V_SEL=0), 0 = 99.8k (10V_SEL=1)
// use_adc:   for common3-6 only, 1 = route to ADC, 0 = pulse input
void pca9555_config_common(common_sel_t common_sel, di_sel_t di_sel, cv_sel_t cv_sel, r_sel_t r_sel);

#ifdef __cplusplus
}
#endif
