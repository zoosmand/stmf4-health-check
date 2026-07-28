/**
  ******************************************************************************
  * @file           : onewire.h
  * @brief          : One-wire bus and device discovery interface.
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

#ifndef ONEWIRE_H
#define ONEWIRE_H

#include "FreeRTOS.h"
#include "main.h"
#include "semphr.h"

#define ONEWIRE_MAX_DEVICES 6U

/**
  * @brief Identity and scratchpad data cached for one one-wire device.
  * @param rom (uint8_t[8]) Unique one-wire ROM code.
  * @param scratchpad (uint8_t[9]) Device scratchpad including CRC.
  */
typedef struct {
  uint8_t rom[8];
  uint8_t scratchpad[9];
} OneWireDevice_TypeDef;

/** @brief Initialize the dedicated PE2 bus. */
ErrorStatus OneWire_Init(void);
/** @brief Reset the bus and detect a device presence pulse. */
ErrorStatus OneWire_Reset(void);
/** @brief Write one byte, least-significant bit first. */
void OneWire_WriteByte(uint8_t value);
/** @brief Read one bit from the bus. */
uint8_t OneWire_ReadBit(void);
/** @brief Read one byte, least-significant bit first. */
uint8_t OneWire_ReadByte(void);
/** @brief Update a Dallas/Maxim CRC-8 with one byte. */
uint8_t OneWire_CRC8(uint8_t crc, uint8_t value);
/**
  * @brief Discover and cache up to six valid devices.
  * @retval (ErrorStatus) SUCCESS when at least one valid device is found.
  * @note Acquires and releases the one-wire bus mutex internally.
  */
ErrorStatus OneWire_Search(void);
/** @brief Select a device by its eight-byte ROM code. */
ErrorStatus OneWire_MatchRom(const uint8_t* rom);
/** @brief Read whether a selected device uses parasitic power. */
ErrorStatus OneWire_ReadPowerSupply(
  const uint8_t* rom,
  uint8_t* isParasitic
);
/** @brief Drive PE2 push-pull high during parasitic conversion. */
void OneWire_StrongPullupEnable(void);
/** @brief Restore PE2 to open-drain one-wire operation. */
void OneWire_StrongPullupDisable(void);
/** @brief Acquire exclusive access to the one-wire bus. */
BaseType_t OneWire_Lock(TickType_t timeout);
/** @brief Release exclusive access to the one-wire bus. */
void OneWire_Unlock(void);
/** @brief Return the latest discovered device count. */
uint8_t OneWire_GetDeviceCount(void);
/** @brief Return a borrowed pointer to the discovered device table. */
OneWireDevice_TypeDef* OneWire_GetDevices(void);

#endif /* ONEWIRE_H */
