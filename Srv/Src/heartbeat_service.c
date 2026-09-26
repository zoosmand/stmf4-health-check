/**
  ******************************************************************************
  * @file           : heartbeat_service.c
  * @brief          : Human-like status LED heartbeat service.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 25.09.2026
  ******************************************************************************
  */

#include "heartbeat_service.h"

#include "FreeRTOS.h"
#include "task.h"

#define HEARTBEAT_LED_PORT             GPIOE
#define HEARTBEAT_LED_PIN              13U
#define HEARTBEAT_TASK_STACK_DEPTH     128U
#define HEARTBEAT_FIRST_PULSE_MS       120U
#define HEARTBEAT_INTER_PULSE_PAUSE_MS 100U
#define HEARTBEAT_SECOND_PULSE_MS      120U
#define HEARTBEAT_REST_MS              880U
#define HEARTBEAT_PERIOD_MS            (HEARTBEAT_FIRST_PULSE_MS \
  + HEARTBEAT_INTER_PULSE_PAUSE_MS + HEARTBEAT_SECOND_PULSE_MS \
  + HEARTBEAT_REST_MS)

static StaticTask_t heartbeatTaskControlBlock;
static StackType_t heartbeatTaskStack[HEARTBEAT_TASK_STACK_DEPTH];

static void heartbeatService_SetLed(uint8_t enabled) {
  /* The JZ-F407VET6 user LEDs are open-drain and active-low. */
  Platform_GpioWrite(
    HEARTBEAT_LED_PORT, HEARTBEAT_LED_PIN, enabled == 0U ? 1U : 0U
  );
}

static void heartbeatService_Task(void* argument) {
  (void)argument;
  TickType_t periodStarted = xTaskGetTickCount();

  for (;;) {
    heartbeatService_SetLed(1U);
    vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_FIRST_PULSE_MS));
    heartbeatService_SetLed(0U);
    vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_INTER_PULSE_PAUSE_MS));
    heartbeatService_SetLed(1U);
    vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_SECOND_PULSE_MS));
    heartbeatService_SetLed(0U);
    vTaskDelayUntil(&periodStarted, pdMS_TO_TICKS(HEARTBEAT_PERIOD_MS));
  }
}

ErrorStatus HeartbeatService_Init(void) {
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOEEN;
  (void)RCC->AHB1ENR;
  heartbeatService_SetLed(0U);
  HEARTBEAT_LED_PORT->OTYPER |= 1UL << HEARTBEAT_LED_PIN;
  Platform_GpioConfigure(
    HEARTBEAT_LED_PORT,
    HEARTBEAT_LED_PIN,
    PLATFORM_GPIO_MODE_OUTPUT,
    PLATFORM_GPIO_PULL_NONE,
    PLATFORM_GPIO_SPEED_LOW,
    0U
  );

  TaskHandle_t task = xTaskCreateStatic(
    heartbeatService_Task,
    "heartbeat",
    HEARTBEAT_TASK_STACK_DEPTH,
    NULL,
    tskIDLE_PRIORITY + 1U,
    heartbeatTaskStack,
    &heartbeatTaskControlBlock
  );
  return task != NULL ? SUCCESS : ERROR;
}
