/**
 * @file rp2040.h
 * @date  12 December 2023

 * @author Spencer Yan
 *
 * @note Description of the file
 *
 * @copyright © 2023, Seeed Studio
 */

#ifndef RP2040_H
#define RP2040_H

#ifdef __cplusplus
extern "C" {
#endif

#include "view_data.h"
enum pkt_type_cmd
{
	PKT_TYPE_NONE = 0,
	PKT_TYPE_CMD_COLLECT_INTERVAL = 0xA0, // uin32_t
	PKT_TYPE_CMD_BEEP_ON = 0xA1, // uin32_t  ms: on time
	PKT_TYPE_CMD_BEEP_OFF = 0xA2,
	PKT_TYPE_CMD_SHUTDOWN = 0xA3,
	PKT_TYPE_CMD_POWER_ON = 0xA4,
	PKT_TYPE_CMD_RESCAN_GROVE = 0xA5,
};

enum pkt_type_data
{
	// Dynamic sensor registry (Grove sensors)
	PKT_TYPE_SENSOR_ATTACHED = 0xB8,
	PKT_TYPE_SENSOR_DETACHED = 0xB9,
	PKT_TYPE_SENSOR_VALUE    = 0xBA,

	// SEN54: PM + humidity + temperature + VOC (replaces all legacy sensors)
	PKT_TYPE_SENSOR_SEN54_PM1_0       = 0xC0,  // float, µg/m³
	PKT_TYPE_SENSOR_SEN54_PM2_5       = 0xC1,  // float, µg/m³
	PKT_TYPE_SENSOR_SEN54_PM4_0       = 0xC2,  // float, µg/m³
	PKT_TYPE_SENSOR_SEN54_PM10        = 0xC3,  // float, µg/m³
	PKT_TYPE_SENSOR_SEN54_HUMIDITY    = 0xC4,  // float, %RH
	PKT_TYPE_SENSOR_SEN54_TEMPERATURE = 0xC5,  // float, °C
	PKT_TYPE_SENSOR_SEN54_VOC_INDEX   = 0xC6,  // float, VOC index 1-500

#ifdef LEGACY_SENSORS
	// Legacy: SCD41 CO2 / SHT41 temp+humidity / SGP40 tVOC
	PKT_TYPE_SENSOR_SCD41_CO2        = 0xB2,
	PKT_TYPE_SENSOR_SHT41_TEMP       = 0xB3,
	PKT_TYPE_SENSOR_SHT41_HUMIDITY   = 0xB4,
	PKT_TYPE_SENSOR_SGP40_TVOC_INDEX = 0xB5,
	// Legacy: SEN5x dynamic-registry protocol
	PKT_TYPE_SENSOR_SEN5X_massConcentrationPm1p0  = 0xB6,
	PKT_TYPE_SENSOR_SEN5X_massConcentrationPm2p5  = 0xB7,
	PKT_TYPE_SENSOR_SEN5X_ambientTemperature      = 0xBB,
	PKT_TYPE_SENSOR_SEN5X_vocIndex                = 0xBC,
	PKT_TYPE_SENSOR_SEN5X_noxIndex                = 0xBD,
	// Legacy: SFA3X formaldehyde sensor
	PKT_TYPE_SENSOR_SFA3X_HCHO     = 0xBE,
	PKT_TYPE_SENSOR_SFA3X_HUMIDITY = 0xBF,
#endif /* LEGACY_SENSORS */
};

#define PKT_SENSOR_ID_AHT20_TEMP 0
#define PKT_SENSOR_ID_AHT20_HUMIDITY 1
#define PKT_SENSOR_ID_SCD41_CO2 2
#define PKT_SENSOR_ID_SGP40_VOC 3
#define PKT_SENSOR_ID_SCD41_TEMP 4
#define PKT_SENSOR_ID_SCD41_HUMIDITY 5
#define PKT_SENSOR_ID_GROVE_BASE 0x10

#define PKT_SENSOR_CAT_TEMP 0
#define PKT_SENSOR_CAT_HUMIDITY 1
#define PKT_SENSOR_CAT_CO2 2
#define PKT_SENSOR_CAT_VOC 3
#define PKT_SENSOR_CAT_LIGHT 4
#define PKT_SENSOR_CAT_PRESSURE 5
#define PKT_SENSOR_CAT_PM 6
#define PKT_SENSOR_CAT_NOX 7
#define PKT_SENSOR_CAT_UNKNOWN 255

void esp32_rp2040_init(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* RP2040_H */
