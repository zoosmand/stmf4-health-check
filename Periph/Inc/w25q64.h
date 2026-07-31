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

HAL_StatusTypeDef W25Q64_Init(void);

/**
  * @brief Verify that the expected NOR Flash still responds on SPI2.
  * @retval (uint8_t) Nonzero when the JEDEC identity matches the W25Q64JV.
  */
uint8_t W25Q64_IsAvailable(void);

HAL_StatusTypeDef W25Q64_Read(uint32_t address, void* data, size_t length);
HAL_StatusTypeDef W25Q64_EraseSector(uint32_t address);
HAL_StatusTypeDef W25Q64_Program(
  uint32_t address,
  const void* data,
  size_t length
);

#endif /* W25Q64_H */
