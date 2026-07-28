/**
  ******************************************************************************
  * @file           : ds18b20.h
  * @brief          : DS18B20 temperature sensor interface.
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

#ifndef DS18B20_H
#define DS18B20_H

#include "main.h"
#include "onewire.h"

/**
  * @brief Result of communicating with one DS18B20 sensor.
  */
typedef enum {
  DS18B20_STATUS_OK = 0U,
  DS18B20_STATUS_BUS,
  DS18B20_STATUS_TIMEOUT,
  DS18B20_STATUS_CRC
} DS18B20_StatusTypeDef;

/**
  * @brief Identity, temperature, and status for one DS18B20.
  * @param rom (uint8_t[8]) Unique one-wire ROM code.
  * @param temperatureCentiDegrees (int16_t) Hundredths of a degree Celsius.
  * @param status (DS18B20_StatusTypeDef) Measurement result.
  */
typedef struct {
  uint8_t rom[8];
  int16_t temperatureCentiDegrees;
  DS18B20_StatusTypeDef status;
} DS18B20_MeasurementTypeDef;

/**
  * @brief Convert and read every discovered DS18B20.
  * @param measurements (DS18B20_MeasurementTypeDef*) Output array.
  * @param capacity (uint8_t) Available array elements.
  * @param count (uint8_t*) Number of results written.
  * @retval (ErrorStatus) SUCCESS when at least one result was written.
  */
ErrorStatus DS18B20_Measure(
  DS18B20_MeasurementTypeDef* measurements,
  uint8_t capacity,
  uint8_t* count
);

#endif /* DS18B20_H */
