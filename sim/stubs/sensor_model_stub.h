#pragma once
#include "sensor_model.h"

/* Inject a value directly into the stub's backing store.
 * Called by the mock layer; then the mock posts VIEW_EVENT_SENSOR_DATA
 * so the view re-reads via get_sensor_float_value(). */
void sim_sensor_set_value(enum sensor_data_type type, float value);
