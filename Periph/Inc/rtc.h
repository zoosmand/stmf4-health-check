/**
  ******************************************************************************
  * @file           : rtc.h
  * @brief          : Hardware RTC initialization and UTC date/time access.
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

#ifndef RTC_H
#define RTC_H

#include "main.h"

/**
  * @brief Calendar representation of the RTC value in UTC.
  */
typedef struct {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
} Rtc_DateTimeTypeDef;

HAL_StatusTypeDef Rtc_Init(void);
HAL_StatusTypeDef Rtc_SetUnixTime(uint32_t unixTime);
HAL_StatusTypeDef Rtc_GetUnixTime(uint32_t* unixTime);
HAL_StatusTypeDef Rtc_GetDateTime(Rtc_DateTimeTypeDef* dateTime);
uint8_t Rtc_IsSynchronized(void);

#endif /* RTC_H */
