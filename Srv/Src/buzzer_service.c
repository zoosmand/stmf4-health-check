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
#include "task.h"

#include <stdio.h>

#define BUZZER_SERVICE_TASK_STACK_DEPTH 128U
#define BUZZER_SELF_TEST_DURATION_MS    100U
#define BUZZER_ALERT_TONE_DURATION_MS   180U
#define BUZZER_ALERT_PAUSE_DURATION_MS  140U
#define BUZZER_ALERT_TONE_COUNT         3U

static StaticTask_t buzzerTaskControlBlock;
static StackType_t buzzerTaskStack[BUZZER_SERVICE_TASK_STACK_DEPTH];
static TaskHandle_t buzzerTask;

static void buzzerService_Tone(uint32_t durationMs) {
  if (Buzzer_Start() != HAL_OK)
    Error_Handler();
  vTaskDelay(pdMS_TO_TICKS(durationMs));
  if (Buzzer_Stop() != HAL_OK)
    Error_Handler();
}

static void buzzerService_Task(void* argument) {
  (void)argument;

  printf("Buzzer self-test: started.\r\n");
  buzzerService_Tone(BUZZER_SELF_TEST_DURATION_MS);
  printf("Buzzer self-test: completed.\r\n");
  for (;;) {
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    for (uint8_t tone = 0U; tone < BUZZER_ALERT_TONE_COUNT; ++tone) {
      buzzerService_Tone(BUZZER_ALERT_TONE_DURATION_MS);
      if ((tone + 1U) < BUZZER_ALERT_TONE_COUNT)
        vTaskDelay(pdMS_TO_TICKS(BUZZER_ALERT_PAUSE_DURATION_MS));
    }
  }
}

ErrorStatus BuzzerService_Init(void) {
  if (Buzzer_Init() != HAL_OK)
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
