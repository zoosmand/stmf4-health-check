/*
 * Copyright (c) 2001-2003 Swedish Institute of Computer Science.
 * Copyright (c) 2017-2026 Dmitry Slobodchikov.
 * All rights reserved.
 *
 * This file provides the native FreeRTOS adaptation required by lwIP.
 */

#ifndef LWIP_ARCH_SYS_ARCH_H
#define LWIP_ARCH_SYS_ARCH_H

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "lwip/arch.h"

#define SYS_ARCH_MBOX_CAPACITY 16U

typedef struct {
  SemaphoreHandle_t handle;
  StaticSemaphore_t storage;
} sys_sem_t;

typedef struct {
  SemaphoreHandle_t handle;
  StaticSemaphore_t storage;
} sys_mutex_t;

typedef struct {
  QueueHandle_t handle;
  StaticQueue_t queue;
  void* messages[SYS_ARCH_MBOX_CAPACITY];
} sys_mbox_t;

typedef TaskHandle_t sys_thread_t;

#define SYS_SEM_NULL  ((sys_sem_t){0})
#define SYS_MBOX_NULL ((sys_mbox_t){0})

#define sys_sem_valid(sem) \
  (((sem) != NULL) && ((sem)->handle != NULL))
#define sys_sem_set_invalid(sem) ((sem)->handle = NULL)

#define sys_mutex_valid(mutex) \
  (((mutex) != NULL) && ((mutex)->handle != NULL))
#define sys_mutex_set_invalid(mutex) ((mutex)->handle = NULL)

#define sys_mbox_valid(mbox) \
  (((mbox) != NULL) && ((mbox)->handle != NULL))
#define sys_mbox_set_invalid(mbox) ((mbox)->handle = NULL)

void sys_arch_msleep(u32_t delayMs);
#define sys_msleep(delayMs) sys_arch_msleep(delayMs)

#endif /* LWIP_ARCH_SYS_ARCH_H */
