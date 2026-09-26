/**
  ******************************************************************************
  * @file           : factory_reset_service.c
  * @brief          : Recoverable physical-button factory reset service.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 25.09.2026
  ******************************************************************************
  */

#include "factory_reset_service.h"

#include "FreeRTOS.h"
#include "buzzer_service.h"
#include "flash_layout.h"
#include "task.h"
#include "w25q64.h"

#include <stddef.h>
#include <stdio.h>

#define FACTORY_RESET_BUTTON_PORT          GPIOE
#define FACTORY_RESET_BUTTON_PIN           10U
#define FACTORY_RESET_TASK_STACK_DEPTH     128U
#define FACTORY_RESET_HOLD_MS              10000U
#define FACTORY_RESET_CANCEL_WINDOW_MS     10000U
#define FACTORY_RESET_DOUBLE_CLICK_MS      600U
#define FACTORY_RESET_DEBOUNCE_MS          30U
#define FACTORY_RESET_POLL_MS              20U
#define FACTORY_RESET_MARKER_MAGIC         0x46525354UL
#define FACTORY_RESET_VERIFY_CHUNK_SIZE     64U

typedef struct {
  uint32_t magic;
  uint32_t inverseMagic;
} FactoryReset_MarkerTypeDef;

static StaticTask_t factoryResetTaskControlBlock;
static StackType_t factoryResetTaskStack[FACTORY_RESET_TASK_STACK_DEPTH];

static uint8_t factoryReset_ButtonPressed(void) {
  return Platform_GpioRead(
    FACTORY_RESET_BUTTON_PORT, FACTORY_RESET_BUTTON_PIN
  ) == 0U;
}

static uint8_t factoryReset_WaitForButton(
  uint8_t pressed,
  uint32_t timeoutMs
) {
  uint32_t started = Platform_GetTick();
  while ((Platform_GetTick() - started) < timeoutMs) {
    if (factoryReset_ButtonPressed() == pressed) {
      vTaskDelay(pdMS_TO_TICKS(FACTORY_RESET_DEBOUNCE_MS));
      if (factoryReset_ButtonPressed() == pressed)
        return 1U;
    }
    vTaskDelay(pdMS_TO_TICKS(FACTORY_RESET_POLL_MS));
  }
  return 0U;
}

static uint8_t factoryReset_MarkerValid(void) {
  FactoryReset_MarkerTypeDef marker;
  return (W25Q64_Read(
      FLASH_LAYOUT_FACTORY_RESET_MARKER_SECTOR, &marker, sizeof(marker)
    ) == PLATFORM_STATUS_OK)
    && (marker.magic == FACTORY_RESET_MARKER_MAGIC)
    && (marker.inverseMagic == ~FACTORY_RESET_MARKER_MAGIC);
}

static Platform_StatusTypeDef factoryReset_Arm(void) {
  const FactoryReset_MarkerTypeDef marker = {
    .magic = FACTORY_RESET_MARKER_MAGIC,
    .inverseMagic = ~FACTORY_RESET_MARKER_MAGIC,
  };
  if ((W25Q64_EraseSector(
        FLASH_LAYOUT_FACTORY_RESET_MARKER_SECTOR
      ) != PLATFORM_STATUS_OK)
      || (W25Q64_Program(
        FLASH_LAYOUT_FACTORY_RESET_MARKER_SECTOR, &marker, sizeof(marker)
      ) != PLATFORM_STATUS_OK)) {
    return PLATFORM_STATUS_ERROR;
  }
  return factoryReset_MarkerValid() != 0U
    ? PLATFORM_STATUS_OK
    : PLATFORM_STATUS_ERROR;
}

static Platform_StatusTypeDef factoryReset_VerifyErased(uint32_t address) {
  uint8_t data[FACTORY_RESET_VERIFY_CHUNK_SIZE];
  for (uint32_t offset = 0U; offset < W25Q64_SECTOR_SIZE;
       offset += sizeof(data)) {
    if (W25Q64_Read(address + offset, data, sizeof(data))
        != PLATFORM_STATUS_OK) {
      return PLATFORM_STATUS_ERROR;
    }
    for (size_t index = 0U; index < sizeof(data); ++index) {
      if (data[index] != 0xFFU)
        return PLATFORM_STATUS_ERROR;
    }
    IWDG->KR = 0xAAAAU;
  }
  return PLATFORM_STATUS_OK;
}

static Platform_StatusTypeDef factoryReset_ErasePersistentData(void) {
  for (uint32_t sector = 1U;
       sector <= FLASH_LAYOUT_FACTORY_RESET_DATA_SECTORS;
       ++sector) {
    uint32_t address = W25Q64_CAPACITY_BYTES
      - (sector * W25Q64_SECTOR_SIZE);
    if ((W25Q64_EraseSector(address) != PLATFORM_STATUS_OK)
        || (factoryReset_VerifyErased(address) != PLATFORM_STATUS_OK)) {
      return PLATFORM_STATUS_ERROR;
    }
    IWDG->KR = 0xAAAAU;
  }
  return W25Q64_EraseSector(FLASH_LAYOUT_FACTORY_RESET_MARKER_SECTOR);
}

Platform_StatusTypeDef FactoryResetService_Recover(void) {
  if (factoryReset_MarkerValid() == 0U)
    return PLATFORM_STATUS_OK;
  printf("Factory reset: resuming interrupted reset.\r\n");
  return factoryReset_ErasePersistentData();
}

