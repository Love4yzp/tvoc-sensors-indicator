#ifndef SENSOR_MODEL_H
#define SENSOR_MODEL_H

#include <stdint.h>
#include <sys/types.h>

#include "rp2040.h"
#include "view_data.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SENSOR_OK               0
#define SENSOR_ERR_INVALID_TYPE -1
#define SENSOR_ERR_MUTEX_FAIL   -2

#ifdef VIEW_DEBUG
extern const char* enum sensor_data_typeStrings[];
#endif

typedef enum
{
	SENSOR_STATUS_OK,
	SENSOR_STATUS_INITED,
	SENSOR_STATUS_WARNING,
	SENSOR_STATUS_ERROR,
} SensorStatus;

typedef struct
{
	float value; // 传感器的测量值
	enum sensor_data_type type; // 传感器的类型
	SensorStatus status; // 传感器的当前状态
	// bool         timeValid; // 时间戳是否有效
	// uint64_t     timeStamp; // 传感器数据的时间戳
} SensorData;

void indicator_sensor_init(void);
int update_sensor_data(const enum sensor_data_type type, uint8_t* p_data);
SensorData* get_current_sensor_data(SensorData* data, enum sensor_data_type type);
const char* get_sensor_name(const enum sensor_data_type type);
float get_sensor_float_value(const enum sensor_data_type type);
int get_sensor_int_value(const enum sensor_data_type type);
int _sensor_data_parse_handle(uint8_t* p_data, ssize_t len);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* SENSOR_MODEL_H */
