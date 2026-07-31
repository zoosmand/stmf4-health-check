/**
  ******************************************************************************
  * @file           : health_check_config.c
  * @brief          : Persistent health-check period and resource list.
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

#include "health_check_config.h"

#include "FreeRTOS.h"
#include "flash_layout.h"
#include "semphr.h"
#include "w25q64.h"

#include <string.h>

#define HEALTH_CHECK_CONFIG_MAGIC     0x48434643UL
#define HEALTH_CHECK_CONFIG_VERSION   1U
#define HEALTH_CHECK_CONFIG_SECTOR_A  FLASH_LAYOUT_HEALTH_CHECK_CONFIG_SECTOR_A
#define HEALTH_CHECK_CONFIG_SECTOR_B  FLASH_LAYOUT_HEALTH_CHECK_CONFIG_SECTOR_B

#define HEALTH_CHECK_CONFIG_DEFAULT_PERIOD_SECONDS 60U
#define HEALTH_CHECK_CONFIG_DEFAULT_HOST           "pgw.intraclear.com"
#define HEALTH_CHECK_CONFIG_DEFAULT_PORT           443U
#define HEALTH_CHECK_CONFIG_DEFAULT_PATH           "/"

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  uint32_t generation;
  uint32_t periodSeconds;
  HealthCheckConfig_ResourceTypeDef resources[HEALTH_CHECK_CONFIG_MAX_RESOURCES];
  uint32_t crc;
} HealthCheckConfig_SnapshotTypeDef;

static HealthCheckConfig_SnapshotTypeDef snapshot;
static uint32_t activeAddress;
static StaticSemaphore_t configMutexControlBlock;
static SemaphoreHandle_t configMutex;

static uint32_t healthCheckConfig_Crc(const void* data, size_t length) {
  const uint8_t* bytes = data;
  uint32_t crc = 0xFFFFFFFFUL;
  while (length-- != 0U) {
    crc ^= *bytes++;
    for (uint8_t bit = 0U; bit < 8U; ++bit)
      crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
  }
  return ~crc;
}

static uint8_t healthCheckConfig_IsValid(
  const HealthCheckConfig_SnapshotTypeDef* candidate
) {
  return ((candidate->magic == HEALTH_CHECK_CONFIG_MAGIC)
      && (candidate->version == HEALTH_CHECK_CONFIG_VERSION)
      && (candidate->periodSeconds >= HEALTH_CHECK_CONFIG_MIN_PERIOD_SECONDS)
      && (candidate->periodSeconds <= HEALTH_CHECK_CONFIG_MAX_PERIOD_SECONDS)
      && (candidate->crc == healthCheckConfig_Crc(
        candidate, offsetof(HealthCheckConfig_SnapshotTypeDef, crc)
      )))
    ? 1U
    : 0U;
}

static void healthCheckConfig_SetDefault(
  HealthCheckConfig_SnapshotTypeDef* target
) {
  memset(target, 0, sizeof(*target));
  target->periodSeconds = HEALTH_CHECK_CONFIG_DEFAULT_PERIOD_SECONDS;
  target->resources[0].occupied = 1U;
  target->resources[0].enabled = 1U;
  target->resources[0].port = HEALTH_CHECK_CONFIG_DEFAULT_PORT;
  (void)strncpy(
    target->resources[0].host,
    HEALTH_CHECK_CONFIG_DEFAULT_HOST,
    sizeof(target->resources[0].host) - 1U
  );
  (void)strncpy(
    target->resources[0].path,
    HEALTH_CHECK_CONFIG_DEFAULT_PATH,
    sizeof(target->resources[0].path) - 1U
  );
}

static HAL_StatusTypeDef healthCheckConfig_Save(
  HealthCheckConfig_SnapshotTypeDef* candidate
) {
  uint32_t target = (activeAddress == HEALTH_CHECK_CONFIG_SECTOR_A)
    ? HEALTH_CHECK_CONFIG_SECTOR_B
    : HEALTH_CHECK_CONFIG_SECTOR_A;
  candidate->magic = HEALTH_CHECK_CONFIG_MAGIC;
  candidate->version = HEALTH_CHECK_CONFIG_VERSION;
  ++candidate->generation;
  candidate->crc = healthCheckConfig_Crc(
    candidate, offsetof(HealthCheckConfig_SnapshotTypeDef, crc)
  );
  if ((W25Q64_EraseSector(target) != HAL_OK)
      || (W25Q64_Program(target, candidate, sizeof(*candidate)) != HAL_OK))
    return HAL_ERROR;
  /* Static to keep the startup task's stack bounded. */
  static HealthCheckConfig_SnapshotTypeDef verification;
  if ((W25Q64_Read(target, &verification, sizeof(verification)) != HAL_OK)
      || (healthCheckConfig_IsValid(&verification) == 0U)
      || (verification.generation != candidate->generation))
    return HAL_ERROR;
  snapshot = *candidate;
  activeAddress = target;
  return HAL_OK;
}

