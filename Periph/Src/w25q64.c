/**
  ******************************************************************************
  * @file           : w25q64.c
  * @brief          : Bounded W25Q64 NOR Flash access.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 31.07.2026
  ******************************************************************************
  */

#include "w25q64.h"

#include "FreeRTOS.h"
#include "semphr.h"

#define W25Q64_COMMAND_JEDEC_ID       0x9FU
#define W25Q64_COMMAND_READ           0x03U
#define W25Q64_COMMAND_WRITE_ENABLE   0x06U
#define W25Q64_COMMAND_STATUS         0x05U
#define W25Q64_COMMAND_PAGE_PROGRAM   0x02U
#define W25Q64_COMMAND_SECTOR_ERASE   0x20U
#define W25Q64_STATUS_BUSY            0x01U
#define W25Q64_SPI_TIMEOUT_MS         100U
#define W25Q64_PROGRAM_TIMEOUT_MS     100U
#define W25Q64_ERASE_TIMEOUT_MS       2000U

static StaticSemaphore_t flashMutexControlBlock;
static SemaphoreHandle_t flashMutex;

static Platform_StatusTypeDef w25q64_Transfer(
  const uint8_t* transmit,
  uint8_t* receive,
  size_t length
) {
  uint32_t started = Platform_GetTick();
  for (size_t offset = 0U; offset < length; ++offset) {
    while ((SPI2->SR & SPI_SR_TXE) == 0U) {
      if ((Platform_GetTick() - started) >= W25Q64_SPI_TIMEOUT_MS)
        return PLATFORM_STATUS_TIMEOUT;
    }
    *(__IO uint8_t*)&SPI2->DR = transmit != NULL ? transmit[offset] : 0xFFU;
    while ((SPI2->SR & SPI_SR_RXNE) == 0U) {
      if ((Platform_GetTick() - started) >= W25Q64_SPI_TIMEOUT_MS)
        return PLATFORM_STATUS_TIMEOUT;
    }
    uint8_t value = *(__IO uint8_t*)&SPI2->DR;
    if (receive != NULL)
      receive[offset] = value;
  }
  while ((SPI2->SR & SPI_SR_BSY) != 0U) {
    if ((Platform_GetTick() - started) >= W25Q64_SPI_TIMEOUT_MS)
      return PLATFORM_STATUS_TIMEOUT;
  }
  return PLATFORM_STATUS_OK;
}

static void w25q64_Select(void) {
  Platform_GpioWrite(FLASH_CS_GPIO_PORT, FLASH_CS_PIN, 0U);
}

static void w25q64_Deselect(void) {
  Platform_GpioWrite(FLASH_CS_GPIO_PORT, FLASH_CS_PIN, 1U);
}

static Platform_StatusTypeDef w25q64_Command(
  const uint8_t* command,
  size_t commandLength,
  uint8_t* receive,
  size_t receiveLength
) {
  w25q64_Select();
  Platform_StatusTypeDef status = w25q64_Transfer(
    command, NULL, commandLength
  );
  if ((status == PLATFORM_STATUS_OK) && (receiveLength != 0U)) {
    status = w25q64_Transfer(NULL, receive, receiveLength);
  }
  w25q64_Deselect();
  return status;
}

static Platform_StatusTypeDef w25q64_WriteEnable(void) {
  const uint8_t command = W25Q64_COMMAND_WRITE_ENABLE;
  return w25q64_Command(&command, 1U, NULL, 0U);
}

static Platform_StatusTypeDef w25q64_WaitReady(uint32_t timeoutMs) {
  uint32_t started = Platform_GetTick();
  uint8_t status;
  do {
    const uint8_t command = W25Q64_COMMAND_STATUS;
    if (w25q64_Command(&command, 1U, &status, 1U) != PLATFORM_STATUS_OK)
      return PLATFORM_STATUS_ERROR;
    if ((status & W25Q64_STATUS_BUSY) == 0U)
      return PLATFORM_STATUS_OK;
    vTaskDelay(pdMS_TO_TICKS(1U));
  } while ((Platform_GetTick() - started) < timeoutMs);
  return PLATFORM_STATUS_TIMEOUT;
}

Platform_StatusTypeDef W25Q64_Init(void) {
  flashMutex = xSemaphoreCreateMutexStatic(&flashMutexControlBlock);
  if (flashMutex == NULL)
    return PLATFORM_STATUS_ERROR;
  return W25Q64_IsAvailable() != 0U ? PLATFORM_STATUS_OK : PLATFORM_STATUS_ERROR;
}

