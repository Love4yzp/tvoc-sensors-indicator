#ifndef DISPLAY_MODEL_H
#define DISPLAY_MODEL_H

#include "view_data.h"

#ifdef __cplusplus
extern "C" {
#endif

struct indicator_display {
	struct view_data_display cfg;
	bool timer_st;
	bool display_st;
};

int indicator_display_init(void);

bool indicator_display_st_get(void);

int indicator_display_on(void);

int indicator_display_off(void);

int indicator_display_sleep_restart(void);

void _display_cfg_get(struct view_data_display* p_data);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_MODEL_H */
