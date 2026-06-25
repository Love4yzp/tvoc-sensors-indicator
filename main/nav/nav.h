#ifndef NAV_H
#define NAV_H

#include "lvgl.h"

/* Tile indices for the main swipeable screens */
#define NAV_TILE_HA_DATA  0   /* sensor data view */
#define NAV_TILE_HA_CTRL  1   /* switch control view */
#define NAV_TILE_HA_MIX   2   /* mixed sensors+switches view */
#define NAV_TILE_COUNT    3

int      nav_init(void);
lv_obj_t *nav_get_tile(int tile_idx);   /* returns the container for that tile */
void     nav_go_tile(int tile_idx);     /* programmatic navigation */

#endif /* NAV_H */
