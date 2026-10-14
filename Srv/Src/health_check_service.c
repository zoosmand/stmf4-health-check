/**
  ******************************************************************************
  * @file           : health_check_service.c
  * @brief          : Periodic HTTPS resource health-check service.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 30.07.2026
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

#include "health_check_service.h"

#include "FreeRTOS.h"
#include "buzzer_service.h"
#include "callback_service.h"
#include "health_check_config.h"
#include "health_check_log.h"
#include "lwip.h"
#include "rtc.h"
#include "task.h"
#include "tls_transport.h"

#include <stdio.h>

#define HEALTH_CHECK_TASK_STACK_DEPTH 2048U
#define HEALTH_CHECK_WAIT_MS          1000U
#define HEALTH_CHECK_OK_STATUS        200U
#define CERTIFICATE_WARNING_WINDOW_SECONDS (10UL * 24UL * 60UL * 60UL)
#define CERTIFICATE_WARNING_PERIOD_SECONDS (60UL * 60UL)

static StaticTask_t healthCheckTaskControlBlock;
static StackType_t healthCheckTaskStack[HEALTH_CHECK_TASK_STACK_DEPTH];
static uint32_t certificateWarningExpiry[HEALTH_CHECK_CONFIG_MAX_RESOURCES];
static uint32_t certificateWarningTime[HEALTH_CHECK_CONFIG_MAX_RESOURCES];

static uint8_t healthCheckService_CheckCertificateExpiry(
  uint8_t resourceIndex,
  uint32_t certificateExpiry,
  uint32_t* warningTime
) {
  if (certificateExpiry == 0U)
    return 0U;
  uint32_t now;
  if (Rtc_GetUnixTime(&now) != PLATFORM_STATUS_OK)
    return 0U;

  if (certificateWarningExpiry[resourceIndex] != certificateExpiry) {
    certificateWarningExpiry[resourceIndex] = certificateExpiry;
    certificateWarningTime[resourceIndex] = 0U;
  }
  uint8_t warningWindow = ((certificateExpiry <= now)
      || ((certificateExpiry - now) <= CERTIFICATE_WARNING_WINDOW_SECONDS))
    ? 1U : 0U;
  if (warningWindow == 0U)
    return 0U;
  uint32_t previousWarning = certificateWarningTime[resourceIndex];
  if ((previousWarning != 0U)
      && ((now - previousWarning) < CERTIFICATE_WARNING_PERIOD_SECONDS)) {
    return 0U;
  }
  *warningTime = now;
  return 1U;
}

/**
  * @brief Run one check against a configured resource and record the result.
  * @param index (uint8_t) Configured resource slot.
  * @param resource (const HealthCheckConfig_ResourceTypeDef*) Occupied,
  *        enabled resource to check.
  */
