/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Application entry point and platform initialization.
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

#include "lwip.h"

#define ETHERNET_PHY_STARTUP_DELAY_MS 2500U

CRC_HandleTypeDef hcrc;
RTC_HandleTypeDef hrtc;

static void system_ClockConfigure(void);
static void peripheral_GpioInit(void);
static void peripheral_CrcInit(void);
static void peripheral_RtcInit(void);

int main(void) {
  HAL_Init();
  system_ClockConfigure();

  peripheral_GpioInit();
  peripheral_CrcInit();
  peripheral_RtcInit();

  /*
   * This board's Ethernet PHY is not ready immediately after power-up.
   * Allow it to stabilize before the MAC and LwIP initialize the interface.
   */
  HAL_Delay(ETHERNET_PHY_STARTUP_DELAY_MS);

  if (Lwip_Init() != LWIP_STATUS_OK)
    Error_Handler();

  for (;;)
    Lwip_Process();
}

static void system_ClockConfigure(void) {
  RCC_OscInitTypeDef oscillator = {0};
  RCC_ClkInitTypeDef clock = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  oscillator.OscillatorType =
    RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_LSE;
  oscillator.HSEState = RCC_HSE_ON;
  oscillator.LSEState = RCC_LSE_ON;
  oscillator.PLL.PLLState = RCC_PLL_ON;
  oscillator.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  oscillator.PLL.PLLM = 25U;
  oscillator.PLL.PLLN = 336U;
  oscillator.PLL.PLLP = RCC_PLLP_DIV2;
  oscillator.PLL.PLLQ = 4U;
  if (HAL_RCC_OscConfig(&oscillator) != HAL_OK)
    Error_Handler();

  clock.ClockType = RCC_CLOCKTYPE_HCLK
    | RCC_CLOCKTYPE_SYSCLK
    | RCC_CLOCKTYPE_PCLK1
    | RCC_CLOCKTYPE_PCLK2;
  clock.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clock.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clock.APB1CLKDivider = RCC_HCLK_DIV4;
  clock.APB2CLKDivider = RCC_HCLK_DIV2;
  if (HAL_RCC_ClockConfig(&clock, FLASH_LATENCY_5) != HAL_OK)
    Error_Handler();
}

static void peripheral_GpioInit(void) {
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
}

static void peripheral_CrcInit(void) {
  hcrc.Instance = CRC;
  if (HAL_CRC_Init(&hcrc) != HAL_OK)
    Error_Handler();
}

static void peripheral_RtcInit(void) {
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127U;
  hrtc.Init.SynchPrediv = 255U;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
    Error_Handler();
}

void Error_Handler(void) {
  __disable_irq();
  for (;;) {
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t* file, uint32_t line) {
  (void)file;
  (void)line;
  Error_Handler();
}
#endif
