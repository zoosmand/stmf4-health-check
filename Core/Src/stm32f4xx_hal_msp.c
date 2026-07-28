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
