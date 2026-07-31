/**
  ******************************************************************************
  * @file           : health_check_log.c
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
  *
  * Two dedicated sectors are filled round-robin as an append-only log:
  * records are programmed into successive, previously-erased offsets within
  * the active sector (NOR flash page-program never needs an erase as long as
  * the same bytes are never rewritten), and a sector is only erased when it
  * is entirely full and its turn to be reused comes around again. This keeps
  * per-sector erase frequency far below the naive "rewrite the whole
  * snapshot on every change" pattern used by user_store.c / health_check_
  * config.c, which is unsuitable here because a log record can be appended
  * as often as every ~20 seconds.
  *
  ******************************************************************************
  */

#include "health_check_log.h"

#include "FreeRTOS.h"
#include "flash_layout.h"
#include "rtc.h"
#include "semphr.h"
#include "w25q64.h"

#include <stddef.h>
#include <stdio.h>

#define HEALTH_CHECK_LOG_ERASED_SEQUENCE 0xFFFFFFFFUL

typedef struct {
  uint32_t sequence;
  uint32_t timestampUnix;
  uint32_t elapsedMs;
  int32_t detail;
  uint16_t httpStatus;
  uint8_t resourceIndex;
  uint8_t status;
  uint32_t crc;
} healthCheckLog_RecordTypeDef;

#define HEALTH_CHECK_LOG_SLOTS_PER_SECTOR \
  (W25Q64_SECTOR_SIZE / sizeof(healthCheckLog_RecordTypeDef))

static uint8_t activeSector;
static uint16_t nextSlot;
static uint32_t nextSequence;
static StaticSemaphore_t logMutexControlBlock;
static SemaphoreHandle_t logMutex;

static uint32_t healthCheckLog_SectorBase(uint8_t sector) {
  return (sector == 0U)
    ? FLASH_LAYOUT_HEALTH_CHECK_LOG_SECTOR_0
    : FLASH_LAYOUT_HEALTH_CHECK_LOG_SECTOR_1;
}

static uint32_t healthCheckLog_Crc(const void* data, size_t length) {
  const uint8_t* bytes = data;
  uint32_t crc = 0xFFFFFFFFUL;
  while (length-- != 0U) {
    crc ^= *bytes++;
    for (uint8_t bit = 0U; bit < 8U; ++bit)
      crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
  }
  return ~crc;
}

/**
  * @brief Count the valid leading records in a sector.
  * @param sector (uint8_t) 0 or 1.
  * @param count (uint16_t*) Number of valid records in the used prefix.
  * @param nextAvailableSlot (uint16_t*) First erased slot after that prefix.
  * @param lastSequence (uint32_t*) Sequence of the last valid record, or 0.
  */
static void healthCheckLog_ScanSector(
  uint8_t sector,
  uint16_t* count,
  uint16_t* nextAvailableSlot,
  uint32_t* lastSequence
) {
  uint32_t base = healthCheckLog_SectorBase(sector);
  uint16_t valid = 0U;
  uint16_t next = HEALTH_CHECK_LOG_SLOTS_PER_SECTOR;
  uint32_t sequence = 0U;
  for (uint16_t slot = 0U; slot < HEALTH_CHECK_LOG_SLOTS_PER_SECTOR; ++slot) {
    healthCheckLog_RecordTypeDef record;
    uint32_t address = base + ((uint32_t)slot * sizeof(record));
    if (W25Q64_Read(address, &record, sizeof(record)) != HAL_OK) {
      next = slot + 1U;
      continue;
    }
    if (record.sequence == HEALTH_CHECK_LOG_ERASED_SEQUENCE) {
      next = slot;
      break;
    }
    if (record.crc != healthCheckLog_Crc(
          &record, offsetof(healthCheckLog_RecordTypeDef, crc)
        )) {
      /* A torn NOR write cannot be retried without an erase. Skip its slot
         and continue appending at the following erased location. */
      next = slot + 1U;
      continue;
    }
    ++valid;
    next = slot + 1U;
    sequence = record.sequence;
  }
  *count = valid;
  *nextAvailableSlot = next;
  *lastSequence = sequence;
}

static uint8_t healthCheckLog_RollToSector(uint8_t sector) {
  if (W25Q64_EraseSector(healthCheckLog_SectorBase(sector)) != HAL_OK)
    return 0U;
  activeSector = sector;
  nextSlot = 0U;
  return 1U;
}

