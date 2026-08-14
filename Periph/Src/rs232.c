/**
  ******************************************************************************
  * @file           : rs232.c
  * @brief          : Onboard RS232 interface implementation.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 13.08.2026
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

#include "rs232.h"

#define RS232_TIMEOUT_MS 1000U

static UART_HandleTypeDef rs232Uart;
static uint8_t rs232Initialized;

HAL_StatusTypeDef Rs232_Init(void) {
  rs232Uart.Instance = USART1;
  rs232Uart.Init.BaudRate = 115200U;
  rs232Uart.Init.WordLength = UART_WORDLENGTH_8B;
  rs232Uart.Init.StopBits = UART_STOPBITS_1;
  rs232Uart.Init.Parity = UART_PARITY_NONE;
  rs232Uart.Init.Mode = UART_MODE_TX_RX;
  rs232Uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  rs232Uart.Init.OverSampling = UART_OVERSAMPLING_16;

  if (HAL_UART_Init(&rs232Uart) != HAL_OK)
    return HAL_ERROR;

  rs232Initialized = 1U;
  return HAL_OK;
}

HAL_StatusTypeDef Rs232_Transmit(const uint8_t* data, size_t length) {
  if ((rs232Initialized == 0U) || (data == NULL))
    return HAL_ERROR;

  if (length == 0U)
    return HAL_OK;

  if (length > UINT16_MAX)
    return HAL_ERROR;

  return HAL_UART_Transmit(
    &rs232Uart,
    data,
    (uint16_t)length,
    RS232_TIMEOUT_MS
  );
}
