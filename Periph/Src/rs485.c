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

#define RS485_DIRECTION_PORT GPIOD
#define RS485_DIRECTION_PIN  7U
#define RS485_TIMEOUT_MS     1000U

static uint8_t rs485Initialized;

Platform_StatusTypeDef Rs485_Init(void) {
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;
  RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
  (void)RCC->APB1ENR;
  Platform_GpioWrite(RS485_DIRECTION_PORT, RS485_DIRECTION_PIN, 0U);
  Platform_GpioConfigure(GPIOD, 7U, 1U, 0U, 3U, 0U);
  Platform_GpioConfigure(GPIOD, 5U, 2U, 0U, 3U, 7U);
  Platform_GpioConfigure(GPIOD, 6U, 2U, 0U, 3U, 7U);
  USART2->BRR = 365U;
  USART2->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
  rs485Initialized = 1U;
  return PLATFORM_STATUS_OK;
}

Platform_StatusTypeDef Rs485_Transmit(const uint8_t* data, size_t length) {
  if ((rs485Initialized == 0U) || (data == NULL))
    return PLATFORM_STATUS_ERROR;

  if (length == 0U)
    return PLATFORM_STATUS_OK;

  if (length > UINT16_MAX)
    return PLATFORM_STATUS_ERROR;

  Platform_GpioWrite(RS485_DIRECTION_PORT, RS485_DIRECTION_PIN, 1U);
  uint32_t started = Platform_GetTick();
  Platform_StatusTypeDef status = PLATFORM_STATUS_OK;
  for (size_t offset = 0U; offset < length; ++offset) {
    while ((USART2->SR & USART_SR_TXE) == 0U) {
      if ((Platform_GetTick() - started) >= RS485_TIMEOUT_MS) {
        status = PLATFORM_STATUS_TIMEOUT;
        break;
      }
    }
    if (status != PLATFORM_STATUS_OK)
      break;
    USART2->DR = data[offset];
  }
  while ((status == PLATFORM_STATUS_OK) && ((USART2->SR & USART_SR_TC) == 0U)) {
    if ((Platform_GetTick() - started) >= RS485_TIMEOUT_MS)
      status = PLATFORM_STATUS_TIMEOUT;
  }
  Platform_GpioWrite(RS485_DIRECTION_PORT, RS485_DIRECTION_PIN, 0U);

  return status;
}
