/**
 * @file main.cpp
 * @author Spencer Yan
 * @brief RP2040 firmware — reads SEN54, sends metrics to ESP32-S3 via COBS/PacketSerial
 * @copyright © 2024, Seeed Studio
 *
 * Product Wiki: https://wiki.seeedstudio.com/Sensor/SenseCAP/SenseCAP_Indicator/
 */

#include "indicator_rp2040.hpp"

PacketSerial packetSerial;

static bool has_sen54 = false;

/************************ recv cmd from esp32 ****************************/

void onPacketReceived(const uint8_t* buffer, size_t size)
{
#if DEBUG
    Serial.printf("<--- recv len:%d, data: ", size);
    for (int i = 0; i < size; i++) {
        Serial.printf("0x%x ", buffer[i]);
    }
    Serial.println("");
#endif
    if (size < 1) {
        return;
    }
    switch (buffer[0]) {
        case PKT_TYPE_CMD_SHUTDOWN:
            Serial.println("cmd shutdown");
            sensor_power_off();
            break;
        case PKT_TYPE_CMD_POWER_ON:
            Serial.println("cmd power on");
            sensor_power_on();
            has_sen54 = sensor_sen54_init();
            break;
        case PKT_TYPE_CMD_BEEP_ON:
            beep_on();
            break;
        case PKT_TYPE_CMD_BEEP_OFF:
            beep_off();
            break;
        case PKT_TYPE_CMD_RESCAN_GROVE:
            Serial.println("cmd rescan grove: no dynamic Grove scanner in this firmware yet");
            break;
        default:
            break;
    }
}

/************************ setup & loop ****************************/

void setup()
{
    Serial.begin(115200);

    Serial1.setRX(17);
    Serial1.setTX(16);
    Serial1.begin(115200);
    packetSerial.setStream(&Serial1);
    packetSerial.setPacketHandler(&onPacketReceived);

    sensor_power_on();

    Wire.setSDA(20);
    Wire.setSCL(21);
    Wire.begin();

    beep_init();

    has_sen54 = sensor_sen54_init();
    if (has_sen54) {
        sensor_attached_send(packetSerial, PKT_SENSOR_ID_SEN54,
                             PKT_SENSOR_CAT_VOC, "SEN54", "idx");
    } else {
        Serial.println("WARNING: SEN54 not found — check I2C wiring");
    }

#ifdef LEGACY_SENSORS
    // Legacy sensor init — disabled, SEN54 replaces SCD41+SGP40+SHT41
    // sensor_aht_init();
    // has_sht41 = sensor_sht41_init();
    // sensor_sgp40_init();
    // sensor_scd4x_init();
#endif /* LEGACY_SENSORS */
}

/************************ timers ****************************/

NonBlockingDelay sen54_delay(1000);  // SEN54 outputs new data every ~1 s

/************************ sensor data ****************************/

SEN5XData data_sen54 = {0};

void loop()
{
    if (sen54_delay.check() && has_sen54 && sensor_sen54_get(data_sen54)) {
        sensor_sen54_print(data_sen54);

        sensor_data_send(packetSerial, PKT_TYPE_SENSOR_SEN54_PM1_0,    data_sen54.pm1p0);
        sensor_data_send(packetSerial, PKT_TYPE_SENSOR_SEN54_PM2_5,    data_sen54.pm2p5);
        sensor_data_send(packetSerial, PKT_TYPE_SENSOR_SEN54_PM4_0,    data_sen54.pm4p0);
        sensor_data_send(packetSerial, PKT_TYPE_SENSOR_SEN54_PM10,     data_sen54.pm10p0);
        sensor_data_send(packetSerial, PKT_TYPE_SENSOR_SEN54_HUMIDITY,    data_sen54.humidity);
        sensor_data_send(packetSerial, PKT_TYPE_SENSOR_SEN54_TEMPERATURE, data_sen54.temperature);
        sensor_data_send(packetSerial, PKT_TYPE_SENSOR_SEN54_VOC_INDEX,   data_sen54.vocIndex);
    }
}

void loop1()
{
    packetSerial.update();
    if (packetSerial.overflow()) {}
}
