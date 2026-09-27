/**
  ******************************************************************************
  * @file           : w25q64.h
  * @brief          : Bounded W25Q64 NOR Flash access.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 31.07.2026
  ******************************************************************************
  */

#ifndef W25Q64_H
#define W25Q64_H

#include "main.h"

#include <stddef.h>
#include <stdint.h>

#define W25Q64_CAPACITY_BYTES (8U * 1024U * 1024U)
#define W25Q64_SECTOR_SIZE     4096U
#define W25Q64_PAGE_SIZE       256U

/** @brief Initialize serialized SPI2 access and verify the JEDEC identity. */
Platform_StatusTypeDef W25Q64_Init(void);

/**
  * @brief Lock the flash across a multi-operation transaction.
  * @note Individual operations may be called while this recursive lock is held.
  */
Platform_StatusTypeDef W25Q64_Lock(void);

/** @brief Release a transaction lock acquired with W25Q64_Lock(). */
void W25Q64_Unlock(void);

/**
  * @brief Verify that the expected NOR Flash still responds on SPI2.
  * @retval (uint8_t) Nonzero when the JEDEC identity matches the W25Q64JV.
  */
uint8_t W25Q64_IsAvailable(void);

/** @brief Read a bounded range into caller-owned storage. */
Platform_StatusTypeDef W25Q64_Read(uint32_t address, void* data, size_t length);

/** @brief Erase the 4 KiB sector beginning at an aligned address. */
Platform_StatusTypeDef W25Q64_EraseSector(uint32_t address);

/**
  * @brief Program a bounded range, splitting writes across page boundaries.
  * @note Bits can only transition from one to zero until their sector is erased.
  */
Platform_StatusTypeDef W25Q64_Program(
  uint32_t address,
  const void* data,
  size_t length
);

#endif /* W25Q64_H */
