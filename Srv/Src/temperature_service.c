/**
  ******************************************************************************
  * @file           : temperature_service.c
  * @brief          : Periodic DS18B20 discovery and measurement service.
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

#include "temperature_service.h"
#include "FreeRTOS.h"
#include "ds18b20.h"
#include "onewire.h"
#include "task.h"

#define TEMPERATURE_TASK_STACK_DEPTH 256U
#define TEMPERATURE_PERIOD_MS        7000U
#define ONEWIRE_SEARCH_PERIOD_MS     60000U

static StaticTask_t temperatureTaskControlBlock;
static StackType_t temperatureTaskStack[TEMPERATURE_TASK_STACK_DEPTH];
static DS18B20_MeasurementTypeDef measurements[ONEWIRE_MAX_DEVICES];
static uint8_t measurementCount;

static void temperatureService_Task(void* argument);

ErrorStatus TemperatureService_Init(void) {
  if (OneWire_Init() != SUCCESS)
    return ERROR;

  TaskHandle_t taskHandle = xTaskCreateStatic(
    temperatureService_Task,
    "temperature",
    TEMPERATURE_TASK_STACK_DEPTH,
    NULL,
    tskIDLE_PRIORITY + 1U,
    temperatureTaskStack,
    &temperatureTaskControlBlock
  );
  return (taskHandle != NULL) ? SUCCESS : ERROR;
}

static void temperatureService_Task(void* argument) {
  (void)argument;
  TickType_t lastSearchTick = 0U;
  TickType_t lastWakeTick = xTaskGetTickCount();

  for (;;) {
    TickType_t now = xTaskGetTickCount();
    if ((OneWire_GetDeviceCount() == 0U)
        || ((now - lastSearchTick)
            >= pdMS_TO_TICKS(ONEWIRE_SEARCH_PERIOD_MS))) {
      (void)OneWire_Search();
      lastSearchTick = now;
    }

    (void)DS18B20_Measure(
      measurements,
      ONEWIRE_MAX_DEVICES,
      &measurementCount
    );
    vTaskDelayUntil(
      &lastWakeTick,
      pdMS_TO_TICKS(TEMPERATURE_PERIOD_MS)
    );
  }
}
