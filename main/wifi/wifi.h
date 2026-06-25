#ifndef WIFI_H
#define WIFI_H

/* WiFi domain — vertical slice.
 * Prefer domain headers when only one slice is needed:
 *   wifi_model.h         — WiFi state machine, connection management, ping
 *   wifi_view.h          — UI event subscriptions, status icons, screen init
 *   wifi_list_screen.h   — AP list component
 *   wifi_connect_screen.h — connect/details dialog component
 */
#include "wifi_model.h"
#include "wifi_view.h"

#endif /* WIFI_H */
