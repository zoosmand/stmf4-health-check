#ifndef PLATFORM_H
#define PLATFORM_H

#include "stm32f4xx.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
  PLATFORM_STATUS_OK = 0,
  PLATFORM_STATUS_ERROR,
  PLATFORM_STATUS_TIMEOUT,
} Platform_StatusTypeDef;

#define PLATFORM_GPIO_MODE_INPUT      0U
#define PLATFORM_GPIO_MODE_OUTPUT     1U
#define PLATFORM_GPIO_MODE_ALTERNATE  2U
#define PLATFORM_GPIO_PULL_NONE       0U
#define PLATFORM_GPIO_PULL_UP         1U
#define PLATFORM_GPIO_PULL_DOWN       2U
#define PLATFORM_GPIO_SPEED_LOW       0U
#define PLATFORM_GPIO_SPEED_VERY_HIGH 3U

/** @brief Enable core facilities and configure the 1 kHz SysTick timebase. */
void Platform_Init(void);

/** @brief Advance the platform millisecond counter from SysTick_Handler(). */
void Platform_IncrementTick(void);

/** @brief Return the wrapping monotonic millisecond counter. */
uint32_t Platform_GetTick(void);

/** @brief Busy-wait for a duration before the scheduler is required. */
void Platform_Delay(uint32_t milliseconds);

/**
  * @brief Configure one STM32 GPIO using CMSIS register fields.
  * @param port (GPIO_TypeDef*) Non-null GPIO peripheral.
  * @param pin (uint8_t) Pin number from 0 through 15.
  * @param mode (uint32_t) PLATFORM_GPIO_MODE_* value.
  * @param pull (uint32_t) PLATFORM_GPIO_PULL_* value.
  * @param speed (uint32_t) STM32 GPIO speed field value.
  * @param alternate (uint8_t) Alternate-function number when mode is alternate.
  */
void Platform_GpioConfigure(
  GPIO_TypeDef* port,
  uint8_t pin,
  uint32_t mode,
  uint32_t pull,
  uint32_t speed,
  uint8_t alternate
);

/** @brief Atomically drive one configured output high or low. */
static inline void Platform_GpioWrite(
  GPIO_TypeDef* port,
  uint8_t pin,
  uint8_t high
) {
  port->BSRR = high != 0U ? (1UL << pin) : (1UL << (pin + 16U));
}

/** @brief Read one GPIO input and return nonzero for a high level. */
static inline uint8_t Platform_GpioRead(GPIO_TypeDef* port, uint8_t pin) {
  return (port->IDR & (1UL << pin)) != 0U;
}

#endif /* PLATFORM_H */
