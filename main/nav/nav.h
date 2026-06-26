#ifndef NAV_H
#define NAV_H

#include "lvgl.h"

/* Tile indices for the main swipeable screens */
#define NAV_TILE_SEN5X    0   /* SEN54 dashboard */
#define NAV_TILE_SETTINGS 1   /* MQTT broker settings (modal trigger) */
#define NAV_TILE_COUNT    2

/* Legacy aliases — kept so old references compile without changes */
#define NAV_TILE_HA_DATA  NAV_TILE_SEN5X
#define NAV_TILE_HA_CTRL  NAV_TILE_SETTINGS
#define NAV_TILE_HA_MIX   NAV_TILE_SETTINGS

int      nav_init(void);
lv_obj_t *nav_get_tile(int tile_idx);   /* returns the container for that tile */
void     nav_go_tile(int tile_idx);     /* programmatic navigation */

#endif /* NAV_H */
