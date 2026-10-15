/**
  ******************************************************************************
  * @file           : buzzer_service.c
  * @brief          : Non-blocking audible alert service.
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

#include "buzzer_service.h"

#include "FreeRTOS.h"
#include "buzzer.h"
#include "health_check_config.h"
#include "semphr.h"
#include "task.h"

#include <stdio.h>

#define BUZZER_SERVICE_TASK_STACK_DEPTH 128U
#define BUZZER_SELF_TEST_DURATION_MS    100U
#define BUZZER_ALERT_TONE_DURATION_MS   180U
#define BUZZER_ALERT_PAUSE_DURATION_MS  140U
#define BUZZER_RESET_WARNING_TONE_COUNT 5U
#define BUZZER_RESET_WARNING_TIMEOUT_MS 5000U
#define BUZZER_MELODY_NOTE_DURATION_MS   120U
#define BUZZER_MELODY_PAUSE_DURATION_MS  60U
#define BUZZER_SOS_UNIT_MS               75U
#define BUZZER_PATTERN_TO_IDS_PAUSE_MS   1000U
#define BUZZER_RESOURCE_IDS_PAUSE_MS     1000U

static StaticTask_t buzzerTaskControlBlock;
static StackType_t buzzerTaskStack[BUZZER_SERVICE_TASK_STACK_DEPTH];
static TaskHandle_t buzzerTask;
static StaticSemaphore_t synchronousCompletionControlBlock;
static SemaphoreHandle_t synchronousCompletion;
static uint8_t synchronousRequestActive;
static uint8_t resetWarningRequested;
static uint8_t alertResourceMask;
static uint8_t certificateWarningMask;

static void buzzerService_Tone(uint16_t frequencyHz, uint32_t durationMs) {
  if (Buzzer_SetFrequency(frequencyHz) != PLATFORM_STATUS_OK)
    Error_Handler();
  if (Buzzer_Start() != PLATFORM_STATUS_OK)
    Error_Handler();
  vTaskDelay(pdMS_TO_TICKS(durationMs));
  if (Buzzer_Stop() != PLATFORM_STATUS_OK)
    Error_Handler();
}

static void buzzerService_PlayCount(uint8_t toneCount) {
  for (uint8_t tone = 0U; tone < toneCount; ++tone) {
    buzzerService_Tone(
      BUZZER_DEFAULT_FREQUENCY_HZ, BUZZER_ALERT_TONE_DURATION_MS
    );
    if ((tone + 1U) < toneCount)
      vTaskDelay(pdMS_TO_TICKS(BUZZER_ALERT_PAUSE_DURATION_MS));
  }
}

static void buzzerService_PlayCertificateSignal(void) {
  static const uint16_t melody[] = { 1600U, 2100U, 2800U };
  for (uint8_t note = 0U; note < (sizeof(melody) / sizeof(melody[0])); ++note) {
    buzzerService_Tone(melody[note], BUZZER_MELODY_NOTE_DURATION_MS);
    if ((note + 1U) < (sizeof(melody) / sizeof(melody[0])))
      vTaskDelay(pdMS_TO_TICKS(BUZZER_MELODY_PAUSE_DURATION_MS));
  }
}

static void buzzerService_PlaySosSignal(void) {
  static const uint8_t durationUnits[] = {
    1U, 1U, 1U, 3U, 3U, 3U, 1U, 1U, 1U
  };
  for (uint8_t symbol = 0U;
       symbol < (sizeof(durationUnits) / sizeof(durationUnits[0]));
       ++symbol) {
    buzzerService_Tone(
      BUZZER_DEFAULT_FREQUENCY_HZ,
      (uint32_t)durationUnits[symbol] * BUZZER_SOS_UNIT_MS
    );
    if ((symbol + 1U) < (sizeof(durationUnits) / sizeof(durationUnits[0]))) {
      uint32_t pauseUnits = ((symbol == 2U) || (symbol == 5U)) ? 3U : 1U;
      vTaskDelay(pdMS_TO_TICKS(pauseUnits * BUZZER_SOS_UNIT_MS));
    }
  }
}

static void buzzerService_PlayResourceIds(uint8_t resourceMask) {
  uint8_t played = 0U;
  for (uint8_t resourceIndex = 0U;
       resourceIndex < HEALTH_CHECK_CONFIG_MAX_RESOURCES;
       ++resourceIndex) {
    if ((resourceMask & (uint8_t)(1U << resourceIndex)) == 0U)
      continue;
    if (played != 0U)
      vTaskDelay(pdMS_TO_TICKS(BUZZER_RESOURCE_IDS_PAUSE_MS));
    buzzerService_PlayCount(resourceIndex + 1U);
    played = 1U;
  }
}

static void buzzerService_Task(void* argument) {
  (void)argument;

  printf("Buzzer self-test: started.\r\n");
  buzzerService_Tone(
    BUZZER_DEFAULT_FREQUENCY_HZ, BUZZER_SELF_TEST_DURATION_MS
  );
  printf("Buzzer self-test: completed.\r\n");
  for (;;) {
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    taskENTER_CRITICAL();
    uint8_t synchronous = synchronousRequestActive;
    uint8_t resetWarning = resetWarningRequested;
    uint8_t alerts = 0U;
    uint8_t certificateWarnings = 0U;
    if (synchronous != 0U) {
      resetWarningRequested = 0U;
    } else {
      alerts = alertResourceMask;
      certificateWarnings = certificateWarningMask;
      alertResourceMask = 0U;
      certificateWarningMask = 0U;
    }
    taskEXIT_CRITICAL();

    if (synchronous != 0U) {
      buzzerService_PlayCount(resetWarning != 0U
        ? BUZZER_RESET_WARNING_TONE_COUNT
        : 3U);
    } else if ((alerts != 0U) || (certificateWarnings != 0U)) {
      /* Start both signatures within three seconds when both classes exist. */
      if (certificateWarnings != 0U)
        buzzerService_PlayCertificateSignal();
      if (alerts != 0U)
        buzzerService_PlaySosSignal();
      vTaskDelay(pdMS_TO_TICKS(BUZZER_PATTERN_TO_IDS_PAUSE_MS));
      /* Keep failed-resource IDs directly associated with the preceding SOS. */
      if (alerts != 0U)
        buzzerService_PlayResourceIds(alerts);
      if ((alerts != 0U) && (certificateWarnings != 0U))
        vTaskDelay(pdMS_TO_TICKS(BUZZER_RESOURCE_IDS_PAUSE_MS));
      if (certificateWarnings != 0U)
        buzzerService_PlayResourceIds(certificateWarnings);
    }

    if (synchronous != 0U) {
      taskENTER_CRITICAL();
      synchronousRequestActive = 0U;
      taskEXIT_CRITICAL();
      (void)xSemaphoreGive(synchronousCompletion);
    }
    taskENTER_CRITICAL();
    uint8_t morePending = ((synchronousRequestActive != 0U)
        || (certificateWarningMask != 0U) || (alertResourceMask != 0U))
      ? 1U : 0U;
    taskEXIT_CRITICAL();
    if (morePending != 0U)
      xTaskNotifyGive(buzzerTask);
  }
}

