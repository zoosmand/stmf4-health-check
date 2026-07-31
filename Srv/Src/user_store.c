/**
  ******************************************************************************
  * @file           : user_store.c
  * @brief          : Persistent management-user database.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 31.07.2026
  ******************************************************************************
  */

#include "user_store.h"

#include "FreeRTOS.h"
#include "flash_layout.h"
#include "semphr.h"
#include "w25q64.h"

#include <string.h>

#define USER_STORE_MAGIC       0x55534552UL
#define USER_STORE_VERSION     1U
#define USER_STORE_SECTOR_A    FLASH_LAYOUT_USER_STORE_SECTOR_A
#define USER_STORE_SECTOR_B    FLASH_LAYOUT_USER_STORE_SECTOR_B

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t count;
  uint32_t generation;
  UserStore_RecordTypeDef users[USER_STORE_MAX_USERS];
  uint32_t crc;
} UserStore_SnapshotTypeDef;

static UserStore_SnapshotTypeDef snapshot;
static uint32_t activeAddress;
static StaticSemaphore_t storeMutexControlBlock;
static SemaphoreHandle_t storeMutex;

static uint32_t userStore_Crc(const void* data, size_t length) {
  const uint8_t* bytes = data;
  uint32_t crc = 0xFFFFFFFFUL;
  while (length-- != 0U) {
    crc ^= *bytes++;
    for (uint8_t bit = 0U; bit < 8U; ++bit)
      crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
  }
  return ~crc;
}

static uint8_t userStore_IsValid(const UserStore_SnapshotTypeDef* candidate) {
  return ((candidate->magic == USER_STORE_MAGIC)
      && (candidate->version == USER_STORE_VERSION)
      && (candidate->count <= USER_STORE_MAX_USERS)
      && (candidate->crc == userStore_Crc(
        candidate, offsetof(UserStore_SnapshotTypeDef, crc)
      )))
    ? 1U
    : 0U;
}

static HAL_StatusTypeDef userStore_Save(
  UserStore_SnapshotTypeDef* candidate
) {
  uint32_t target = (activeAddress == USER_STORE_SECTOR_A)
    ? USER_STORE_SECTOR_B
    : USER_STORE_SECTOR_A;
  candidate->magic = USER_STORE_MAGIC;
  candidate->version = USER_STORE_VERSION;
  ++candidate->generation;
  candidate->crc = userStore_Crc(
    candidate, offsetof(UserStore_SnapshotTypeDef, crc)
  );
  if ((W25Q64_EraseSector(target) != HAL_OK)
      || (W25Q64_Program(target, candidate, sizeof(*candidate)) != HAL_OK))
    return HAL_ERROR;
  UserStore_SnapshotTypeDef verification;
  if ((W25Q64_Read(target, &verification, sizeof(verification)) != HAL_OK)
      || (userStore_IsValid(&verification) == 0U)
      || (verification.generation != candidate->generation))
    return HAL_ERROR;
  snapshot = *candidate;
  activeAddress = target;
  return HAL_OK;
}

HAL_StatusTypeDef UserStore_Init(void) {
  storeMutex = xSemaphoreCreateMutexStatic(&storeMutexControlBlock);
  if (storeMutex == NULL)
    return HAL_ERROR;
  UserStore_SnapshotTypeDef first;
  UserStore_SnapshotTypeDef second;
  uint8_t firstValid = (W25Q64_Read(
    USER_STORE_SECTOR_A, &first, sizeof(first)
  ) == HAL_OK) && userStore_IsValid(&first);
  uint8_t secondValid = (W25Q64_Read(
    USER_STORE_SECTOR_B, &second, sizeof(second)
  ) == HAL_OK) && userStore_IsValid(&second);
  if ((firstValid != 0U)
      && ((secondValid == 0U) || (first.generation >= second.generation))) {
    snapshot = first;
    activeAddress = USER_STORE_SECTOR_A;
  } else if (secondValid != 0U) {
    snapshot = second;
    activeAddress = USER_STORE_SECTOR_B;
  } else {
    memset(&snapshot, 0, sizeof(snapshot));
    activeAddress = USER_STORE_SECTOR_B;
  }
  return HAL_OK;
}

HAL_StatusTypeDef UserStore_Find(
  const char* username,
  UserStore_RecordTypeDef* record,
  uint8_t* index
) {
  if ((username == NULL) || (record == NULL))
    return HAL_ERROR;
  if (xSemaphoreTake(storeMutex, portMAX_DELAY) != pdTRUE)
    return HAL_ERROR;
  HAL_StatusTypeDef status = HAL_ERROR;
  for (uint8_t position = 0U; position < snapshot.count; ++position) {
    if (strcmp(snapshot.users[position].username, username) == 0) {
      *record = snapshot.users[position];
      if (index != NULL)
        *index = position;
      status = HAL_OK;
      break;
    }
  }
  (void)xSemaphoreGive(storeMutex);
  return status;
}

HAL_StatusTypeDef UserStore_Put(const UserStore_RecordTypeDef* record) {
  if ((record == NULL) || (record->username[0] == '\0'))
    return HAL_ERROR;
  if (xSemaphoreTake(storeMutex, portMAX_DELAY) != pdTRUE)
    return HAL_ERROR;
  UserStore_SnapshotTypeDef candidate = snapshot;
  uint8_t position;
  for (position = 0U; position < candidate.count; ++position) {
    if (strcmp(candidate.users[position].username, record->username) == 0)
      break;
  }
  if (position == candidate.count) {
    if (candidate.count >= USER_STORE_MAX_USERS) {
      (void)xSemaphoreGive(storeMutex);
      return HAL_ERROR;
    }
    ++candidate.count;
  }
  candidate.users[position] = *record;
  HAL_StatusTypeDef status = userStore_Save(&candidate);
  (void)xSemaphoreGive(storeMutex);
  return status;
}

HAL_StatusTypeDef UserStore_Delete(const char* username) {
  if (username == NULL)
    return HAL_ERROR;
  if (xSemaphoreTake(storeMutex, portMAX_DELAY) != pdTRUE)
    return HAL_ERROR;
  UserStore_SnapshotTypeDef candidate = snapshot;
  uint8_t position;
  for (position = 0U; position < candidate.count; ++position) {
    if (strcmp(candidate.users[position].username, username) == 0)
      break;
  }
  if (position == candidate.count) {
    (void)xSemaphoreGive(storeMutex);
    return HAL_ERROR;
  }
  for (uint8_t tail = position; (tail + 1U) < candidate.count; ++tail)
    candidate.users[tail] = candidate.users[tail + 1U];
  memset(&candidate.users[candidate.count - 1U], 0, sizeof(candidate.users[0]));
  --candidate.count;
  HAL_StatusTypeDef status = userStore_Save(&candidate);
  (void)xSemaphoreGive(storeMutex);
  return status;
}

size_t UserStore_List(UserStore_RecordTypeDef* records, size_t capacity) {
  if ((records == NULL) || (capacity == 0U))
    return 0U;
  if (xSemaphoreTake(storeMutex, portMAX_DELAY) != pdTRUE)
    return 0U;
  size_t count = snapshot.count < capacity ? snapshot.count : capacity;
  memcpy(records, snapshot.users, count * sizeof(*records));
  (void)xSemaphoreGive(storeMutex);
  return count;
}
