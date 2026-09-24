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

volatile uint32_t rs232ReceiveInterruptCount;
volatile uint32_t rs232ReceiveErrorCount;
volatile uint8_t rs232LastReceivedByte;

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

  HAL_NVIC_SetPriority(USART1_IRQn, 6U, 0U);
  HAL_NVIC_EnableIRQ(USART1_IRQn);
  __HAL_UART_ENABLE_IT(&rs232Uart, UART_IT_RXNE);

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

void Rs232_HandleInterrupt(void) {
  uint32_t status = USART1->SR;
  const uint32_t errorMask = USART_SR_ORE
    | USART_SR_NE
    | USART_SR_FE
    | USART_SR_PE;

  if ((status & (USART_SR_RXNE | errorMask)) == 0U)
    return;

  /* Reading SR followed by DR clears RXNE and the receive error flags. */
  uint8_t receivedByte = (uint8_t)USART1->DR;

  if ((status & USART_SR_RXNE) != 0U) {
    rs232LastReceivedByte = receivedByte;
    rs232ReceiveInterruptCount++;
  }

  if ((status & errorMask) != 0U)
    rs232ReceiveErrorCount++;
}
