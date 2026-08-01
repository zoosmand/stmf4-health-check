/**
  ******************************************************************************
  * @file           : sys_arch.c
  * @brief          : Static FreeRTOS adaptation for the lwIP system API.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 29.07.2026
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

#include "lwip/sys.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#define SYS_ARCH_THREAD_COUNT       2U
#define SYS_ARCH_THREAD_STACK_WORDS 512U

typedef struct {
  StaticTask_t controlBlock;
  StackType_t stack[SYS_ARCH_THREAD_STACK_WORDS];
  uint8_t allocated;
} SysArchThread_TypeDef;

static SysArchThread_TypeDef sysArchThreads[SYS_ARCH_THREAD_COUNT];

void sys_init(void) {
}

u32_t sys_now(void) {
  return (u32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

u32_t sys_jiffies(void) {
  return (u32_t)xTaskGetTickCount();
}

void sys_arch_msleep(u32_t delayMs) {
  TickType_t delay = pdMS_TO_TICKS(delayMs);
  vTaskDelay((delay > 0U) ? delay : 1U);
}

sys_prot_t sys_arch_protect(void) {
  taskENTER_CRITICAL();
  return 1U;
}

void sys_arch_unprotect(sys_prot_t protection) {
  (void)protection;
  taskEXIT_CRITICAL();
}

err_t sys_mutex_new(sys_mutex_t* mutex) {
  if (mutex == NULL)
    return ERR_ARG;

  mutex->handle = xSemaphoreCreateRecursiveMutexStatic(&mutex->storage);
  return (mutex->handle != NULL) ? ERR_OK : ERR_MEM;
}

void sys_mutex_lock(sys_mutex_t* mutex) {
  LWIP_ASSERT("valid mutex", sys_mutex_valid(mutex));
  (void)xSemaphoreTakeRecursive(mutex->handle, portMAX_DELAY);
}

void sys_mutex_unlock(sys_mutex_t* mutex) {
  LWIP_ASSERT("valid mutex", sys_mutex_valid(mutex));
  (void)xSemaphoreGiveRecursive(mutex->handle);
}

void sys_mutex_free(sys_mutex_t* mutex) {
  if (sys_mutex_valid(mutex))
    vSemaphoreDelete(mutex->handle);
  sys_mutex_set_invalid(mutex);
}

err_t sys_sem_new(sys_sem_t* semaphore, u8_t initialCount) {
  if ((semaphore == NULL) || (initialCount > 1U))
    return ERR_ARG;

  semaphore->handle = xSemaphoreCreateBinaryStatic(&semaphore->storage);
  if (semaphore->handle == NULL)
    return ERR_MEM;
  if (initialCount != 0U)
    (void)xSemaphoreGive(semaphore->handle);
  return ERR_OK;
}

void sys_sem_signal(sys_sem_t* semaphore) {
  LWIP_ASSERT("valid semaphore", sys_sem_valid(semaphore));
  (void)xSemaphoreGive(semaphore->handle);
}

u32_t sys_arch_sem_wait(sys_sem_t* semaphore, u32_t timeoutMs) {
  LWIP_ASSERT("valid semaphore", sys_sem_valid(semaphore));
  TickType_t start = xTaskGetTickCount();
  TickType_t timeout = (timeoutMs == 0U)
    ? portMAX_DELAY
    : pdMS_TO_TICKS(timeoutMs);
  if ((timeoutMs != 0U) && (timeout == 0U))
    timeout = 1U;

  if (xSemaphoreTake(semaphore->handle, timeout) != pdTRUE)
    return SYS_ARCH_TIMEOUT;
  return (u32_t)((xTaskGetTickCount() - start) * portTICK_PERIOD_MS);
}

void sys_sem_free(sys_sem_t* semaphore) {
  if (sys_sem_valid(semaphore))
    vSemaphoreDelete(semaphore->handle);
  sys_sem_set_invalid(semaphore);
}

err_t sys_mbox_new(sys_mbox_t* mailbox, int size) {
  if ((mailbox == NULL) || (size <= 0)
      || ((uint32_t)size > SYS_ARCH_MBOX_CAPACITY)) {
    return ERR_ARG;
  }

  mailbox->handle = xQueueCreateStatic(
    (UBaseType_t)size,
    sizeof(void*),
    (uint8_t*)mailbox->messages,
    &mailbox->queue
  );
  return (mailbox->handle != NULL) ? ERR_OK : ERR_MEM;
}

void sys_mbox_post(sys_mbox_t* mailbox, void* message) {
  LWIP_ASSERT("valid mailbox", sys_mbox_valid(mailbox));
  (void)xQueueSendToBack(mailbox->handle, &message, portMAX_DELAY);
}

err_t sys_mbox_trypost(sys_mbox_t* mailbox, void* message) {
  LWIP_ASSERT("valid mailbox", sys_mbox_valid(mailbox));
  return (xQueueSendToBack(mailbox->handle, &message, 0U) == pdTRUE)
    ? ERR_OK
    : ERR_MEM;
}

err_t sys_mbox_trypost_fromisr(
  sys_mbox_t* mailbox,
  void* message
) {
  LWIP_ASSERT("valid mailbox", sys_mbox_valid(mailbox));
  BaseType_t taskWoken = pdFALSE;
  BaseType_t result = xQueueSendToBackFromISR(
    mailbox->handle,
    &message,
    &taskWoken
  );
  portYIELD_FROM_ISR(taskWoken);
  return (result == pdTRUE) ? ERR_OK : ERR_MEM;
}

u32_t sys_arch_mbox_fetch(
  sys_mbox_t* mailbox,
  void** message,
  u32_t timeoutMs
) {
  LWIP_ASSERT("valid mailbox", sys_mbox_valid(mailbox));
  TickType_t start = xTaskGetTickCount();
  TickType_t timeout = (timeoutMs == 0U)
    ? portMAX_DELAY
    : pdMS_TO_TICKS(timeoutMs);
  if ((timeoutMs != 0U) && (timeout == 0U))
    timeout = 1U;

  void* received = NULL;
  if (xQueueReceive(mailbox->handle, &received, timeout) != pdTRUE)
    return SYS_ARCH_TIMEOUT;
  if (message != NULL)
    *message = received;
  return (u32_t)((xTaskGetTickCount() - start) * portTICK_PERIOD_MS);
}

u32_t sys_arch_mbox_tryfetch(sys_mbox_t* mailbox, void** message) {
  LWIP_ASSERT("valid mailbox", sys_mbox_valid(mailbox));
  void* received = NULL;
  if (xQueueReceive(mailbox->handle, &received, 0U) != pdTRUE)
    return SYS_MBOX_EMPTY;
  if (message != NULL)
    *message = received;
  return 0U;
}

void sys_mbox_free(sys_mbox_t* mailbox) {
  if (sys_mbox_valid(mailbox))
    vQueueDelete(mailbox->handle);
  sys_mbox_set_invalid(mailbox);
}

sys_thread_t sys_thread_new(
  const char* name,
  lwip_thread_fn function,
  void* argument,
  int stackSize,
  int priority
) {
  if ((function == NULL) || (stackSize <= 0)
      || ((uint32_t)stackSize
          > (SYS_ARCH_THREAD_STACK_WORDS * sizeof(StackType_t)))) {
    return NULL;
  }

  for (uint32_t index = 0U; index < SYS_ARCH_THREAD_COUNT; index++) {
    if (sysArchThreads[index].allocated == 0U) {
      sysArchThreads[index].allocated = 1U;
      return xTaskCreateStatic(
        function,
        name,
        SYS_ARCH_THREAD_STACK_WORDS,
        argument,
        (UBaseType_t)priority,
        sysArchThreads[index].stack,
        &sysArchThreads[index].controlBlock
      );
    }
  }
  return NULL;
}
