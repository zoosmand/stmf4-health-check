/**
  ******************************************************************************
  * @file           : stm32f4xx_it.c
  * @brief          : Cortex-M exception handlers.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 13.01.2026
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

#include "main.h"
#include "stm32f4xx_it.h"

static void exception_Stop(void) {
  for (;;) {
  }
}

void NMI_Handler(void) {
  exception_Stop();
}

void HardFault_Handler(void) {
  exception_Stop();
}

void MemManage_Handler(void) {
  exception_Stop();
}

void BusFault_Handler(void) {
  exception_Stop();
}

void UsageFault_Handler(void) {
  exception_Stop();
}

void SVC_Handler(void) {
}

void DebugMon_Handler(void) {
}

void PendSV_Handler(void) {
}

void SysTick_Handler(void) {
  HAL_IncTick();
}
