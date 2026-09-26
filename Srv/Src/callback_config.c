#include "callback_config.h"

#include "FreeRTOS.h"
#include "flash_layout.h"
#include "semphr.h"
#include "tls_trust_store.h"
#include "w25q64.h"

#include <stddef.h>
#include <string.h>

#define CALLBACK_CONFIG_MAGIC   0x43424B43UL
#define CALLBACK_CONFIG_VERSION 1U

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  uint32_t generation;
  CallbackConfig_TypeDef config;
  uint32_t crc;
} CallbackConfig_SnapshotTypeDef;

_Static_assert(
  sizeof(CallbackConfig_SnapshotTypeDef) <= W25Q64_SECTOR_SIZE,
  "Callback configuration snapshot must fit in one NOR sector"
);

static CallbackConfig_SnapshotTypeDef snapshot;
static uint32_t activeAddress;
static StaticSemaphore_t callbackConfigMutexControlBlock;
static SemaphoreHandle_t callbackConfigMutex;

static uint8_t callbackConfig_HostValid(const char* host) {
  if ((host == NULL) || (host[0] == '\0'))
    return 0U;
  for (size_t index = 0U; index < CALLBACK_CONFIG_HOST_SIZE; ++index) {
    char character = host[index];
    if (character == '\0')
      return 1U;
    if (!(((character >= 'a') && (character <= 'z'))
          || ((character >= 'A') && (character <= 'Z'))
          || ((character >= '0') && (character <= '9'))
          || (character == '.') || (character == '-'))) {
      return 0U;
    }
  }
  return 0U;
}

static uint8_t callbackConfig_PathValid(const char* path) {
  if ((path == NULL) || (path[0] != '/'))
    return 0U;
  for (size_t index = 0U; index < CALLBACK_CONFIG_PATH_SIZE; ++index) {
    uint8_t character = (uint8_t)path[index];
    if (character == '\0')
      return 1U;
    if ((character < 0x21U) || (character > 0x7EU)
        || (character == '"') || (character == '\\')
        || (character == '#')) {
      return 0U;
    }
  }
  return 0U;
}

static uint32_t callbackConfig_Crc(const void* data, size_t length) {
  const uint8_t* bytes = data;
  uint32_t crc = 0xFFFFFFFFUL;
  while (length-- != 0U) {
    crc ^= *bytes++;
    for (uint8_t bit = 0U; bit < 8U; ++bit)
      crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
  }
  return ~crc;
}

static uint8_t callbackConfig_FieldsValid(const CallbackConfig_TypeDef* value) {
  return ((value != NULL) && (value->enabled <= 1U)
      && (value->method <= CALLBACK_METHOD_POST) && (value->port != 0U)
      && (value->trustAnchorId <= TLS_TRUST_STORE_MAX_PERSISTED)
      && (callbackConfig_HostValid(value->host) != 0U)
      && (callbackConfig_PathValid(value->path) != 0U)) ? 1U : 0U;
}

static uint8_t callbackConfig_Valid(const CallbackConfig_SnapshotTypeDef* value) {
  return ((value->magic == CALLBACK_CONFIG_MAGIC)
      && (value->version == CALLBACK_CONFIG_VERSION)
      && (callbackConfig_FieldsValid(&value->config) != 0U)
      && (value->crc == callbackConfig_Crc(
        value, offsetof(CallbackConfig_SnapshotTypeDef, crc)
      ))) ? 1U : 0U;
}

