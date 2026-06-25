#include "display_model.h"

static bool                    s_on  = true;
static struct view_data_display s_cfg = {
    .brightness         = 80,
    .sleep_mode_en      = false,
    .sleep_mode_time_min = 5,
};

bool indicator_display_st_get(void)          { return s_on; }
int  indicator_display_on(void)              { s_on = true;  return 0; }
int  indicator_display_off(void)             { s_on = false; return 0; }
int  indicator_display_init(void)            { return 0; }
int  indicator_display_sleep_restart(void)   { return 0; }
void _display_cfg_get(struct view_data_display *p) { if (p) *p = s_cfg; }
