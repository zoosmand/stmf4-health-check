/**
  ******************************************************************************
  * @file           : onewire.c
  * @brief          : One-wire bus timing and ROM discovery.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 28.07.2026
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

#include "onewire.h"
#include "delay.h"
#include <string.h>

#define ONEWIRE_PORT GPIOE
#define ONEWIRE_PIN  GPIO_PIN_2
#define ONEWIRE_COMMAND_SEARCH_ROM        0xF0U
#define ONEWIRE_COMMAND_MATCH_ROM         0x55U
#define ONEWIRE_COMMAND_READ_POWER_SUPPLY 0xB4U

static OneWireDevice_TypeDef oneWireDevices[ONEWIRE_MAX_DEVICES];
static uint8_t oneWireDeviceCount;
static StaticSemaphore_t oneWireMutexBuffer;
static SemaphoreHandle_t oneWireMutex;

static void oneWire_WriteBit(uint8_t value);
static ErrorStatus oneWire_SearchUnlocked(void);
static uint32_t oneWire_InterruptLock(void);
static void oneWire_InterruptUnlock(uint32_t interruptMask);

ErrorStatus OneWire_Init(void) {
  HAL_GPIO_WritePin(ONEWIRE_PORT, ONEWIRE_PIN, GPIO_PIN_SET);
  GPIO_InitTypeDef gpio = {
    .Pin = ONEWIRE_PIN,
    .Mode = GPIO_MODE_OUTPUT_OD,
    .Pull = GPIO_PULLUP,
    .Speed = GPIO_SPEED_FREQ_VERY_HIGH,
  };
  HAL_GPIO_Init(ONEWIRE_PORT, &gpio);
  Delay_Init();
  oneWireMutex = xSemaphoreCreateMutexStatic(&oneWireMutexBuffer);
  return (oneWireMutex != NULL) ? SUCCESS : ERROR;
}

ErrorStatus OneWire_Reset(void) {
  uint32_t interruptMask = oneWire_InterruptLock();
  HAL_GPIO_WritePin(ONEWIRE_PORT, ONEWIRE_PIN, GPIO_PIN_RESET);
  Delay_Microseconds(480U);
  HAL_GPIO_WritePin(ONEWIRE_PORT, ONEWIRE_PIN, GPIO_PIN_SET);
  Delay_Microseconds(70U);
  GPIO_PinState presence = HAL_GPIO_ReadPin(ONEWIRE_PORT, ONEWIRE_PIN);
  Delay_Microseconds(410U);
  oneWire_InterruptUnlock(interruptMask);
  return (presence == GPIO_PIN_RESET) ? SUCCESS : ERROR;
}

void OneWire_WriteByte(uint8_t value) {
  for (uint8_t bit = 0U; bit < 8U; bit++) {
    oneWire_WriteBit(value & 0x01U);
    value >>= 1U;
  }
}

uint8_t OneWire_ReadBit(void) {
  uint32_t interruptMask = oneWire_InterruptLock();
  HAL_GPIO_WritePin(ONEWIRE_PORT, ONEWIRE_PIN, GPIO_PIN_RESET);
  Delay_Microseconds(6U);
  HAL_GPIO_WritePin(ONEWIRE_PORT, ONEWIRE_PIN, GPIO_PIN_SET);
  Delay_Microseconds(9U);
  uint8_t value =
    (HAL_GPIO_ReadPin(ONEWIRE_PORT, ONEWIRE_PIN) == GPIO_PIN_SET) ? 1U : 0U;
  Delay_Microseconds(55U);
  oneWire_InterruptUnlock(interruptMask);
  return value;
}

uint8_t OneWire_ReadByte(void) {
  uint8_t value = 0U;
  for (uint8_t bit = 0U; bit < 8U; bit++)
    value |= (uint8_t)(OneWire_ReadBit() << bit);
  return value;
}

uint8_t OneWire_CRC8(uint8_t crc, uint8_t value) {
  for (uint8_t bit = 0U; bit < 8U; bit++) {
    uint8_t mix = (crc ^ value) & 0x01U;
    crc >>= 1U;
    if (mix != 0U)
      crc ^= 0x8CU;
    value >>= 1U;
  }
  return crc;
}

ErrorStatus OneWire_Search(void) {
  if (OneWire_Lock(portMAX_DELAY) != pdTRUE)
    return ERROR;

  ErrorStatus status = oneWire_SearchUnlocked();
  OneWire_Unlock();
  return status;
}

static ErrorStatus oneWire_SearchUnlocked(void) {
  uint8_t rom[8] = {0};
  uint8_t lastDiscrepancy = 0U;
  uint8_t lastDevice = 0U;
  oneWireDeviceCount = 0U;
  memset(oneWireDevices, 0, sizeof(oneWireDevices));

  while ((lastDevice == 0U) && (oneWireDeviceCount < ONEWIRE_MAX_DEVICES)) {
    if (OneWire_Reset() != SUCCESS)
      break;
    OneWire_WriteByte(ONEWIRE_COMMAND_SEARCH_ROM);
    uint8_t discrepancy = 0U;
    uint8_t romBit = 1U;

    for (; romBit <= 64U; romBit++) {
      uint8_t bit = OneWire_ReadBit();
      uint8_t complement = OneWire_ReadBit();
      if ((bit != 0U) && (complement != 0U))
        break;
      uint8_t byteIndex = (romBit - 1U) / 8U;
      uint8_t bitMask = (uint8_t)(1U << ((romBit - 1U) % 8U));
      uint8_t direction;
      if (bit != complement) {
        direction = bit;
      } else {
        direction = (romBit < lastDiscrepancy)
          ? ((rom[byteIndex] & bitMask) != 0U)
          : (romBit == lastDiscrepancy);
        if (direction == 0U)
          discrepancy = romBit;
      }
      if (direction != 0U)
        rom[byteIndex] |= bitMask;
      else
        rom[byteIndex] &= (uint8_t)~bitMask;
      oneWire_WriteBit(direction);
    }

    if (romBit <= 64U)
      break;
    uint8_t crc = 0U;
    for (uint8_t index = 0U; index < 8U; index++)
      crc = OneWire_CRC8(crc, rom[index]);
    if (crc != 0U)
      break;
    memcpy(oneWireDevices[oneWireDeviceCount].rom, rom, sizeof(rom));
    oneWireDeviceCount++;
    lastDiscrepancy = discrepancy;
    lastDevice = (lastDiscrepancy == 0U);
  }
  return (oneWireDeviceCount > 0U) ? SUCCESS : ERROR;
}

ErrorStatus OneWire_MatchRom(const uint8_t* rom) {
  if ((rom == NULL) || (OneWire_Reset() != SUCCESS))
    return ERROR;
  OneWire_WriteByte(ONEWIRE_COMMAND_MATCH_ROM);
  for (uint8_t index = 0U; index < 8U; index++)
    OneWire_WriteByte(rom[index]);
  return SUCCESS;
}

ErrorStatus OneWire_ReadPowerSupply(
  const uint8_t* rom,
  uint8_t* isParasitic
) {
  if ((isParasitic == NULL) || (OneWire_MatchRom(rom) != SUCCESS))
    return ERROR;
  OneWire_WriteByte(ONEWIRE_COMMAND_READ_POWER_SUPPLY);
  *isParasitic = (OneWire_ReadBit() == 0U) ? 1U : 0U;
  return SUCCESS;
}

void OneWire_StrongPullupEnable(void) {
  HAL_GPIO_WritePin(ONEWIRE_PORT, ONEWIRE_PIN, GPIO_PIN_SET);
  CLEAR_BIT(ONEWIRE_PORT->OTYPER, ONEWIRE_PIN);
}

void OneWire_StrongPullupDisable(void) {
  HAL_GPIO_WritePin(ONEWIRE_PORT, ONEWIRE_PIN, GPIO_PIN_SET);
  SET_BIT(ONEWIRE_PORT->OTYPER, ONEWIRE_PIN);
}

BaseType_t OneWire_Lock(TickType_t timeout) {
  return (oneWireMutex != NULL)
    ? xSemaphoreTake(oneWireMutex, timeout)
    : pdFALSE;
}

void OneWire_Unlock(void) {
  if (oneWireMutex != NULL)
    (void)xSemaphoreGive(oneWireMutex);
}

uint8_t OneWire_GetDeviceCount(void) {
  return oneWireDeviceCount;
}

OneWireDevice_TypeDef* OneWire_GetDevices(void) {
  return oneWireDevices;
}

static void oneWire_WriteBit(uint8_t value) {
  uint32_t interruptMask = oneWire_InterruptLock();
  HAL_GPIO_WritePin(ONEWIRE_PORT, ONEWIRE_PIN, GPIO_PIN_RESET);
  Delay_Microseconds((value != 0U) ? 6U : 60U);
  HAL_GPIO_WritePin(ONEWIRE_PORT, ONEWIRE_PIN, GPIO_PIN_SET);
  Delay_Microseconds((value != 0U) ? 64U : 10U);
  oneWire_InterruptUnlock(interruptMask);
}

static uint32_t oneWire_InterruptLock(void) {
  uint32_t interruptMask = __get_PRIMASK();
  __disable_irq();
  return interruptMask;
}

static void oneWire_InterruptUnlock(uint32_t interruptMask) {
  __set_PRIMASK(interruptMask);
}
