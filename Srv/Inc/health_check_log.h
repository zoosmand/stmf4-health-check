/**
  ******************************************************************************
  * @file           : health_check_log.h
  * @brief          : Wear-aware persistent ring of recent health-check results.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 01.08.2026
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

#ifndef HEALTH_CHECK_LOG_H
#define HEALTH_CHECK_LOG_H

#include "main.h"
#include "tls_transport.h"

#include <stddef.h>
#include <stdint.h>

#define HEALTH_CHECK_LOG_MAX_RESULTS 10U

/**
  * @brief One retained health-check result.
  * @param sequence (uint32_t) Monotonically increasing record number; higher
  *        is more recent.
  * @param timestampUnix (uint32_t) RTC UTC time at the moment of the check.
  * @param elapsedMs (uint32_t) Check duration in milliseconds.
  * @param detail (int32_t) Layer-specific diagnostic code; see
  *        TlsTransport_ResultTypeDef.detail.
  * @param httpStatus (uint16_t) HTTP status received, or 0 if none.
  * @param resourceIndex (uint8_t) Configured resource slot this check ran
  *        against.
  * @param status (uint8_t) TlsTransport_StatusTypeDef value for this check.
  */
typedef struct {
  uint32_t sequence;
  uint32_t timestampUnix;
  uint32_t elapsedMs;
  int32_t detail;
  uint16_t httpStatus;
  uint8_t resourceIndex;
  uint8_t status;
} HealthCheckLog_EntryTypeDef;

HAL_StatusTypeDef HealthCheckLog_Init(void);

/**
  * @brief Append one check result to the persistent ring.
  * @param resourceIndex (uint8_t) Configured resource slot this check ran
  *        against.
  * @param result (const TlsTransport_ResultTypeDef*) Non-null check result.
  * @retval (HAL_StatusTypeDef) HAL_OK when the record was written and
  *         verified.
  */
HAL_StatusTypeDef HealthCheckLog_Append(
  uint8_t resourceIndex,
  const TlsTransport_ResultTypeDef* result
);

/**
  * @brief Copy out the most recent records, newest first.
  * @param entries (HealthCheckLog_EntryTypeDef*) Non-null output array.
  * @param capacity (size_t) Available elements in entries, at most
  *        HEALTH_CHECK_LOG_MAX_RESULTS are ever meaningfully returned.
  * @retval (size_t) Number of records copied.
  */
size_t HealthCheckLog_GetRecent(
  HealthCheckLog_EntryTypeDef* entries,
  size_t capacity
);

#endif /* HEALTH_CHECK_LOG_H */
