/**
  ******************************************************************************
  * @file           : rs485.c
  * @brief          : Onboard RS485 interface implementation.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 29.07.2026
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

#include "rs485.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#define RS485_DIRECTION_PORT GPIOD
#define RS485_DIRECTION_PIN  GPIO_PIN_7
#define RS485_TIMEOUT_MS     1000U

static UART_HandleTypeDef rs485Uart;
static StaticSemaphore_t rs485MutexBuffer;
static SemaphoreHandle_t rs485Mutex;
static uint8_t rs485Initialized;

HAL_StatusTypeDef Rs485_Init(void) {
  rs485Uart.Instance = USART2;
  rs485Uart.Init.BaudRate = 115200U;
  rs485Uart.Init.WordLength = UART_WORDLENGTH_8B;
  rs485Uart.Init.StopBits = UART_STOPBITS_1;
  rs485Uart.Init.Parity = UART_PARITY_NONE;
  rs485Uart.Init.Mode = UART_MODE_TX_RX;
  rs485Uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  rs485Uart.Init.OverSampling = UART_OVERSAMPLING_16;

  if (HAL_UART_Init(&rs485Uart) != HAL_OK)
    return HAL_ERROR;

  rs485Mutex = xSemaphoreCreateMutexStatic(&rs485MutexBuffer);
  if (rs485Mutex == NULL)
    return HAL_ERROR;

  HAL_GPIO_WritePin(
    RS485_DIRECTION_PORT,
    RS485_DIRECTION_PIN,
    GPIO_PIN_RESET
  );
  rs485Initialized = 1U;
  return HAL_OK;
}

HAL_StatusTypeDef Rs485_Transmit(const uint8_t* data, size_t length) {
  if ((rs485Initialized == 0U) || (data == NULL) || (rs485Mutex == NULL))
    return HAL_ERROR;

  if (length == 0U)
    return HAL_OK;

  if (length > UINT16_MAX)
    return HAL_ERROR;

  BaseType_t schedulerRunning =
    (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) ? pdTRUE : pdFALSE;
  if ((schedulerRunning == pdTRUE)
      && (xSemaphoreTake(rs485Mutex, portMAX_DELAY) != pdTRUE)) {
    return HAL_ERROR;
  }

  HAL_GPIO_WritePin(
    RS485_DIRECTION_PORT,
    RS485_DIRECTION_PIN,
    GPIO_PIN_SET
  );

  HAL_StatusTypeDef status = HAL_UART_Transmit(
    &rs485Uart,
    data,
    (uint16_t)length,
    RS485_TIMEOUT_MS
  );

  HAL_GPIO_WritePin(
    RS485_DIRECTION_PORT,
    RS485_DIRECTION_PIN,
    GPIO_PIN_RESET
  );

  if (schedulerRunning == pdTRUE)
    (void)xSemaphoreGive(rs485Mutex);

  return status;
}