HAL_StatusTypeDef HealthCheckLog_Init(void) {
  logMutex = xSemaphoreCreateMutexStatic(&logMutexControlBlock);
  if (logMutex == NULL)
    return HAL_ERROR;

  uint16_t count[2];
  uint16_t available[2];
  uint32_t lastSequence[2];
  healthCheckLog_ScanSector(
    0U, &count[0], &available[0], &lastSequence[0]
  );
  healthCheckLog_ScanSector(
    1U, &count[1], &available[1], &lastSequence[1]
  );

  if ((count[0] == 0U) && (count[1] == 0U)) {
    nextSequence = 1U;
    return (healthCheckLog_RollToSector(0U) != 0U) ? HAL_OK : HAL_ERROR;
  }

  uint8_t active = (lastSequence[0] >= lastSequence[1]) ? 0U : 1U;
  if (available[active] < HEALTH_CHECK_LOG_SLOTS_PER_SECTOR) {
    activeSector = active;
    nextSlot = available[active];
    nextSequence = lastSequence[active] + 1U;
    return HAL_OK;
  }

  nextSequence = lastSequence[active] + 1U;
  return (healthCheckLog_RollToSector(active ^ 1U) != 0U) ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef HealthCheckLog_Append(
  uint8_t resourceIndex,
  const TlsTransport_ResultTypeDef* result
) {
  if (result == NULL)
    return HAL_ERROR;
  if (xSemaphoreTake(logMutex, portMAX_DELAY) != pdTRUE)
    return HAL_ERROR;

  if ((nextSlot >= HEALTH_CHECK_LOG_SLOTS_PER_SECTOR)
      && (healthCheckLog_RollToSector(activeSector ^ 1U) == 0U)) {
    (void)xSemaphoreGive(logMutex);
    return HAL_ERROR;
  }

  healthCheckLog_RecordTypeDef record = {
    .sequence = nextSequence,
    .timestampUnix = 0U,
    .elapsedMs = result->elapsedMs,
    .detail = (int32_t)result->detail,
    .httpStatus = result->httpStatus,
    .resourceIndex = resourceIndex,
    .status = (uint8_t)result->status,
    .crc = 0U,
  };
  (void)Rtc_GetUnixTime(&record.timestampUnix);
  record.crc = healthCheckLog_Crc(
    &record, offsetof(healthCheckLog_RecordTypeDef, crc)
  );

  uint32_t address = healthCheckLog_SectorBase(activeSector)
    + ((uint32_t)nextSlot * sizeof(record));
  HAL_StatusTypeDef status = W25Q64_Program(address, &record, sizeof(record));
  if (status == HAL_OK) {
    healthCheckLog_RecordTypeDef verification;
    if ((W25Q64_Read(address, &verification, sizeof(verification)) != HAL_OK)
        || (verification.sequence != record.sequence)
        || (verification.crc != record.crc)) {
      status = HAL_ERROR;
    }
  }
  if (status != HAL_OK)
    printf("Health check log: record write failed; slot skipped.\r\n");

  /* Never retry the same slot: bits already programmed can't be rewritten
     without an erase. The boot scan's CRC check self-heals by treating a
     bad slot (and anything after it) as unwritten on the next reset. */
  ++nextSlot;
  ++nextSequence;
  (void)xSemaphoreGive(logMutex);
  return status;
}

size_t HealthCheckLog_GetRecent(
  HealthCheckLog_EntryTypeDef* entries,
  size_t capacity
) {
  if ((entries == NULL) || (capacity == 0U))
    return 0U;
  if (xSemaphoreTake(logMutex, portMAX_DELAY) != pdTRUE)
    return 0U;

  uint8_t sectors[2] = { activeSector, (uint8_t)(activeSector ^ 1U) };
  uint16_t counts[2];
  uint16_t unusedAvailableSlot;
  uint32_t unusedLastSequence;
  counts[0] = nextSlot;
  healthCheckLog_ScanSector(
    sectors[1],
    &counts[1],
    &unusedAvailableSlot,
    &unusedLastSequence
  );
  counts[1] = unusedAvailableSlot;

  size_t written = 0U;
  for (uint8_t pass = 0U; (pass < 2U) && (written < capacity); ++pass) {
    uint16_t remaining = counts[pass];
    while ((remaining > 0U) && (written < capacity)) {
      --remaining;
      healthCheckLog_RecordTypeDef record;
      uint32_t address = healthCheckLog_SectorBase(sectors[pass])
        + ((uint32_t)remaining * sizeof(record));
      if (W25Q64_Read(address, &record, sizeof(record)) != HAL_OK)
        break;
      if ((record.sequence == HEALTH_CHECK_LOG_ERASED_SEQUENCE)
          || (record.crc != healthCheckLog_Crc(
                &record, offsetof(healthCheckLog_RecordTypeDef, crc)
              ))) {
        continue;
      }
      entries[written].sequence = record.sequence;
      entries[written].timestampUnix = record.timestampUnix;
      entries[written].elapsedMs = record.elapsedMs;
      entries[written].detail = record.detail;
      entries[written].httpStatus = record.httpStatus;
      entries[written].resourceIndex = record.resourceIndex;
      entries[written].status = record.status;
      ++written;
    }
  }

  (void)xSemaphoreGive(logMutex);
  return written;
}
