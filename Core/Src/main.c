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
#define SYSTEM_REGISTER_WAIT_LIMIT    32000000UL

static Platform_StatusTypeDef system_ClockConfigure(void);
static Platform_StatusTypeDef system_WaitForRegister(
  volatile uint32_t* reg,
  uint32_t mask,
  uint32_t expected
);
static void peripheral_GpioInit(void);
static void peripheral_CrcInit(void);
static void peripheral_Spi2Init(void);

int main(void) {
  if (system_ClockConfigure() != PLATFORM_STATUS_OK)
    Error_Handler();
  Platform_Init();

  peripheral_GpioInit();
  peripheral_CrcInit();
  if (Rtc_Init() != PLATFORM_STATUS_OK)
    Error_Handler();
  if (Rs485_Init() != PLATFORM_STATUS_OK)
    Error_Handler();
  printf("RS485 standard output ready.\r\n");

  peripheral_Spi2Init();

  /*
   * This board's Ethernet PHY is not ready immediately after power-up.
   * Allow it to stabilize before the MAC and LwIP initialize the interface.
   */
  Platform_Delay(ETHERNET_PHY_STARTUP_DELAY_MS);
  printf("Startup: PHY delay complete.\r\n");

  if (Rtos_Init() != RTOS_STATUS_OK)
    Error_Handler();

  printf("Startup: RTOS objects ready.\r\n");
  vTaskStartScheduler();
  Error_Handler();
}

static Platform_StatusTypeDef system_WaitForRegister(
  volatile uint32_t* reg,
  uint32_t mask,
  uint32_t expected
) {
  for (uint32_t remaining = SYSTEM_REGISTER_WAIT_LIMIT;
       remaining != 0U;
       --remaining) {
    if ((*reg & mask) == expected)
      return PLATFORM_STATUS_OK;
  }
  return PLATFORM_STATUS_TIMEOUT;
}

static Platform_StatusTypeDef system_ClockConfigure(void) {
  RCC->APB1ENR |= RCC_APB1ENR_PWREN;
  (void)RCC->APB1ENR;
  PWR->CR = (PWR->CR & ~PWR_CR_VOS) | PWR_CR_VOS;

  RCC->CR |= RCC_CR_HSEON;
  if (system_WaitForRegister(
        &RCC->CR, RCC_CR_HSERDY, RCC_CR_HSERDY
      ) != PLATFORM_STATUS_OK)
    return PLATFORM_STATUS_TIMEOUT;

  PWR->CR |= PWR_CR_DBP;
  if (system_WaitForRegister(
        &PWR->CR, PWR_CR_DBP, PWR_CR_DBP
      ) != PLATFORM_STATUS_OK)
    return PLATFORM_STATUS_TIMEOUT;
  RCC->BDCR |= RCC_BDCR_LSEON;
  if (system_WaitForRegister(
        &RCC->BDCR, RCC_BDCR_LSERDY, RCC_BDCR_LSERDY
      ) != PLATFORM_STATUS_OK)
    return PLATFORM_STATUS_TIMEOUT;

  RCC->PLLCFGR = 25U
    | (336U << RCC_PLLCFGR_PLLN_Pos)
    | (0U << RCC_PLLCFGR_PLLP_Pos)
    | RCC_PLLCFGR_PLLSRC_HSE
    | (7U << RCC_PLLCFGR_PLLQ_Pos);
  RCC->CR |= RCC_CR_PLLON;
  if (system_WaitForRegister(
        &RCC->CR, RCC_CR_PLLRDY, RCC_CR_PLLRDY
      ) != PLATFORM_STATUS_OK)
    return PLATFORM_STATUS_TIMEOUT;

  FLASH->ACR = FLASH_ACR_LATENCY_5WS
    | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;
  RCC->CFGR = (RCC->CFGR
      & ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2 | RCC_CFGR_SW))
    | RCC_CFGR_PPRE1_DIV4 | RCC_CFGR_PPRE2_DIV2 | RCC_CFGR_SW_PLL;
  if (system_WaitForRegister(
        &RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_PLL
      ) != PLATFORM_STATUS_OK)
    return PLATFORM_STATUS_TIMEOUT;
  SystemCoreClockUpdate();
  return PLATFORM_STATUS_OK;
}

static void peripheral_GpioInit(void) {
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN
    | RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_GPIOEEN | RCC_AHB1ENR_GPIOHEN;
  (void)RCC->AHB1ENR;
  Platform_GpioWrite(FLASH_CS_GPIO_PORT, FLASH_CS_PIN, 1U);
  Platform_GpioConfigure(FLASH_CS_GPIO_PORT, FLASH_CS_PIN, 1U, 1U, 3U, 0U);
}

static void peripheral_CrcInit(void) {
  RCC->AHB1ENR |= RCC_AHB1ENR_CRCEN;
  CRC->CR = CRC_CR_RESET;
}

static void peripheral_Spi2Init(void) {
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;
  RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;
  (void)RCC->APB1ENR;
  Platform_GpioConfigure(GPIOB, 10U, 2U, 0U, 3U, 5U);
  Platform_GpioConfigure(GPIOC, 2U, 2U, 0U, 3U, 5U);
  Platform_GpioConfigure(GPIOC, 3U, 2U, 0U, 3U, 5U);
  SPI2->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI
    | SPI_CR1_BR_0 | SPI_CR1_SPE;
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