ErrorStatus BuzzerService_Init(void) {
  if (Buzzer_Init() != PLATFORM_STATUS_OK)
    return ERROR;
  synchronousCompletion = xSemaphoreCreateBinaryStatic(
    &synchronousCompletionControlBlock
  );
  if (synchronousCompletion == NULL)
    return ERROR;
  buzzerTask = xTaskCreateStatic(
    buzzerService_Task,
    "buzzer",
    BUZZER_SERVICE_TASK_STACK_DEPTH,
    NULL,
    tskIDLE_PRIORITY + 1U,
    buzzerTaskStack,
    &buzzerTaskControlBlock
  );
  return buzzerTask != NULL ? SUCCESS : ERROR;
}

ErrorStatus BuzzerService_Alert(uint8_t resourceIndex) {
  if ((buzzerTask == NULL)
      || (resourceIndex >= HEALTH_CHECK_CONFIG_MAX_RESOURCES))
    return ERROR;
  return BuzzerService_AlertResources((uint8_t)(1U << resourceIndex));
}

ErrorStatus BuzzerService_AlertResources(uint8_t resourceMask) {
  return BuzzerService_QueueAlerts(resourceMask, 0U);
}

ErrorStatus BuzzerService_CertificateExpiryWarning(uint8_t resourceIndex) {
  if ((buzzerTask == NULL)
      || (resourceIndex >= HEALTH_CHECK_CONFIG_MAX_RESOURCES))
    return ERROR;
  return BuzzerService_CertificateExpiryWarnings(
    (uint8_t)(1U << resourceIndex)
  );
}

ErrorStatus BuzzerService_CertificateExpiryWarnings(uint8_t resourceMask) {
  return BuzzerService_QueueAlerts(0U, resourceMask);
}

ErrorStatus BuzzerService_QueueAlerts(
  uint8_t failedMask,
  uint8_t certificateMask
) {
  uint8_t validMask = (uint8_t)((1U << HEALTH_CHECK_CONFIG_MAX_RESOURCES) - 1U);
  if ((buzzerTask == NULL) || ((failedMask | certificateMask) == 0U)
      || (((failedMask | certificateMask) & (uint8_t)~validMask) != 0U))
    return ERROR;
  taskENTER_CRITICAL();
  alertResourceMask |= failedMask;
  certificateWarningMask |= certificateMask;
  taskEXIT_CRITICAL();
  xTaskNotifyGive(buzzerTask);
  return SUCCESS;
}

static ErrorStatus buzzerService_PlayAndWait(uint8_t resetWarning) {
  if (buzzerTask == NULL)
    return ERROR;
  taskENTER_CRITICAL();
  if (synchronousRequestActive != 0U) {
    taskEXIT_CRITICAL();
    return ERROR;
  }
  synchronousRequestActive = 1U;
  resetWarningRequested = resetWarning;
  taskEXIT_CRITICAL();
  (void)xSemaphoreTake(synchronousCompletion, 0U);
  xTaskNotifyGive(buzzerTask);
  return xSemaphoreTake(
    synchronousCompletion, pdMS_TO_TICKS(BUZZER_RESET_WARNING_TIMEOUT_MS)
  ) == pdTRUE ? SUCCESS : ERROR;
}

ErrorStatus BuzzerService_FactoryResetWarning(void) {
  return buzzerService_PlayAndWait(1U);
}

ErrorStatus BuzzerService_FactoryResetCancelled(void) {
  return buzzerService_PlayAndWait(0U);
}
