/**
  ******************************************************************************
  * @file           : stm32f4xx_hal_msp.c
  * @brief          : HAL low-level clock configuration.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 13.01.2026
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

#include "main.h"

void HAL_MspInit(void) {
  __HAL_RCC_SYSCFG_CLK_ENABLE();
  __HAL_RCC_PWR_CLK_ENABLE();
}

void HAL_CRC_MspInit(CRC_HandleTypeDef* crc) {
  if ((crc != NULL) && (crc->Instance == CRC))
    __HAL_RCC_CRC_CLK_ENABLE();
}

void HAL_CRC_MspDeInit(CRC_HandleTypeDef* crc) {
  if ((crc != NULL) && (crc->Instance == CRC))
    __HAL_RCC_CRC_CLK_DISABLE();
}

void HAL_RTC_MspInit(RTC_HandleTypeDef* rtc) {
  if ((rtc == NULL) || (rtc->Instance != RTC))
    return;

  RCC_PeriphCLKInitTypeDef clock = {
    .PeriphClockSelection = RCC_PERIPHCLK_RTC,
    .RTCClockSelection = RCC_RTCCLKSOURCE_LSE,
  };
  if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK)
    Error_Handler();
  __HAL_RCC_RTC_ENABLE();
}

void HAL_RTC_MspDeInit(RTC_HandleTypeDef* rtc) {
  if ((rtc != NULL) && (rtc->Instance == RTC))
    __HAL_RCC_RTC_DISABLE();
}

void HAL_RNG_MspInit(RNG_HandleTypeDef* randomGenerator) {
  if ((randomGenerator != NULL) && (randomGenerator->Instance == RNG))
    __HAL_RCC_RNG_CLK_ENABLE();
}

void HAL_RNG_MspDeInit(RNG_HandleTypeDef* randomGenerator) {
  if ((randomGenerator != NULL) && (randomGenerator->Instance == RNG))
    __HAL_RCC_RNG_CLK_DISABLE();
}

void HAL_SPI_MspInit(SPI_HandleTypeDef* spi) {
  if ((spi == NULL) || (spi->Instance != SPI2))
    return;

  __HAL_RCC_SPI2_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  GPIO_InitTypeDef gpio = {
    .Mode = GPIO_MODE_AF_PP,
    .Pull = GPIO_NOPULL,
    .Speed = GPIO_SPEED_FREQ_VERY_HIGH,
    .Alternate = GPIO_AF5_SPI2,
  };

  gpio.Pin = GPIO_PIN_10;
  HAL_GPIO_Init(GPIOB, &gpio);

  gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3;
  HAL_GPIO_Init(GPIOC, &gpio);
}

void HAL_SPI_MspDeInit(SPI_HandleTypeDef* spi) {
  if ((spi == NULL) || (spi->Instance != SPI2))
    return;

  __HAL_RCC_SPI2_CLK_DISABLE();
  HAL_GPIO_DeInit(GPIOB, GPIO_PIN_10);
  HAL_GPIO_DeInit(GPIOC, GPIO_PIN_2 | GPIO_PIN_3);
}

void HAL_TIM_PWM_MspInit(TIM_HandleTypeDef* timer) {
  if ((timer == NULL) || (timer->Instance != TIM1))
    return;

  __HAL_RCC_TIM1_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  GPIO_InitTypeDef gpio = {
    .Pin = GPIO_PIN_8,
    .Mode = GPIO_MODE_AF_PP,
    .Pull = GPIO_PULLDOWN,
    .Speed = GPIO_SPEED_FREQ_LOW,
    .Alternate = GPIO_AF1_TIM1,
  };
  HAL_GPIO_Init(GPIOA, &gpio);
}

void HAL_TIM_PWM_MspDeInit(TIM_HandleTypeDef* timer) {
  if ((timer == NULL) || (timer->Instance != TIM1))
    return;

  __HAL_RCC_TIM1_CLK_DISABLE();
  HAL_GPIO_DeInit(GPIOA, GPIO_PIN_8);
}

void HAL_UART_MspInit(UART_HandleTypeDef* uart) {
  if ((uart == NULL) || (uart->Instance != USART2))
    return;

  __HAL_RCC_USART2_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  GPIO_InitTypeDef uartPins = {
    .Pin = GPIO_PIN_5 | GPIO_PIN_6,
    .Mode = GPIO_MODE_AF_PP,
    .Pull = GPIO_PULLUP,
    .Speed = GPIO_SPEED_FREQ_VERY_HIGH,
    .Alternate = GPIO_AF7_USART2,
  };
  HAL_GPIO_Init(GPIOD, &uartPins);
}

void HAL_UART_MspDeInit(UART_HandleTypeDef* uart) {
  if ((uart == NULL) || (uart->Instance != USART2))
    return;

  __HAL_RCC_USART2_CLK_DISABLE();
  HAL_GPIO_DeInit(GPIOD, GPIO_PIN_5 | GPIO_PIN_6);
}