static Platform_StatusTypeDef callbackConfig_Save(
  CallbackConfig_SnapshotTypeDef* candidate
) {
  uint32_t target = activeAddress == FLASH_LAYOUT_CALLBACK_CONFIG_SECTOR_A
    ? FLASH_LAYOUT_CALLBACK_CONFIG_SECTOR_B
    : FLASH_LAYOUT_CALLBACK_CONFIG_SECTOR_A;
  candidate->magic = CALLBACK_CONFIG_MAGIC;
  candidate->version = CALLBACK_CONFIG_VERSION;
  ++candidate->generation;
  candidate->crc = callbackConfig_Crc(
    candidate, offsetof(CallbackConfig_SnapshotTypeDef, crc)
  );
  if ((W25Q64_EraseSector(target) != PLATFORM_STATUS_OK)
      || (W25Q64_Program(target, candidate, sizeof(*candidate))
          != PLATFORM_STATUS_OK))
    return PLATFORM_STATUS_ERROR;
  CallbackConfig_SnapshotTypeDef verification;
  if ((W25Q64_Read(target, &verification, sizeof(verification))
        != PLATFORM_STATUS_OK)
      || (callbackConfig_Valid(&verification) == 0U)
      || (verification.generation != candidate->generation))
    return PLATFORM_STATUS_ERROR;
  snapshot = *candidate;
  activeAddress = target;
  return PLATFORM_STATUS_OK;
}

Platform_StatusTypeDef CallbackConfig_Init(void) {
  callbackConfigMutex = xSemaphoreCreateMutexStatic(
    &callbackConfigMutexControlBlock
  );
  if (callbackConfigMutex == NULL)
    return PLATFORM_STATUS_ERROR;
  CallbackConfig_SnapshotTypeDef first;
  CallbackConfig_SnapshotTypeDef second;
  uint8_t firstValid = W25Q64_Read(
    FLASH_LAYOUT_CALLBACK_CONFIG_SECTOR_A, &first, sizeof(first)
  ) == PLATFORM_STATUS_OK && callbackConfig_Valid(&first);
  uint8_t secondValid = W25Q64_Read(
    FLASH_LAYOUT_CALLBACK_CONFIG_SECTOR_B, &second, sizeof(second)
  ) == PLATFORM_STATUS_OK && callbackConfig_Valid(&second);
  if ((firstValid != 0U)
      && ((secondValid == 0U) || (first.generation >= second.generation))) {
    snapshot = first;
    activeAddress = FLASH_LAYOUT_CALLBACK_CONFIG_SECTOR_A;
    return PLATFORM_STATUS_OK;
  }
  if (secondValid != 0U) {
    snapshot = second;
    activeAddress = FLASH_LAYOUT_CALLBACK_CONFIG_SECTOR_B;
    return PLATFORM_STATUS_OK;
  }
  memset(&snapshot, 0, sizeof(snapshot));
  snapshot.config.method = CALLBACK_METHOD_POST;
  snapshot.config.port = 443U;
  snapshot.config.trustAnchorId = TLS_TRUST_STORE_DEFAULT_ID;
  (void)strncpy(snapshot.config.host, "loopback.intraclear.com",
    sizeof(snapshot.config.host) - 1U);
  (void)strncpy(snapshot.config.path, "/", sizeof(snapshot.config.path) - 1U);
  activeAddress = FLASH_LAYOUT_CALLBACK_CONFIG_SECTOR_B;
  return callbackConfig_Save(&snapshot);
}

void CallbackConfig_Get(CallbackConfig_TypeDef* config) {
  if (config == NULL)
    return;
  memset(config, 0, sizeof(*config));
  if (xSemaphoreTake(callbackConfigMutex, portMAX_DELAY) == pdTRUE) {
    *config = snapshot.config;
    (void)xSemaphoreGive(callbackConfigMutex);
  }
}

Platform_StatusTypeDef CallbackConfig_Set(const CallbackConfig_TypeDef* config) {
  if (callbackConfig_FieldsValid(config) == 0U)
    return PLATFORM_STATUS_ERROR;
  if (xSemaphoreTake(callbackConfigMutex, portMAX_DELAY) != pdTRUE)
    return PLATFORM_STATUS_ERROR;
  CallbackConfig_SnapshotTypeDef candidate = snapshot;
  candidate.config = *config;
  Platform_StatusTypeDef status = callbackConfig_Save(&candidate);
  (void)xSemaphoreGive(callbackConfigMutex);
  return status;
}

uint8_t CallbackConfig_IsTrustAnchorInUse(uint8_t trustAnchorId) {
  CallbackConfig_TypeDef config = {0};
  CallbackConfig_Get(&config);
  return ((config.enabled != 0U) && (config.trustAnchorId == trustAnchorId))
    ? 1U : 0U;
}