uint8_t W25Q64_IsAvailable(void) {
  if ((flashMutex == NULL)
      || (xSemaphoreTake(flashMutex, portMAX_DELAY) != pdTRUE)) {
    return 0U;
  }
  const uint8_t command = W25Q64_COMMAND_JEDEC_ID;
  uint8_t identity[3];
  Platform_StatusTypeDef status = w25q64_Command(
    &command, 1U, identity, sizeof(identity)
  );
  (void)xSemaphoreGive(flashMutex);
  return ((status == PLATFORM_STATUS_OK)
      && (identity[0] == 0xEFU)
      && (identity[1] == 0x40U)
      && (identity[2] == 0x17U))
    ? 1U
    : 0U;
}

Platform_StatusTypeDef W25Q64_Read(uint32_t address, void* data, size_t length) {
  if ((data == NULL) || (length == 0U)
      || (address > W25Q64_CAPACITY_BYTES - length))
    return PLATFORM_STATUS_ERROR;
  if (xSemaphoreTake(flashMutex, portMAX_DELAY) != pdTRUE)
    return PLATFORM_STATUS_ERROR;
  uint8_t command[4] = {
    W25Q64_COMMAND_READ,
    (uint8_t)(address >> 16U),
    (uint8_t)(address >> 8U),
    (uint8_t)address,
  };
  Platform_StatusTypeDef status = w25q64_Command(
    command, sizeof(command), data, length
  );
  (void)xSemaphoreGive(flashMutex);
  return status;
}

Platform_StatusTypeDef W25Q64_EraseSector(uint32_t address) {
  if ((address >= W25Q64_CAPACITY_BYTES)
      || ((address % W25Q64_SECTOR_SIZE) != 0U))
    return PLATFORM_STATUS_ERROR;
  if (xSemaphoreTake(flashMutex, portMAX_DELAY) != pdTRUE)
    return PLATFORM_STATUS_ERROR;
  Platform_StatusTypeDef status = w25q64_WriteEnable();
  uint8_t command[4] = {
    W25Q64_COMMAND_SECTOR_ERASE,
    (uint8_t)(address >> 16U),
    (uint8_t)(address >> 8U),
    (uint8_t)address,
  };
  if (status == PLATFORM_STATUS_OK)
    status = w25q64_Command(command, sizeof(command), NULL, 0U);
  if (status == PLATFORM_STATUS_OK)
    status = w25q64_WaitReady(W25Q64_ERASE_TIMEOUT_MS);
  (void)xSemaphoreGive(flashMutex);
  return status;
}

Platform_StatusTypeDef W25Q64_Program(
  uint32_t address,
  const void* data,
  size_t length
) {
  if ((data == NULL) || (length == 0U)
      || (address > W25Q64_CAPACITY_BYTES - length))
    return PLATFORM_STATUS_ERROR;
  if (xSemaphoreTake(flashMutex, portMAX_DELAY) != pdTRUE)
    return PLATFORM_STATUS_ERROR;

  const uint8_t* source = data;
  Platform_StatusTypeDef status = PLATFORM_STATUS_OK;
  while ((length != 0U) && (status == PLATFORM_STATUS_OK)) {
    size_t chunk = W25Q64_PAGE_SIZE - (address % W25Q64_PAGE_SIZE);
    if (chunk > length)
      chunk = length;
    status = w25q64_WriteEnable();
    uint8_t command[4] = {
      W25Q64_COMMAND_PAGE_PROGRAM,
      (uint8_t)(address >> 16U),
      (uint8_t)(address >> 8U),
      (uint8_t)address,
    };
    if (status == PLATFORM_STATUS_OK) {
      w25q64_Select();
      status = w25q64_Transfer(command, NULL, sizeof(command));
      if (status == PLATFORM_STATUS_OK) {
        status = w25q64_Transfer(source, NULL, chunk);
      }
      w25q64_Deselect();
    }
    if (status == PLATFORM_STATUS_OK)
      status = w25q64_WaitReady(W25Q64_PROGRAM_TIMEOUT_MS);
    address += chunk;
    source += chunk;
    length -= chunk;
  }

  (void)xSemaphoreGive(flashMutex);
  return status;
}
