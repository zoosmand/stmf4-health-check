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

#include "FreeRTOS.h"
#include "lwip.h"
#include "rtc.h"
#include "rs485.h"
#include "rtos.h"
#include "task.h"

#include <stdio.h>

#define ETHERNET_PHY_STARTUP_DELAY_MS 2500U

CRC_HandleTypeDef hcrc;
SPI_HandleTypeDef hspi2;

static void system_ClockConfigure(void);
static void peripheral_GpioInit(void);
static void peripheral_CrcInit(void);
static void peripheral_Spi2Init(void);

int main(void) {
  HAL_Init();
  system_ClockConfigure();

  peripheral_GpioInit();
  peripheral_CrcInit();
  if (Rtc_Init() != HAL_OK)
    Error_Handler();
  peripheral_Spi2Init();
  if (Rs485_Init() != HAL_OK)
    Error_Handler();
  printf("RS485 standard output ready.\r\n");

  /*
   * This board's Ethernet PHY is not ready immediately after power-up.
   * Allow it to stabilize before the MAC and LwIP initialize the interface.
   */
  HAL_Delay(ETHERNET_PHY_STARTUP_DELAY_MS);
  printf("Startup: PHY delay complete.\r\n");

  if (Rtos_Init() != RTOS_STATUS_OK)
    Error_Handler();

  printf("Startup: RTOS objects ready.\r\n");
  vTaskStartScheduler();
  Error_Handler();
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
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();

  HAL_GPIO_WritePin(FLASH_CS_GPIO_PORT, FLASH_CS_PIN, GPIO_PIN_SET);

  GPIO_InitTypeDef flashChipSelect = {
    .Pin = FLASH_CS_PIN,
    .Mode = GPIO_MODE_OUTPUT_PP,
    .Pull = GPIO_PULLUP,
    .Speed = GPIO_SPEED_FREQ_VERY_HIGH,
  };
  HAL_GPIO_Init(FLASH_CS_GPIO_PORT, &flashChipSelect);
}

static void peripheral_CrcInit(void) {
  hcrc.Instance = CRC;
  if (HAL_CRC_Init(&hcrc) != HAL_OK)
    Error_Handler();
}

static void peripheral_Spi2Init(void) {
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 7U;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
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
