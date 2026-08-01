/**
  ******************************************************************************
  * @file           : health_check_config.h
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

#ifndef HEALTH_CHECK_CONFIG_H
#define HEALTH_CHECK_CONFIG_H

#include "main.h"

#include <stdint.h>

#define HEALTH_CHECK_CONFIG_MAX_RESOURCES      3U
#define HEALTH_CHECK_CONFIG_HOST_SIZE          64U
#define HEALTH_CHECK_CONFIG_PATH_SIZE          80U
#define HEALTH_CHECK_CONFIG_MIN_PERIOD_SECONDS 60U
#define HEALTH_CHECK_CONFIG_MAX_PERIOD_SECONDS  1800U

typedef enum {
  HEALTH_CHECK_CONFIG_STATUS_OK = 0,
  HEALTH_CHECK_CONFIG_STATUS_INVALID_ARGUMENT,
  HEALTH_CHECK_CONFIG_STATUS_NOT_FOUND,
  HEALTH_CHECK_CONFIG_STATUS_FULL,
  HEALTH_CHECK_CONFIG_STATUS_STORAGE_ERROR
} HealthCheckConfig_StatusTypeDef;

/**
  * @brief One configured HTTPS resource to periodically check.
  * @param occupied (uint8_t) Nonzero when this fixed slot holds a resource.
  * @param enabled (uint8_t) Nonzero when the resource is actively checked.
  * @param trustAnchorId (uint8_t) TLS trust-anchor ID selected for this host.
  * @param port (uint16_t) TCP port, typically 443.
  * @param host (char[HEALTH_CHECK_CONFIG_HOST_SIZE]) Null-terminated hostname.
  * @param path (char[HEALTH_CHECK_CONFIG_PATH_SIZE]) Null-terminated request
  *        path.
  */
typedef struct {
  uint8_t occupied;
  uint8_t enabled;
  uint8_t trustAnchorId;
  uint8_t reserved;
  uint16_t port;
  char host[HEALTH_CHECK_CONFIG_HOST_SIZE];
  char path[HEALTH_CHECK_CONFIG_PATH_SIZE];
} HealthCheckConfig_ResourceTypeDef;

HAL_StatusTypeDef HealthCheckConfig_Init(void);

/**
  * @brief Return the current health-check period.
  * @retval (uint32_t) Period in seconds.
  */
uint32_t HealthCheckConfig_GetPeriodSeconds(void);

/**
  * @brief Persist a new health-check period.
  * @param periodSeconds (uint32_t) Value from HEALTH_CHECK_CONFIG_MIN_PERIOD_SECONDS
  *        through HEALTH_CHECK_CONFIG_MAX_PERIOD_SECONDS.
  * @retval (HealthCheckConfig_StatusTypeDef) OK on success.
  */
HealthCheckConfig_StatusTypeDef HealthCheckConfig_SetPeriodSeconds(
  uint32_t periodSeconds
);

/**
  * @brief Copy out all configured resource slots, including unoccupied ones.
  * @param resources (HealthCheckConfig_ResourceTypeDef*) Output array of
  *        HEALTH_CHECK_CONFIG_MAX_RESOURCES elements.
  */
void HealthCheckConfig_GetResources(
  HealthCheckConfig_ResourceTypeDef resources[HEALTH_CHECK_CONFIG_MAX_RESOURCES]
);

/**
  * @brief Occupy the first free resource slot.
  * @param host (const char*) Non-null, non-empty hostname.
  * @param port (uint16_t) TCP port.
  * @param path (const char*) Non-null request path.
  * @param enabled (uint8_t) Initial enabled state.
  * @param trustAnchorId (uint8_t) Existing TLS trust-anchor ID.
  * @param assignedIndex (uint8_t*) Optional; receives the occupied slot index.
  * @retval (HealthCheckConfig_StatusTypeDef) OK on success, FULL when every
  *         slot is occupied.
  */
HealthCheckConfig_StatusTypeDef HealthCheckConfig_AddResource(
  const char* host,
  uint16_t port,
  const char* path,
  uint8_t enabled,
  uint8_t trustAnchorId,
  uint8_t* assignedIndex
);

/**
  * @brief Replace an occupied resource slot's fields.
  * @param index (uint8_t) Slot index, 0 through HEALTH_CHECK_CONFIG_MAX_RESOURCES-1.
  * @param host (const char*) Non-null, non-empty hostname.
  * @param port (uint16_t) TCP port.
  * @param path (const char*) Non-null request path beginning with '/'.
  * @param enabled (uint8_t) New enabled state.
  * @param trustAnchorId (uint8_t) Existing TLS trust-anchor ID.
  * @retval (HealthCheckConfig_StatusTypeDef) OK on success, NOT_FOUND when the
  *         slot is not occupied.
  */
HealthCheckConfig_StatusTypeDef HealthCheckConfig_UpdateResource(
  uint8_t index,
  const char* host,
  uint16_t port,
  const char* path,
  uint8_t enabled,
  uint8_t trustAnchorId
);

/**
  * @brief Clear a resource slot. A later resource may reuse the slot index.
  * @param index (uint8_t) Slot index to clear.
  * @retval (HealthCheckConfig_StatusTypeDef) OK on success, NOT_FOUND when the
  *         slot was not occupied.
  */
HealthCheckConfig_StatusTypeDef HealthCheckConfig_DeleteResource(
  uint8_t index
);

/**
  * @brief Report whether an occupied resource references a trust anchor.
  * @param trustAnchorId (uint8_t) Anchor ID to find.
  * @retval (uint8_t) Nonzero when at least one resource uses the ID.
  */
uint8_t HealthCheckConfig_IsTrustAnchorInUse(uint8_t trustAnchorId);

/**
  * @brief Reassign every resource to factory trust anchor ID 0.
  * @retval (HealthCheckConfig_StatusTypeDef) Persistence result.
  */
HealthCheckConfig_StatusTypeDef HealthCheckConfig_ResetTrustAnchors(void);

#endif /* HEALTH_CHECK_CONFIG_H */
