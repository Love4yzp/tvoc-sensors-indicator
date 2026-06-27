#ifndef NAV_H
#define NAV_H

#include "lvgl.h"

/* Single-tile UI: the SEN54 dashboard is the only tile. Settings, Wi-Fi,
 * Display and Broker are modals on lv_layer_top() opened from on-screen
 * buttons — not swipeable tiles. (There used to be a second SETTINGS tile,
 * but it was unreachable: tile 0 has LV_DIR_NONE and nothing navigated to it.) */
#define NAV_TILE_SEN5X    0   /* SEN54 dashboard — the only tile */
#define NAV_TILE_COUNT    1

/* Legacy aliases — all collapse to the single dashboard tile so old/legacy
 * references (Wi-Fi status icon, HA switch screen) keep compiling. Settings is
 * a gear-button modal, not a tile, so it has deliberately no tile constant. */
#define NAV_TILE_HA_DATA  NAV_TILE_SEN5X
#define NAV_TILE_HA_CTRL  NAV_TILE_SEN5X
#define NAV_TILE_HA_MIX   NAV_TILE_SEN5X

int      nav_init(void);
lv_obj_t *nav_get_tile(int tile_idx);   /* returns the container for that tile */
void     nav_go_tile(int tile_idx);     /* programmatic navigation */

#endif /* NAV_H */
