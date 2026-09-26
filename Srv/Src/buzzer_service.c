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
#include "semphr.h"
#include "task.h"

#include <stdio.h>

#define BUZZER_SERVICE_TASK_STACK_DEPTH 128U
#define BUZZER_SELF_TEST_DURATION_MS    100U
#define BUZZER_ALERT_TONE_DURATION_MS   180U
#define BUZZER_ALERT_PAUSE_DURATION_MS  140U
#define BUZZER_ALERT_TONE_COUNT         3U
#define BUZZER_RESET_WARNING_TONE_COUNT 5U
#define BUZZER_RESET_WARNING_TIMEOUT_MS 5000U

static StaticTask_t buzzerTaskControlBlock;
static StackType_t buzzerTaskStack[BUZZER_SERVICE_TASK_STACK_DEPTH];
static TaskHandle_t buzzerTask;
static StaticSemaphore_t synchronousCompletionControlBlock;
static SemaphoreHandle_t synchronousCompletion;
static uint8_t synchronousRequestActive;
static uint8_t resetWarningRequested;

static void buzzerService_Tone(uint32_t durationMs) {
  if (Buzzer_Start() != PLATFORM_STATUS_OK)
    Error_Handler();
  vTaskDelay(pdMS_TO_TICKS(durationMs));
  if (Buzzer_Stop() != PLATFORM_STATUS_OK)
    Error_Handler();
}

static void buzzerService_Task(void* argument) {
  (void)argument;

  printf("Buzzer self-test: started.\r\n");
  buzzerService_Tone(BUZZER_SELF_TEST_DURATION_MS);
  printf("Buzzer self-test: completed.\r\n");
  for (;;) {
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    taskENTER_CRITICAL();
    uint8_t synchronous = synchronousRequestActive;
    uint8_t toneCount = resetWarningRequested != 0U
      ? BUZZER_RESET_WARNING_TONE_COUNT
      : BUZZER_ALERT_TONE_COUNT;
    if (synchronous != 0U)
      resetWarningRequested = 0U;
    taskEXIT_CRITICAL();
    for (uint8_t tone = 0U; tone < toneCount; ++tone) {
      buzzerService_Tone(BUZZER_ALERT_TONE_DURATION_MS);
      if ((tone + 1U) < toneCount)
        vTaskDelay(pdMS_TO_TICKS(BUZZER_ALERT_PAUSE_DURATION_MS));
    }
    if (synchronous != 0U) {
      taskENTER_CRITICAL();
      synchronousRequestActive = 0U;
      taskEXIT_CRITICAL();
      (void)xSemaphoreGive(synchronousCompletion);
    }
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

ErrorStatus BuzzerService_Alert(void) {
  if (buzzerTask == NULL)
    return ERROR;
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
