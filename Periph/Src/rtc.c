/**
  ******************************************************************************
  * @file           : rtc.c
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

#include "rtc.h"

#define RTC_SYNC_MARKER        0x52544353UL
#define RTC_SECONDS_PER_DAY    86400UL
#define RTC_UNIX_YEAR_2000     946684800UL
#define RTC_UNIX_YEAR_2100     4102444800UL

static RTC_HandleTypeDef rtc;

static uint8_t rtc_IsLeapYear(uint16_t year) {
  return ((year % 4U) == 0U)
    && (((year % 100U) != 0U) || ((year % 400U) == 0U));
}

static uint8_t rtc_DaysInMonth(uint16_t year, uint8_t month) {
  static const uint8_t days[] = {
    31U, 28U, 31U, 30U, 31U, 30U,
    31U, 31U, 30U, 31U, 30U, 31U
  };
  if ((month == 2U) && rtc_IsLeapYear(year))
    return 29U;
  return days[month - 1U];
}

HAL_StatusTypeDef Rtc_Init(void) {
  rtc.Instance = RTC;
  rtc.Init.HourFormat = RTC_HOURFORMAT_24;
  rtc.Init.AsynchPrediv = 127U;
  rtc.Init.SynchPrediv = 255U;
  rtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  rtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  rtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  return HAL_RTC_Init(&rtc);
}

HAL_StatusTypeDef Rtc_SetUnixTime(uint32_t unixTime) {
  if ((unixTime < RTC_UNIX_YEAR_2000)
      || (unixTime >= RTC_UNIX_YEAR_2100))
    return HAL_ERROR;

  uint32_t days = unixTime / RTC_SECONDS_PER_DAY;
  uint32_t seconds = unixTime % RTC_SECONDS_PER_DAY;
  uint16_t year = 1970U;

  while (days >= (rtc_IsLeapYear(year) ? 366UL : 365UL)) {
    days -= rtc_IsLeapYear(year) ? 366UL : 365UL;
    ++year;
  }

  uint8_t month = 1U;
  while (days >= rtc_DaysInMonth(year, month)) {
    days -= rtc_DaysInMonth(year, month);
    ++month;
  }

  RTC_TimeTypeDef time = {
    .Hours = (uint8_t)(seconds / 3600UL),
    .Minutes = (uint8_t)((seconds % 3600UL) / 60UL),
    .Seconds = (uint8_t)(seconds % 60UL),
    .DayLightSaving = RTC_DAYLIGHTSAVING_NONE,
    .StoreOperation = RTC_STOREOPERATION_RESET,
  };
  RTC_DateTypeDef date = {
    .WeekDay = (uint8_t)(((unixTime / RTC_SECONDS_PER_DAY) + 3UL) % 7UL + 1UL),
    .Month = month,
    .Date = (uint8_t)(days + 1UL),
    .Year = (uint8_t)(year - 2000U),
  };

  if (HAL_RTC_SetTime(&rtc, &time, RTC_FORMAT_BIN) != HAL_OK)
    return HAL_ERROR;
  if (HAL_RTC_SetDate(&rtc, &date, RTC_FORMAT_BIN) != HAL_OK)
    return HAL_ERROR;

  HAL_RTCEx_BKUPWrite(&rtc, RTC_BKP_DR0, RTC_SYNC_MARKER);
  return HAL_OK;
}

HAL_StatusTypeDef Rtc_GetDateTime(Rtc_DateTimeTypeDef* dateTime) {
  if (dateTime == NULL)
    return HAL_ERROR;

  RTC_TimeTypeDef time;
  RTC_DateTypeDef date;
  if (HAL_RTC_GetTime(&rtc, &time, RTC_FORMAT_BIN) != HAL_OK)
    return HAL_ERROR;
  if (HAL_RTC_GetDate(&rtc, &date, RTC_FORMAT_BIN) != HAL_OK)
    return HAL_ERROR;

  dateTime->year = 2000U + date.Year;
  dateTime->month = date.Month;
  dateTime->day = date.Date;
  dateTime->hour = time.Hours;
  dateTime->minute = time.Minutes;
  dateTime->second = time.Seconds;
  return HAL_OK;
}

uint8_t Rtc_IsSynchronized(void) {
  return HAL_RTCEx_BKUPRead(&rtc, RTC_BKP_DR0) == RTC_SYNC_MARKER;
}