HAL_StatusTypeDef HealthCheckConfig_Init(void) {
  configMutex = xSemaphoreCreateMutexStatic(&configMutexControlBlock);
  if (configMutex == NULL)
    return HAL_ERROR;
  /* Static to keep the startup task's stack bounded. */
  static HealthCheckConfig_SnapshotTypeDef first;
  static HealthCheckConfig_SnapshotTypeDef second;
  uint8_t firstValid = (W25Q64_Read(
    HEALTH_CHECK_CONFIG_SECTOR_A, &first, sizeof(first)
  ) == HAL_OK) && healthCheckConfig_IsValid(&first);
  uint8_t secondValid = (W25Q64_Read(
    HEALTH_CHECK_CONFIG_SECTOR_B, &second, sizeof(second)
  ) == HAL_OK) && healthCheckConfig_IsValid(&second);
  if ((firstValid != 0U)
      && ((secondValid == 0U) || (first.generation >= second.generation))) {
    snapshot = first;
    activeAddress = HEALTH_CHECK_CONFIG_SECTOR_A;
    return HAL_OK;
  }
  if (secondValid != 0U) {
    snapshot = second;
    activeAddress = HEALTH_CHECK_CONFIG_SECTOR_B;
    return HAL_OK;
  }
  healthCheckConfig_SetDefault(&snapshot);
  activeAddress = HEALTH_CHECK_CONFIG_SECTOR_B;
  return healthCheckConfig_Save(&snapshot);
}

uint32_t HealthCheckConfig_GetPeriodSeconds(void) {
  uint32_t periodSeconds = HEALTH_CHECK_CONFIG_DEFAULT_PERIOD_SECONDS;
  if (xSemaphoreTake(configMutex, portMAX_DELAY) == pdTRUE) {
    periodSeconds = snapshot.periodSeconds;
    (void)xSemaphoreGive(configMutex);
  }
  return periodSeconds;
}

HealthCheckConfig_StatusTypeDef HealthCheckConfig_SetPeriodSeconds(
  uint32_t periodSeconds
) {
  if ((periodSeconds < HEALTH_CHECK_CONFIG_MIN_PERIOD_SECONDS)
      || (periodSeconds > HEALTH_CHECK_CONFIG_MAX_PERIOD_SECONDS))
    return HEALTH_CHECK_CONFIG_STATUS_INVALID_ARGUMENT;
  if (xSemaphoreTake(configMutex, portMAX_DELAY) != pdTRUE)
    return HEALTH_CHECK_CONFIG_STATUS_STORAGE_ERROR;
  HealthCheckConfig_SnapshotTypeDef candidate = snapshot;
  candidate.periodSeconds = periodSeconds;
  HealthCheckConfig_StatusTypeDef status =
    (healthCheckConfig_Save(&candidate) == HAL_OK)
      ? HEALTH_CHECK_CONFIG_STATUS_OK
      : HEALTH_CHECK_CONFIG_STATUS_STORAGE_ERROR;
  (void)xSemaphoreGive(configMutex);
  return status;
}

void HealthCheckConfig_GetResources(
  HealthCheckConfig_ResourceTypeDef resources[HEALTH_CHECK_CONFIG_MAX_RESOURCES]
) {
  if (resources == NULL)
    return;
  if (xSemaphoreTake(configMutex, portMAX_DELAY) == pdTRUE) {
    memcpy(resources, snapshot.resources, sizeof(snapshot.resources));
    (void)xSemaphoreGive(configMutex);
  }
}

