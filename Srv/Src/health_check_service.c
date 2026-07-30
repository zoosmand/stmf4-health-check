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
#include "lwip.h"
#include "rtc.h"
#include "task.h"
#include "tls_transport.h"

#include <stdio.h>

#define HEALTH_CHECK_TASK_STACK_DEPTH 2048U
#define HEALTH_CHECK_PERIOD_MS        60000U
#define HEALTH_CHECK_WAIT_MS          1000U
#define HEALTH_CHECK_HOST             "pgw.intraclear.com"
#define HEALTH_CHECK_PORT             443U
#define HEALTH_CHECK_RESOURCE         "/"
#define HEALTH_CHECK_OK_STATUS        200U

static StaticTask_t healthCheckTaskControlBlock;
static StackType_t healthCheckTaskStack[HEALTH_CHECK_TASK_STACK_DEPTH];

static void healthCheckService_Task(void* argument) {
  (void)argument;

  for (;;) {
    if ((Lwip_IsReady() == 0U) || (Rtc_IsSynchronized() == 0U)) {
      vTaskDelay(pdMS_TO_TICKS(HEALTH_CHECK_WAIT_MS));
      continue;
    }

    printf("HTTPS check: https://" HEALTH_CHECK_HOST "\r\n");
    TlsTransport_ResultTypeDef result;
    TlsTransport_Head(
      HEALTH_CHECK_HOST,
      HEALTH_CHECK_PORT,
      HEALTH_CHECK_RESOURCE,
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

    printf("Resource health: %s\r\n",
      ((result.status == TLS_TRANSPORT_OK)
        && (result.httpStatus == HEALTH_CHECK_OK_STATUS))
        ? "OK"
        : "FAILED");

    vTaskDelay(pdMS_TO_TICKS(HEALTH_CHECK_PERIOD_MS));
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
