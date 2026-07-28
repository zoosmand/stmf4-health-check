/**
  ******************************************************************************
  * @file           : ds18b20.c
  * @brief          : DS18B20 conversion and scratchpad handling.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 28.07.2026
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2017-2026 Dmitry Slobodchikov
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

#include "ds18b20.h"
#include "task.h"
#include <string.h>

#define DS18B20_COMMAND_CONVERT_T       0x44U
#define DS18B20_COMMAND_READ_SCRATCHPAD 0xBEU
#define DS18B20_CONVERSION_TIMEOUT_MS   750U

static DS18B20_StatusTypeDef dS18B20_MeasureDevice(
  OneWireDevice_TypeDef* device
);
static ErrorStatus dS18B20_WaitForConversion(uint32_t timeoutMs);

ErrorStatus DS18B20_Measure(
  DS18B20_MeasurementTypeDef* measurements,
  uint8_t capacity,
  uint8_t* count
) {
  if ((measurements == NULL) || (count == NULL) || (capacity == 0U))
    return ERROR;

  *count = 0U;
  if (OneWire_Lock(portMAX_DELAY) != pdTRUE)
    return ERROR;

  uint8_t deviceCount = OneWire_GetDeviceCount();
  uint8_t readCount = (deviceCount < capacity) ? deviceCount : capacity;
  OneWireDevice_TypeDef* devices = OneWire_GetDevices();

  for (uint8_t index = 0U; index < readCount; index++) {
    memcpy(measurements[index].rom, devices[index].rom, 8U);
    measurements[index].status = dS18B20_MeasureDevice(&devices[index]);
    if (measurements[index].status == DS18B20_STATUS_OK) {
      int16_t raw = (int16_t)(
        ((uint16_t)devices[index].scratchpad[1] << 8U)
        | devices[index].scratchpad[0]
      );
      measurements[index].temperatureCentiDegrees =
        (int16_t)(((int32_t)raw * 100) / 16);
    } else {
      measurements[index].temperatureCentiDegrees = 0;
    }
  }

  *count = readCount;
  OneWire_Unlock();
  return (readCount > 0U) ? SUCCESS : ERROR;
}

static DS18B20_StatusTypeDef dS18B20_MeasureDevice(
  OneWireDevice_TypeDef* device
) {
  uint8_t isParasitic;
  if (OneWire_ReadPowerSupply(device->rom, &isParasitic) != SUCCESS)
    return DS18B20_STATUS_BUS;
  if (OneWire_MatchRom(device->rom) != SUCCESS)
    return DS18B20_STATUS_BUS;

  OneWire_WriteByte(DS18B20_COMMAND_CONVERT_T);
  if (isParasitic != 0U) {
    OneWire_StrongPullupEnable();
    vTaskDelay(pdMS_TO_TICKS(DS18B20_CONVERSION_TIMEOUT_MS));
    OneWire_StrongPullupDisable();
  } else if (dS18B20_WaitForConversion(
               DS18B20_CONVERSION_TIMEOUT_MS
             ) != SUCCESS) {
    return DS18B20_STATUS_TIMEOUT;
  }

  if (OneWire_MatchRom(device->rom) != SUCCESS)
    return DS18B20_STATUS_BUS;
  OneWire_WriteByte(DS18B20_COMMAND_READ_SCRATCHPAD);

  uint8_t crc = 0U;
  for (uint8_t index = 0U; index < 9U; index++) {
    device->scratchpad[index] = OneWire_ReadByte();
    crc = OneWire_CRC8(crc, device->scratchpad[index]);
  }
  return (crc == 0U) ? DS18B20_STATUS_OK : DS18B20_STATUS_CRC;
}

static ErrorStatus dS18B20_WaitForConversion(uint32_t timeoutMs) {
  TickType_t start = xTaskGetTickCount();
  TickType_t timeout = pdMS_TO_TICKS(timeoutMs);
  do {
    if (OneWire_ReadBit() != 0U)
      return SUCCESS;
    vTaskDelay(pdMS_TO_TICKS(1U));
  } while ((xTaskGetTickCount() - start) < timeout);
  return ERROR;
}
