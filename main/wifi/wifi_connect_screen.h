#ifndef WIFI_CONNECT_SCREEN_H
#define WIFI_CONNECT_SCREEN_H

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wifi_connect_screen wifi_connect_screen_t;

/* Invoked (in the LVGL context) whenever the dialog is dismissed — via Cancel,
 * Delete, Join or a programmatic dismiss. The owner uses it to drop its handle
 * so it never dereferences the freed dialog or blocks reopening. */
typedef void (*wifi_connect_screen_dismiss_cb_t)(void);

/* Show a connect dialog for an unconnected AP (modal overlay).
 * Returns NULL if one is already open.                              */
wifi_connect_screen_t *wifi_connect_screen_show(const char *ssid, bool have_password,
                                                wifi_connect_screen_dismiss_cb_t on_dismiss);

/* Show a details dialog for the currently-connected AP (Delete/Cancel). */
wifi_connect_screen_t *wifi_details_screen_show(const char *ssid,
                                                wifi_connect_screen_dismiss_cb_t on_dismiss);

/* Dismiss programmatically (safe to call if already dismissed). */
void wifi_connect_screen_dismiss(wifi_connect_screen_t *s);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_CONNECT_SCREEN_H */