static void healthCheckService_CheckResource(
  uint8_t index,
  const HealthCheckConfig_ResourceTypeDef* resource,
  uint8_t* failedResources,
  uint8_t* expiringCertificates,
  uint32_t warningTimes[HEALTH_CHECK_CONFIG_MAX_RESOURCES]
) {
  if (resource->port == 443U) {
    printf(
      "HTTPS check: https://%s%s\r\n", resource->host, resource->path
    );
  } else {
    printf(
      "HTTPS check: https://%s:%u%s\r\n",
      resource->host,
      (unsigned int)resource->port,
      resource->path
    );
  }
  TlsTransport_ResultTypeDef result;
  TlsTransport_Head(
    resource->host,
    resource->port,
    resource->path,
    resource->trustAnchorId,
    &result
  );
  if (result.status == TLS_TRANSPORT_OK) {
    printf("TLS: %s, %s, certificate valid\r\n",
      result.tlsVersion,
      result.cipherSuite);
    printf("HTTP HEAD: %u, %lu ms\r\n",
      result.httpStatus,
      (unsigned long)result.elapsedMs);
  } else {
    printf("HTTPS failure: stage=%u, detail=%d, %lu ms\r\n",
      (unsigned int)result.status,
      result.detail,
      (unsigned long)result.elapsedMs);
  }

  uint8_t resourceHealthy = ((result.status == TLS_TRANSPORT_OK)
      && (result.httpStatus == HEALTH_CHECK_OK_STATUS))
    ? 1U
    : 0U;
  printf(
    "Resource health: %s\r\n", resourceHealthy != 0U ? "OK" : "FAILED"
  );

  uint8_t resourceBit = (uint8_t)(1U << index);
  if (resourceHealthy == 0U) {
    *failedResources |= resourceBit;
  } else if (healthCheckService_CheckCertificateExpiry(
      index, result.certificateNotAfterUnix, &warningTimes[index]) != 0U) {
    *expiringCertificates |= resourceBit;
  }

  HealthCheckLog_EntryTypeDef logEntry;
  Platform_StatusTypeDef logStatus = HealthCheckLog_Append(
    index, resource->trustAnchorId, &result, &logEntry
  );
  if (logStatus != PLATFORM_STATUS_OK) {
    printf("Health check log append failed; callback skipped.\r\n");
  } else if (resourceHealthy == 0U) {
    CallbackService_Enqueue(&logEntry);
  }
}

static void healthCheckService_Task(void* argument) {
  (void)argument;

  for (;;) {
    if ((Lwip_IsReady() == 0U) || (Rtc_IsSynchronized() == 0U)) {
      vTaskDelay(pdMS_TO_TICKS(HEALTH_CHECK_WAIT_MS));
      continue;
    }

    HealthCheckConfig_ResourceTypeDef resources[
      HEALTH_CHECK_CONFIG_MAX_RESOURCES
    ];
    uint8_t failedResources = 0U;
    uint8_t expiringCertificates = 0U;
    uint32_t warningTimes[HEALTH_CHECK_CONFIG_MAX_RESOURCES] = { 0U };
    HealthCheckConfig_GetResources(resources);
    for (uint8_t index = 0U; index < HEALTH_CHECK_CONFIG_MAX_RESOURCES; ++index) {
      if ((resources[index].occupied == 0U)
          || (resources[index].enabled == 0U))
        continue;
      healthCheckService_CheckResource(
        index,
        &resources[index],
        &failedResources,
        &expiringCertificates,
        warningTimes
      );
    }

    ErrorStatus alertStatus = ((failedResources | expiringCertificates) != 0U)
      ? BuzzerService_QueueAlerts(failedResources, expiringCertificates)
      : SUCCESS;
    if ((expiringCertificates != 0U) && (alertStatus == SUCCESS)) {
      for (uint8_t index = 0U;
           index < HEALTH_CHECK_CONFIG_MAX_RESOURCES;
           ++index) {
        if ((expiringCertificates & (uint8_t)(1U << index)) == 0U)
          continue;
        certificateWarningTime[index] = warningTimes[index];
        printf(
          "Certificate expiry warning: resource=%u expires=%lu.\r\n",
          (unsigned int)index,
          (unsigned long)certificateWarningExpiry[index]
        );
      }
    } else if (expiringCertificates != 0U) {
      printf("Certificate expiry warning scheduling failed.\r\n");
    }

    if ((failedResources != 0U) && (alertStatus != SUCCESS))
      printf("Buzzer alert scheduling failed.\r\n");

    vTaskDelay(pdMS_TO_TICKS(
      HealthCheckConfig_GetPeriodSeconds() * 1000U
    ));
  }
}

ErrorStatus HealthCheckService_Init(void) {
  TaskHandle_t task = xTaskCreateStatic(
    healthCheckService_Task,
    "health-check",
    HEALTH_CHECK_TASK_STACK_DEPTH,
    NULL,
    tskIDLE_PRIORITY + 1U,
    healthCheckTaskStack,
    &healthCheckTaskControlBlock
  );
  return task != NULL ? SUCCESS : ERROR;
}
