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

static uint8_t rtc_ToBcd(uint8_t value) {
  return (uint8_t)(((value / 10U) << 4U) | (value % 10U));
}

static uint8_t rtc_FromBcd(uint8_t value) {
  return (uint8_t)(((value >> 4U) * 10U) + (value & 0x0FU));
}

static void rtc_Unlock(void) {
  RTC->WPR = 0xCAU;
  RTC->WPR = 0x53U;
}

static void rtc_Lock(void) {
  RTC->WPR = 0xFFU;
}

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

Platform_StatusTypeDef Rtc_Init(void) {
  RCC->BDCR = (RCC->BDCR & ~RCC_BDCR_RTCSEL) | RCC_BDCR_RTCSEL_0;
  RCC->BDCR |= RCC_BDCR_RTCEN;
  rtc_Unlock();
  RTC->ISR |= RTC_ISR_INIT;
  while ((RTC->ISR & RTC_ISR_INITF) == 0U) {
  }
  RTC->CR &= ~(RTC_CR_FMT | RTC_CR_OSEL);
  RTC->PRER = (127U << RTC_PRER_PREDIV_A_Pos) | 255U;
  RTC->ISR &= ~RTC_ISR_INIT;
  rtc_Lock();
  return PLATFORM_STATUS_OK;
}

Platform_StatusTypeDef Rtc_SetUnixTime(uint32_t unixTime) {
  if ((unixTime < RTC_UNIX_YEAR_2000)
      || (unixTime >= RTC_UNIX_YEAR_2100))
    return PLATFORM_STATUS_ERROR;

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

  uint8_t hours = (uint8_t)(seconds / 3600UL);
  uint8_t minutes = (uint8_t)((seconds % 3600UL) / 60UL);
  uint8_t secondsValue = (uint8_t)(seconds % 60UL);
  uint8_t weekday = (uint8_t)(
    ((unixTime / RTC_SECONDS_PER_DAY) + 3UL) % 7UL + 1UL
  );
  rtc_Unlock();
  RTC->ISR |= RTC_ISR_INIT;
  while ((RTC->ISR & RTC_ISR_INITF) == 0U) {
  }
  RTC->TR = ((uint32_t)rtc_ToBcd(hours) << RTC_TR_HU_Pos)
    | ((uint32_t)rtc_ToBcd(minutes) << RTC_TR_MNU_Pos)
    | ((uint32_t)rtc_ToBcd(secondsValue) << RTC_TR_SU_Pos);
  RTC->DR = ((uint32_t)rtc_ToBcd((uint8_t)(year - 2000U)) << RTC_DR_YU_Pos)
    | ((uint32_t)weekday << RTC_DR_WDU_Pos)
    | ((uint32_t)rtc_ToBcd(month) << RTC_DR_MU_Pos)
    | ((uint32_t)rtc_ToBcd((uint8_t)(days + 1UL)) << RTC_DR_DU_Pos);
  RTC->ISR &= ~RTC_ISR_INIT;
  rtc_Lock();
  RTC->BKP0R = RTC_SYNC_MARKER;
  return PLATFORM_STATUS_OK;
}

Platform_StatusTypeDef Rtc_GetDateTime(Rtc_DateTimeTypeDef* dateTime) {
  if (dateTime == NULL)
    return PLATFORM_STATUS_ERROR;

  (void)RTC->SSR;
  uint32_t time = RTC->TR;
  uint32_t date = RTC->DR;
  dateTime->year = 2000U + rtc_FromBcd((uint8_t)(date >> RTC_DR_YU_Pos));
  dateTime->month = rtc_FromBcd((uint8_t)(
    (date & (RTC_DR_MT | RTC_DR_MU)) >> RTC_DR_MU_Pos
  ));
  dateTime->day = rtc_FromBcd((uint8_t)(
    (date & (RTC_DR_DT | RTC_DR_DU)) >> RTC_DR_DU_Pos
  ));
  dateTime->hour = rtc_FromBcd((uint8_t)(
    (time & (RTC_TR_HT | RTC_TR_HU)) >> RTC_TR_HU_Pos
  ));
  dateTime->minute = rtc_FromBcd((uint8_t)(
    (time & (RTC_TR_MNT | RTC_TR_MNU)) >> RTC_TR_MNU_Pos
  ));
  dateTime->second = rtc_FromBcd((uint8_t)(
    (time & (RTC_TR_ST | RTC_TR_SU)) >> RTC_TR_SU_Pos
  ));
  return PLATFORM_STATUS_OK;
}

Platform_StatusTypeDef Rtc_GetUnixTime(uint32_t* unixTime) {
  if (unixTime == NULL)
    return PLATFORM_STATUS_ERROR;

  Rtc_DateTimeTypeDef dateTime;
  if (Rtc_GetDateTime(&dateTime) != PLATFORM_STATUS_OK)
    return PLATFORM_STATUS_ERROR;

  uint32_t days = 0U;
  for (uint16_t year = 1970U; year < dateTime.year; ++year)
    days += rtc_IsLeapYear(year) ? 366UL : 365UL;
  for (uint8_t month = 1U; month < dateTime.month; ++month)
    days += rtc_DaysInMonth(dateTime.year, month);
  days += dateTime.day - 1U;

  *unixTime = (days * RTC_SECONDS_PER_DAY)
    + ((uint32_t)dateTime.hour * 3600UL)
    + ((uint32_t)dateTime.minute * 60UL)
    + dateTime.second;
  return PLATFORM_STATUS_OK;
}

uint8_t Rtc_IsSynchronized(void) {
  return RTC->BKP0R == RTC_SYNC_MARKER;
}