HealthCheckConfig_StatusTypeDef HealthCheckConfig_AddResource(
  const char* host,
  uint16_t port,
  const char* path,
  uint8_t enabled,
  uint8_t* assignedIndex
) {
  if ((host == NULL) || (host[0] == '\0') || (path == NULL)
      || (path[0] != '/') || (port == 0U)
      || (strlen(host) >= HEALTH_CHECK_CONFIG_HOST_SIZE)
      || (strlen(path) >= HEALTH_CHECK_CONFIG_PATH_SIZE))
    return HEALTH_CHECK_CONFIG_STATUS_INVALID_ARGUMENT;
  if (xSemaphoreTake(configMutex, portMAX_DELAY) != pdTRUE)
    return HEALTH_CHECK_CONFIG_STATUS_STORAGE_ERROR;
  HealthCheckConfig_SnapshotTypeDef candidate = snapshot;
  uint8_t index;
  for (index = 0U; index < HEALTH_CHECK_CONFIG_MAX_RESOURCES; ++index) {
    if (candidate.resources[index].occupied == 0U)
      break;
  }
  if (index == HEALTH_CHECK_CONFIG_MAX_RESOURCES) {
    (void)xSemaphoreGive(configMutex);
    return HEALTH_CHECK_CONFIG_STATUS_FULL;
  }
  memset(&candidate.resources[index], 0, sizeof(candidate.resources[index]));
  candidate.resources[index].occupied = 1U;
  candidate.resources[index].enabled = enabled ? 1U : 0U;
  candidate.resources[index].port = port;
  (void)strncpy(
    candidate.resources[index].host,
    host,
    sizeof(candidate.resources[index].host) - 1U
  );
  (void)strncpy(
    candidate.resources[index].path,
    path,
    sizeof(candidate.resources[index].path) - 1U
  );
  HealthCheckConfig_StatusTypeDef status =
    (healthCheckConfig_Save(&candidate) == HAL_OK)
      ? HEALTH_CHECK_CONFIG_STATUS_OK
      : HEALTH_CHECK_CONFIG_STATUS_STORAGE_ERROR;
  if ((status == HEALTH_CHECK_CONFIG_STATUS_OK) && (assignedIndex != NULL))
    *assignedIndex = index;
  (void)xSemaphoreGive(configMutex);
  return status;
}

HealthCheckConfig_StatusTypeDef HealthCheckConfig_UpdateResource(
  uint8_t index,
  const char* host,
  uint16_t port,
  const char* path,
  uint8_t enabled
) {
  if ((index >= HEALTH_CHECK_CONFIG_MAX_RESOURCES)
      || (host == NULL) || (host[0] == '\0') || (path == NULL)
      || (path[0] != '/') || (port == 0U)
      || (strlen(host) >= HEALTH_CHECK_CONFIG_HOST_SIZE)
      || (strlen(path) >= HEALTH_CHECK_CONFIG_PATH_SIZE))
    return HEALTH_CHECK_CONFIG_STATUS_INVALID_ARGUMENT;
  if (xSemaphoreTake(configMutex, portMAX_DELAY) != pdTRUE)
    return HEALTH_CHECK_CONFIG_STATUS_STORAGE_ERROR;
  if (snapshot.resources[index].occupied == 0U) {
    (void)xSemaphoreGive(configMutex);
    return HEALTH_CHECK_CONFIG_STATUS_NOT_FOUND;
  }
  HealthCheckConfig_SnapshotTypeDef candidate = snapshot;
  candidate.resources[index].enabled = enabled ? 1U : 0U;
  candidate.resources[index].port = port;
  memset(candidate.resources[index].host, 0, HEALTH_CHECK_CONFIG_HOST_SIZE);
  (void)strncpy(
    candidate.resources[index].host,
    host,
    sizeof(candidate.resources[index].host) - 1U
  );
  memset(candidate.resources[index].path, 0, HEALTH_CHECK_CONFIG_PATH_SIZE);
  (void)strncpy(
    candidate.resources[index].path,
    path,
    sizeof(candidate.resources[index].path) - 1U
  );
  HealthCheckConfig_StatusTypeDef status =
    (healthCheckConfig_Save(&candidate) == HAL_OK)
      ? HEALTH_CHECK_CONFIG_STATUS_OK
      : HEALTH_CHECK_CONFIG_STATUS_STORAGE_ERROR;
  (void)xSemaphoreGive(configMutex);
  return status;
}

HealthCheckConfig_StatusTypeDef HealthCheckConfig_DeleteResource(
  uint8_t index
) {
  if (index >= HEALTH_CHECK_CONFIG_MAX_RESOURCES)
    return HEALTH_CHECK_CONFIG_STATUS_INVALID_ARGUMENT;
  if (xSemaphoreTake(configMutex, portMAX_DELAY) != pdTRUE)
    return HEALTH_CHECK_CONFIG_STATUS_STORAGE_ERROR;
  if (snapshot.resources[index].occupied == 0U) {
    (void)xSemaphoreGive(configMutex);
    return HEALTH_CHECK_CONFIG_STATUS_NOT_FOUND;
  }
  HealthCheckConfig_SnapshotTypeDef candidate = snapshot;
  memset(&candidate.resources[index], 0, sizeof(candidate.resources[index]));
  HealthCheckConfig_StatusTypeDef status =
    (healthCheckConfig_Save(&candidate) == HAL_OK)
      ? HEALTH_CHECK_CONFIG_STATUS_OK
      : HEALTH_CHECK_CONFIG_STATUS_STORAGE_ERROR;
  (void)xSemaphoreGive(configMutex);
  return status;
}