static uint8_t factoryReset_CancelRequested(uint32_t windowStarted) {
  uint32_t elapsed = Platform_GetTick() - windowStarted;
  if (elapsed >= FACTORY_RESET_CANCEL_WINDOW_MS)
    return 0U;
  if (factoryReset_WaitForButton(
        0U, FACTORY_RESET_CANCEL_WINDOW_MS - elapsed
      ) == 0U)
    return 0U;

  elapsed = Platform_GetTick() - windowStarted;
  if (elapsed >= FACTORY_RESET_CANCEL_WINDOW_MS)
    return 0U;
  if (factoryReset_WaitForButton(
        1U, FACTORY_RESET_CANCEL_WINDOW_MS - elapsed
      ) == 0U)
    return 0U;
  uint32_t firstClick = Platform_GetTick();

  elapsed = firstClick - windowStarted;
  if (elapsed >= FACTORY_RESET_CANCEL_WINDOW_MS)
    return 0U;
  uint32_t secondClickTimeout = FACTORY_RESET_CANCEL_WINDOW_MS - elapsed;
  if (secondClickTimeout > FACTORY_RESET_DOUBLE_CLICK_MS)
    secondClickTimeout = FACTORY_RESET_DOUBLE_CLICK_MS;
  if (factoryReset_WaitForButton(
        0U, secondClickTimeout
      ) == 0U)
    return 0U;
  uint32_t clickElapsed = Platform_GetTick() - firstClick;
  if (clickElapsed >= FACTORY_RESET_DOUBLE_CLICK_MS)
    return 0U;
  uint32_t remaining = FACTORY_RESET_DOUBLE_CLICK_MS - clickElapsed;
  elapsed = Platform_GetTick() - windowStarted;
  if (elapsed >= FACTORY_RESET_CANCEL_WINDOW_MS)
    return 0U;
  uint32_t windowRemaining = FACTORY_RESET_CANCEL_WINDOW_MS - elapsed;
  if (remaining > windowRemaining)
    remaining = windowRemaining;
  return factoryReset_WaitForButton(1U, remaining);
}

static void factoryReset_Execute(void) {
  if (W25Q64_Lock() != PLATFORM_STATUS_OK) {
    printf("Factory reset: unable to lock persistent storage.\r\n");
    return;
  }
  vTaskSuspendAll();
  if (factoryReset_Arm() != PLATFORM_STATUS_OK) {
    (void)xTaskResumeAll();
    W25Q64_Unlock();
    printf("Factory reset: unable to write recovery marker.\r\n");
    return;
  }
  if (factoryReset_ErasePersistentData() != PLATFORM_STATUS_OK) {
    /* The verified marker remains; reboot and let startup retry safely. */
    NVIC_SystemReset();
  }
  NVIC_SystemReset();
}

static void factoryReset_Task(void* argument) {
  (void)argument;

  for (;;) {
    if (factoryReset_WaitForButton(1U, portMAX_DELAY) == 0U)
      continue;
    uint32_t holdStarted = Platform_GetTick();
    for (;;) {
      if ((Platform_GetTick() - holdStarted) >= FACTORY_RESET_HOLD_MS)
        break;
      if (factoryReset_ButtonPressed() == 0U) {
        vTaskDelay(pdMS_TO_TICKS(FACTORY_RESET_DEBOUNCE_MS));
        if (factoryReset_ButtonPressed() == 0U)
          break;
      }
      vTaskDelay(pdMS_TO_TICKS(FACTORY_RESET_POLL_MS));
    }
    if ((Platform_GetTick() - holdStarted) < FACTORY_RESET_HOLD_MS)
      continue;

    printf("Factory reset: armed; five-beep warning.\r\n");
    if (BuzzerService_FactoryResetWarning() != SUCCESS) {
      printf("Factory reset: warning failed; reset aborted.\r\n");
      continue;
    }
    uint32_t windowStarted = Platform_GetTick();
    printf("Factory reset: double-click S1 within 10 seconds to cancel.\r\n");
    if (factoryReset_CancelRequested(windowStarted) != 0U) {
      printf("Factory reset: cancelled.\r\n");
      if (BuzzerService_FactoryResetCancelled() != SUCCESS)
        printf("Factory reset: cancellation acknowledgement failed.\r\n");
      continue;
    }
    while ((Platform_GetTick() - windowStarted)
           < FACTORY_RESET_CANCEL_WINDOW_MS) {
      vTaskDelay(pdMS_TO_TICKS(FACTORY_RESET_POLL_MS));
    }
    printf("Factory reset: erasing persistent configuration.\r\n");
    factoryReset_Execute();
  }
}

ErrorStatus FactoryResetService_Init(void) {
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOEEN;
  (void)RCC->AHB1ENR;
  Platform_GpioConfigure(
    FACTORY_RESET_BUTTON_PORT,
    FACTORY_RESET_BUTTON_PIN,
    PLATFORM_GPIO_MODE_INPUT,
    PLATFORM_GPIO_PULL_UP,
    PLATFORM_GPIO_SPEED_LOW,
    0U
  );
  TaskHandle_t task = xTaskCreateStatic(
    factoryReset_Task,
    "factory-reset",
    FACTORY_RESET_TASK_STACK_DEPTH,
    NULL,
    tskIDLE_PRIORITY + 1U,
    factoryResetTaskStack,
    &factoryResetTaskControlBlock
  );
  return task != NULL ? SUCCESS : ERROR;
}
