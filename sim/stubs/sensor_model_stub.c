#include "sensor_model.h"
#include <string.h>

static SensorData s_data[ENUM_SENSOR_ALL];

void indicator_sensor_init(void) {
    memset(s_data, 0, sizeof(s_data));
}

SensorData *get_current_sensor_data(SensorData *dst, enum sensor_data_type type) {
    if (dst && type < ENUM_SENSOR_ALL) *dst = s_data[type];
    return dst;
}

float get_sensor_float_value(const enum sensor_data_type type) {
    return (type < ENUM_SENSOR_ALL) ? s_data[type].value : 0.0f;
}

int get_sensor_int_value(const enum sensor_data_type type) {
    return (int)get_sensor_float_value(type);
}

const char *get_sensor_name(const enum sensor_data_type type) {
#define X(t, s) case t: return s;
    switch (type) { SENSOR_TYPE_LIST default: return "?"; }
#undef X
}

int update_sensor_data(const enum sensor_data_type type, uint8_t *p_data) {
    (void)type; (void)p_data; return 0;
}

int _sensor_data_parse_handle(uint8_t *p_data, ssize_t len) {
    (void)p_data; (void)len; return 0;
}

/* Called by mock layer to inject values into the stub's backing store */
void sim_sensor_set_value(enum sensor_data_type type, float value) {
    if (type < ENUM_SENSOR_ALL) {
        s_data[type].value  = value;
        s_data[type].type   = type;
        s_data[type].status = SENSOR_STATUS_OK;
    }
}
