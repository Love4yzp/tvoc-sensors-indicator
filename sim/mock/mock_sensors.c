/**
 * Mock sensor + WiFi data layer.
 *
 * Injects events via the synchronous esp_event stub so the UI renders
 * without physical hardware. All values oscillate slowly so you can
 * visually verify the UI updates correctly.
 *
 * Event delivery:
 *   VIEW_EVENT_SENSOR_DATA  — 1 Hz, all 4 built-in sensor types
 *   VIEW_EVENT_WIFI_ST      — once at startup (connected)
 */
#include "mock_sensors.h"
#include "view_data.h"
#include "view_data_types.h"
#include "sensor_model_stub.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ── Oscillation helpers ─────────────────────────────────────────────────── */

/* t in seconds (float), returns value oscillating between lo and hi */
static float osc(float t, float lo, float hi, float period) {
    return lo + (hi - lo) * 0.5f * (1.0f + sinf(2.0f * 3.14159265f * t / period));
}

/* ── State ───────────────────────────────────────────────────────────────── */

static uint32_t s_last_sensor_tick = 0;
static bool     s_wifi_posted      = false;

/* ── WiFi ────────────────────────────────────────────────────────────────── */

static void post_wifi_status(void) {
    struct view_data_wifi_st st = {
        .is_connected = true,
        .is_connecting = false,
        .is_network = true,
        .rssi = -55,
    };
    strncpy(st.ssid, "SimWiFi-5G", sizeof(st.ssid) - 1);

    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST,
                      &st, sizeof(st), portMAX_DELAY);
    printf("[mock] wifi: connected (ssid=%s rssi=%d)\n", st.ssid, st.rssi);
}

/* ── Sensors ─────────────────────────────────────────────────────────────── */

static void post_sensor(enum sensor_data_type type, float value) {
    struct view_data_sensor_data d = { .sensor_type = type, .value = value };
    sim_sensor_set_value(type, value);
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SENSOR_DATA,
                      &d, sizeof(d), portMAX_DELAY);
}

static void tick_sensors(void) {
    float t = SDL_GetTicks() / 1000.0f;   /* seconds since init */

    post_sensor(SCD41_SENSOR_CO2,      osc(t, 400.0f,  1200.0f, 30.0f));
    post_sensor(SGP40_SENSOR_TVOC,     osc(t,   0.0f,   300.0f, 45.0f));
    post_sensor(SHT41_SENSOR_TEMP,     osc(t,  18.0f,    32.0f, 60.0f));
    post_sensor(SHT41_SENSOR_HUMIDITY, osc(t,  30.0f,    80.0f, 50.0f));
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void mock_sensors_start(void) {
    printf("[mock] sensors: starting (1 Hz sensor events, wifi at startup)\n");
    /* First tick fires immediately */
    s_last_sensor_tick = 0;
    s_wifi_posted = false;
}

void mock_sensors_tick(void) {
    uint32_t now = SDL_GetTicks();

    if (!s_wifi_posted) {
        post_wifi_status();
        s_wifi_posted = true;
    }

    if (now - s_last_sensor_tick >= 1000) {
        tick_sensors();
        s_last_sensor_tick = now;
    }
}
