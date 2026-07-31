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

static void w25q64_Select(void) {
  HAL_GPIO_WritePin(FLASH_CS_GPIO_PORT, FLASH_CS_PIN, GPIO_PIN_RESET);
}

static void w25q64_Deselect(void) {
  HAL_GPIO_WritePin(FLASH_CS_GPIO_PORT, FLASH_CS_PIN, GPIO_PIN_SET);
}

static HAL_StatusTypeDef w25q64_Command(
  const uint8_t* command,
  size_t commandLength,
  uint8_t* receive,
  size_t receiveLength
) {
  w25q64_Select();
  HAL_StatusTypeDef status = HAL_SPI_Transmit(
    &hspi2,
    (uint8_t*)command,
    commandLength,
    W25Q64_SPI_TIMEOUT_MS
  );
  if ((status == HAL_OK) && (receiveLength != 0U)) {
    status = HAL_SPI_Receive(
      &hspi2,
      receive,
      receiveLength,
      W25Q64_SPI_TIMEOUT_MS
    );
  }
  w25q64_Deselect();
  return status;
}

static HAL_StatusTypeDef w25q64_WriteEnable(void) {
  const uint8_t command = W25Q64_COMMAND_WRITE_ENABLE;
  return w25q64_Command(&command, 1U, NULL, 0U);
}

static HAL_StatusTypeDef w25q64_WaitReady(uint32_t timeoutMs) {
  uint32_t started = HAL_GetTick();
  uint8_t status;
  do {
    const uint8_t command = W25Q64_COMMAND_STATUS;
    if (w25q64_Command(&command, 1U, &status, 1U) != HAL_OK)
      return HAL_ERROR;
    if ((status & W25Q64_STATUS_BUSY) == 0U)
      return HAL_OK;
    vTaskDelay(pdMS_TO_TICKS(1U));
  } while ((HAL_GetTick() - started) < timeoutMs);
  return HAL_TIMEOUT;
}

HAL_StatusTypeDef W25Q64_Init(void) {
  flashMutex = xSemaphoreCreateMutexStatic(&flashMutexControlBlock);
  if (flashMutex == NULL)
    return HAL_ERROR;
  const uint8_t command = W25Q64_COMMAND_JEDEC_ID;
  uint8_t identity[3];
  if (w25q64_Command(&command, 1U, identity, sizeof(identity)) != HAL_OK)
    return HAL_ERROR;
  return ((identity[0] == 0xEFU)
      && (identity[1] == 0x40U)
      && (identity[2] == 0x17U))
    ? HAL_OK
    : HAL_ERROR;
}

HAL_StatusTypeDef W25Q64_Read(uint32_t address, void* data, size_t length) {
  if ((data == NULL) || (length == 0U)
      || (address > W25Q64_CAPACITY_BYTES - length))
    return HAL_ERROR;
  if (xSemaphoreTake(flashMutex, portMAX_DELAY) != pdTRUE)
    return HAL_ERROR;
  uint8_t command[4] = {
    W25Q64_COMMAND_READ,
    (uint8_t)(address >> 16U),
    (uint8_t)(address >> 8U),
    (uint8_t)address,
  };
  HAL_StatusTypeDef status = w25q64_Command(
    command, sizeof(command), data, length
  );
  (void)xSemaphoreGive(flashMutex);
  return status;
}

HAL_StatusTypeDef W25Q64_EraseSector(uint32_t address) {
  if ((address >= W25Q64_CAPACITY_BYTES)
      || ((address % W25Q64_SECTOR_SIZE) != 0U))
    return HAL_ERROR;
  if (xSemaphoreTake(flashMutex, portMAX_DELAY) != pdTRUE)
    return HAL_ERROR;
  HAL_StatusTypeDef status = w25q64_WriteEnable();
  uint8_t command[4] = {
    W25Q64_COMMAND_SECTOR_ERASE,
    (uint8_t)(address >> 16U),
    (uint8_t)(address >> 8U),
    (uint8_t)address,
  };
  if (status == HAL_OK)
    status = w25q64_Command(command, sizeof(command), NULL, 0U);
  if (status == HAL_OK)
    status = w25q64_WaitReady(W25Q64_ERASE_TIMEOUT_MS);
  (void)xSemaphoreGive(flashMutex);
  return status;
}

HAL_StatusTypeDef W25Q64_Program(
  uint32_t address,
  const void* data,
  size_t length
) {
  if ((data == NULL) || (length == 0U)
      || (address > W25Q64_CAPACITY_BYTES - length))
    return HAL_ERROR;
  if (xSemaphoreTake(flashMutex, portMAX_DELAY) != pdTRUE)
    return HAL_ERROR;

  const uint8_t* source = data;
  HAL_StatusTypeDef status = HAL_OK;
  while ((length != 0U) && (status == HAL_OK)) {
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
    if (status == HAL_OK) {
      w25q64_Select();
      status = HAL_SPI_Transmit(
        &hspi2, command, sizeof(command), W25Q64_SPI_TIMEOUT_MS
      );
      if (status == HAL_OK) {
        status = HAL_SPI_Transmit(
          &hspi2, (uint8_t*)source, chunk, W25Q64_SPI_TIMEOUT_MS
        );
      }
      w25q64_Deselect();
    }
    if (status == HAL_OK)
      status = w25q64_WaitReady(W25Q64_PROGRAM_TIMEOUT_MS);
    address += chunk;
    source += chunk;
    length -= chunk;
  }

  (void)xSemaphoreGive(flashMutex);
  return status;
}
