/**
  ******************************************************************************
  * @file           : tls_trust_store.c
  * @brief          : Root certificates trusted by the HTTPS transport.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 30.07.2026
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

#include "tls_trust_store.h"

#include "FreeRTOS.h"
#include "flash_layout.h"
#include "semphr.h"
#include "w25q64.h"

#include "mbedtls/asn1.h"
#include "mbedtls/bignum.h"
#include "mbedtls/pk.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define TLS_TRUST_STORE_MAGIC   0x54525354UL
#define TLS_TRUST_STORE_VERSION 2U
#define TLS_TRUST_STORE_LEGACY_VERSION 1U

typedef struct {
  uint8_t occupied;
  uint8_t reserved;
  uint16_t derLength;
  char subject[TLS_TRUST_STORE_SUBJECT_SIZE];
  uint8_t der[TLS_TRUST_STORE_MAX_DER_SIZE];
} TlsTrustStore_AnchorTypeDef;

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  uint32_t generation;
  TlsTrustStore_AnchorTypeDef anchors[TLS_TRUST_STORE_MAX_ANCHORS];
  uint32_t crc;
} TlsTrustStore_SnapshotTypeDef;

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  uint32_t generation;
  TlsTrustStore_AnchorTypeDef anchors[3U];
  uint32_t crc;
} TlsTrustStore_LegacySnapshotTypeDef;

_Static_assert(
  sizeof(TlsTrustStore_SnapshotTypeDef)
    <= (FLASH_LAYOUT_TLS_TRUST_STORE_BANK_SECTORS * W25Q64_SECTOR_SIZE),
  "TLS trust store exceeds its Flash bank"
);

static TlsTrustStore_SnapshotTypeDef trustStoreSnapshot;
static uint32_t trustStoreActiveAddress;
static StaticSemaphore_t trustStoreMutexControlBlock;
static SemaphoreHandle_t trustStoreMutex;

static const char tlsTrustStore_DefaultPem[] =
  "-----BEGIN CERTIFICATE-----\n"
  "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\n"
  "98OwoX6EyneSMSy4kLGCenROmxMmtNVQZlR4rmA=\n"
  "-----END CERTIFICATE-----\n";

_Static_assert(
  sizeof(tlsTrustStore_DefaultPem) <= TLS_TRUST_STORE_MAX_DER_SIZE,
  "Default trust anchor exceeds its persistent slot"
);

static const char tlsTrustStore_DefaultSubject[] =
  "CN=SSL.com TLS RSA Root CA 2022,O=SSL Corporation,C=US";

static uint32_t tlsTrustStore_Crc(const void* data, size_t length) {
  const uint8_t* bytes = data;
  uint32_t crc = 0xFFFFFFFFUL;
  while (length-- != 0U) {
    crc ^= *bytes++;
    for (uint8_t bit = 0U; bit < 8U; ++bit)
      crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
  }
  return ~crc;
}

static uint8_t tlsTrustStore_IsSnapshotValid(
  const TlsTrustStore_SnapshotTypeDef* candidate
) {
  if ((candidate->magic != TLS_TRUST_STORE_MAGIC)
      || (candidate->version != TLS_TRUST_STORE_VERSION)
      || (candidate->crc != tlsTrustStore_Crc(
        candidate, offsetof(TlsTrustStore_SnapshotTypeDef, crc)
      ))) {
    return 0U;
  }
  for (uint8_t index = 0U; index < TLS_TRUST_STORE_MAX_ANCHORS; ++index) {
    const TlsTrustStore_AnchorTypeDef* anchor = &candidate->anchors[index];
    if ((anchor->occupied != 0U)
        && ((anchor->derLength == 0U)
          || (anchor->derLength > TLS_TRUST_STORE_MAX_DER_SIZE)
          || (memchr(anchor->subject, '\0', sizeof(anchor->subject)) == NULL))) {
      return 0U;
    }
  }
  return 1U;
}

static uint8_t tlsTrustStore_IsLegacySnapshotValid(
  const TlsTrustStore_LegacySnapshotTypeDef* candidate
) {
  return (candidate->magic == TLS_TRUST_STORE_MAGIC)
    && (candidate->version == TLS_TRUST_STORE_LEGACY_VERSION)
    && (candidate->crc == tlsTrustStore_Crc(
      candidate, offsetof(TlsTrustStore_LegacySnapshotTypeDef, crc)
    ));
}

static void tlsTrustStore_SetDefaultAnchor(
  TlsTrustStore_AnchorTypeDef* anchor
) {
  memset(anchor, 0, sizeof(*anchor));
  anchor->occupied = 1U;
  anchor->derLength = (uint16_t)sizeof(tlsTrustStore_DefaultPem);
  memcpy(anchor->der, tlsTrustStore_DefaultPem, sizeof(tlsTrustStore_DefaultPem));
  (void)strncpy(
    anchor->subject,
    tlsTrustStore_DefaultSubject,
    sizeof(anchor->subject) - 1U
  );
}

static Platform_StatusTypeDef tlsTrustStore_ReadSnapshot(
  uint32_t address,
  TlsTrustStore_SnapshotTypeDef* target
) {
  if (W25Q64_Read(address, target, sizeof(*target)) != PLATFORM_STATUS_OK)
    return PLATFORM_STATUS_ERROR;
  return tlsTrustStore_IsSnapshotValid(target) != 0U ? PLATFORM_STATUS_OK : PLATFORM_STATUS_ERROR;
}

static Platform_StatusTypeDef tlsTrustStore_Save(void) {
  uint32_t target =
    (trustStoreActiveAddress == FLASH_LAYOUT_TLS_TRUST_STORE_BANK_A)
      ? FLASH_LAYOUT_TLS_TRUST_STORE_BANK_B
      : FLASH_LAYOUT_TLS_TRUST_STORE_BANK_A;
  for (uint8_t sector = 0U;
       sector < FLASH_LAYOUT_TLS_TRUST_STORE_BANK_SECTORS;
       ++sector) {
    if (W25Q64_EraseSector(
          target + ((uint32_t)sector * W25Q64_SECTOR_SIZE)
        ) != PLATFORM_STATUS_OK) {
      return PLATFORM_STATUS_ERROR;
    }
  }
  if (W25Q64_Program(
        target, &trustStoreSnapshot, sizeof(trustStoreSnapshot)
      ) != PLATFORM_STATUS_OK) {
    return PLATFORM_STATUS_ERROR;
  }
  if (tlsTrustStore_ReadSnapshot(
        target, &trustStoreSnapshot
      ) != PLATFORM_STATUS_OK) {
    return PLATFORM_STATUS_ERROR;
  }
  trustStoreActiveAddress = target;
  return PLATFORM_STATUS_OK;
}

static TlsTrustStore_StatusTypeDef tlsTrustStore_CommitCandidate(void) {
  trustStoreSnapshot.magic = TLS_TRUST_STORE_MAGIC;
  trustStoreSnapshot.version = TLS_TRUST_STORE_VERSION;
  trustStoreSnapshot.reserved = 0U;
  ++trustStoreSnapshot.generation;
  trustStoreSnapshot.crc = tlsTrustStore_Crc(
    &trustStoreSnapshot, offsetof(TlsTrustStore_SnapshotTypeDef, crc)
  );
  if (tlsTrustStore_Save() == PLATFORM_STATUS_OK)
    return TLS_TRUST_STORE_STATUS_OK;
  (void)tlsTrustStore_ReadSnapshot(
    trustStoreActiveAddress, &trustStoreSnapshot
  );
  return TLS_TRUST_STORE_STATUS_STORAGE_ERROR;
}

static TlsTrustStore_StatusTypeDef tlsTrustStore_ValidateDer(
  const uint8_t* der,
  size_t length,
  TlsTrustStore_AnchorTypeDef* anchor
) {
  if ((der == NULL) || (length == 0U)
      || (length > TLS_TRUST_STORE_MAX_DER_SIZE) || (anchor == NULL)) {
    return TLS_TRUST_STORE_STATUS_INVALID_ARGUMENT;
  }
  mbedtls_x509_crt certificate;
  mbedtls_x509_crt_init(&certificate);
  int result = mbedtls_x509_crt_parse_der_nocopy(
    &certificate, der, length
  );
  if (result != 0) {
    mbedtls_x509_crt_free(&certificate);
    printf("Trust anchor parse failed: %d\r\n", result);
    if ((result == MBEDTLS_ERR_X509_ALLOC_FAILED)
        || (result == MBEDTLS_ERR_PK_ALLOC_FAILED)
        || ((-result & 0x007FU) == -MBEDTLS_ERR_MPI_ALLOC_FAILED)
        || ((-result & 0x007FU) == -MBEDTLS_ERR_ASN1_ALLOC_FAILED)) {
      return TLS_TRUST_STORE_STATUS_NO_MEMORY;
    }
    return TLS_TRUST_STORE_STATUS_INVALID_CERTIFICATE;
  }
  if ((mbedtls_x509_crt_get_ca_istrue(&certificate) != 1)
      || (mbedtls_x509_crt_check_key_usage(
        &certificate, MBEDTLS_X509_KU_KEY_CERT_SIGN
      ) != 0)) {
    mbedtls_x509_crt_free(&certificate);
    return TLS_TRUST_STORE_STATUS_NOT_CA;
  }
  char subject[TLS_TRUST_STORE_SUBJECT_SIZE];
  int subjectLength = mbedtls_x509_dn_gets(
    subject, sizeof(subject), &certificate.subject
  );
  if ((subjectLength <= 0)
      || ((size_t)subjectLength >= sizeof(subject))) {
    mbedtls_x509_crt_free(&certificate);
    return TLS_TRUST_STORE_STATUS_INVALID_CERTIFICATE;
  }
  /* Do not alter the live snapshot until every validation step succeeds. */
  memset(anchor, 0, sizeof(*anchor));
  anchor->occupied = 1U;
  anchor->derLength = (uint16_t)length;
  memcpy(anchor->subject, subject, (size_t)subjectLength + 1U);
  memcpy(anchor->der, der, length);
  mbedtls_x509_crt_free(&certificate);
  return TLS_TRUST_STORE_STATUS_OK;
}

Platform_StatusTypeDef TlsTrustStore_Init(void) {
  trustStoreMutex = xSemaphoreCreateMutexStatic(
    &trustStoreMutexControlBlock
  );
  if (trustStoreMutex == NULL)
    return PLATFORM_STATUS_ERROR;

  uint8_t firstValid = (tlsTrustStore_ReadSnapshot(
    FLASH_LAYOUT_TLS_TRUST_STORE_BANK_A, &trustStoreSnapshot
  ) == PLATFORM_STATUS_OK);
  uint32_t firstGeneration = trustStoreSnapshot.generation;
  uint8_t secondValid = (tlsTrustStore_ReadSnapshot(
    FLASH_LAYOUT_TLS_TRUST_STORE_BANK_B, &trustStoreSnapshot
  ) == PLATFORM_STATUS_OK);
  uint32_t secondGeneration = trustStoreSnapshot.generation;

  if ((firstValid != 0U) && ((secondValid == 0U)
      || (firstGeneration >= secondGeneration))) {
    if (tlsTrustStore_ReadSnapshot(
          FLASH_LAYOUT_TLS_TRUST_STORE_BANK_A, &trustStoreSnapshot
        ) != PLATFORM_STATUS_OK) {
      return PLATFORM_STATUS_ERROR;
    }
    trustStoreActiveAddress = FLASH_LAYOUT_TLS_TRUST_STORE_BANK_A;
    return PLATFORM_STATUS_OK;
  }
  if (secondValid != 0U) {
    trustStoreActiveAddress = FLASH_LAYOUT_TLS_TRUST_STORE_BANK_B;
    return PLATFORM_STATUS_OK;
  }

  /* The legacy image is smaller and shares the header and anchor layout.
   * Moving anchors backwards below prevents their source from being
   * overwritten while the in-place image is expanded to version 2. */
  TlsTrustStore_LegacySnapshotTypeDef* legacy =
    (TlsTrustStore_LegacySnapshotTypeDef*)&trustStoreSnapshot;
  uint8_t legacyFirstValid = (W25Q64_Read(
    FLASH_LAYOUT_TLS_TRUST_STORE_LEGACY_BANK_A,
    legacy,
    sizeof(*legacy)
  ) == PLATFORM_STATUS_OK) && tlsTrustStore_IsLegacySnapshotValid(legacy);
  uint32_t legacyFirstGeneration = legacy->generation;
  uint8_t legacySecondValid = (W25Q64_Read(
    FLASH_LAYOUT_TLS_TRUST_STORE_LEGACY_BANK_B,
    legacy,
    sizeof(*legacy)
  ) == PLATFORM_STATUS_OK) && tlsTrustStore_IsLegacySnapshotValid(legacy);
  uint32_t legacySecondGeneration = legacy->generation;

  if ((legacyFirstValid != 0U) || (legacySecondValid != 0U)) {
    uint8_t useFirst = (legacyFirstValid != 0U)
      && ((legacySecondValid == 0U)
        || (legacyFirstGeneration >= legacySecondGeneration));
    if (useFirst != 0U) {
      (void)W25Q64_Read(
        FLASH_LAYOUT_TLS_TRUST_STORE_LEGACY_BANK_A, legacy, sizeof(*legacy)
      );
    }
    uint32_t generation = legacy->generation;
    for (uint8_t index = 3U; index > 0U; --index)
      trustStoreSnapshot.anchors[index] = legacy->anchors[index - 1U];
    trustStoreSnapshot.generation = generation;
  } else {
    memset(&trustStoreSnapshot, 0, sizeof(trustStoreSnapshot));
  }
  tlsTrustStore_SetDefaultAnchor(&trustStoreSnapshot.anchors[0]);
  trustStoreActiveAddress = FLASH_LAYOUT_TLS_TRUST_STORE_BANK_B;
  return tlsTrustStore_CommitCandidate() == TLS_TRUST_STORE_STATUS_OK
    ? PLATFORM_STATUS_OK
    : PLATFORM_STATUS_ERROR;
}

uint8_t TlsTrustStore_Exists(uint8_t id) {
  if ((id > TLS_TRUST_STORE_MAX_PERSISTED) || (trustStoreMutex == NULL))
    return 0U;
  uint8_t exists = 0U;
  if (xSemaphoreTake(trustStoreMutex, portMAX_DELAY) == pdTRUE) {
    exists = trustStoreSnapshot.anchors[id].occupied != 0U;
    (void)xSemaphoreGive(trustStoreMutex);
  }
  return exists;
}

size_t TlsTrustStore_List(
  TlsTrustStore_InfoTypeDef* anchors,
  size_t capacity
) {
  if ((anchors == NULL) || (capacity == 0U) || (trustStoreMutex == NULL))
    return 0U;
  size_t count = 0U;
  if (xSemaphoreTake(trustStoreMutex, portMAX_DELAY) != pdTRUE)
    return 0U;
  for (uint8_t index = 0U;
       (index <= TLS_TRUST_STORE_MAX_PERSISTED) && (count < capacity);
       ++index) {
    const TlsTrustStore_AnchorTypeDef* anchor =
      &trustStoreSnapshot.anchors[index];
    if (anchor->occupied == 0U)
      continue;
    anchors[count].id = index;
    anchors[count].factory = 0U;
    anchors[count].derLength = anchor->derLength;
    (void)strncpy(
      anchors[count].subject,
      anchor->subject,
      sizeof(anchors[count].subject) - 1U
    );
    anchors[count].subject[sizeof(anchors[count].subject) - 1U] = '\0';
    ++count;
  }
  (void)xSemaphoreGive(trustStoreMutex);
  return count;
}

TlsTrustStore_StatusTypeDef TlsTrustStore_Parse(
  uint8_t id,
  mbedtls_x509_crt* certificate
) {
  if (certificate == NULL)
    return TLS_TRUST_STORE_STATUS_INVALID_ARGUMENT;
  if ((id > TLS_TRUST_STORE_MAX_PERSISTED) || (trustStoreMutex == NULL))
    return TLS_TRUST_STORE_STATUS_NOT_FOUND;
  if (xSemaphoreTake(trustStoreMutex, portMAX_DELAY) != pdTRUE)
    return TLS_TRUST_STORE_STATUS_STORAGE_ERROR;
  const TlsTrustStore_AnchorTypeDef* anchor =
    &trustStoreSnapshot.anchors[id];
  TlsTrustStore_StatusTypeDef status = TLS_TRUST_STORE_STATUS_NOT_FOUND;
  if (anchor->occupied != 0U) {
    status = (mbedtls_x509_crt_parse(
      certificate, anchor->der, anchor->derLength
    ) == 0)
      ? TLS_TRUST_STORE_STATUS_OK
      : TLS_TRUST_STORE_STATUS_INVALID_CERTIFICATE;
  }
  (void)xSemaphoreGive(trustStoreMutex);
  return status;
}

TlsTrustStore_StatusTypeDef TlsTrustStore_Add(
  const uint8_t* der,
  size_t length,
  uint8_t* assignedId
) {
  if (trustStoreMutex == NULL)
    return TLS_TRUST_STORE_STATUS_STORAGE_ERROR;
  if (xSemaphoreTake(trustStoreMutex, portMAX_DELAY) != pdTRUE)
    return TLS_TRUST_STORE_STATUS_STORAGE_ERROR;
  uint8_t index;
  for (index = 0U; index <= TLS_TRUST_STORE_MAX_PERSISTED; ++index) {
    if (trustStoreSnapshot.anchors[index].occupied == 0U)
      break;
  }
  if (index > TLS_TRUST_STORE_MAX_PERSISTED) {
    (void)xSemaphoreGive(trustStoreMutex);
    return TLS_TRUST_STORE_STATUS_FULL;
  }
  TlsTrustStore_StatusTypeDef status = tlsTrustStore_ValidateDer(
    der, length, &trustStoreSnapshot.anchors[index]
  );
  if (status == TLS_TRUST_STORE_STATUS_OK)
    status = tlsTrustStore_CommitCandidate();
  if ((status == TLS_TRUST_STORE_STATUS_OK) && (assignedId != NULL))
    *assignedId = index;
  (void)xSemaphoreGive(trustStoreMutex);
  return status;
}

TlsTrustStore_StatusTypeDef TlsTrustStore_Replace(
  uint8_t id,
  const uint8_t* der,
  size_t length
) {
  if ((id > TLS_TRUST_STORE_MAX_PERSISTED) || (trustStoreMutex == NULL))
    return TLS_TRUST_STORE_STATUS_NOT_FOUND;
  if (xSemaphoreTake(trustStoreMutex, portMAX_DELAY) != pdTRUE)
    return TLS_TRUST_STORE_STATUS_STORAGE_ERROR;
  if (trustStoreSnapshot.anchors[id].occupied == 0U) {
    (void)xSemaphoreGive(trustStoreMutex);
    return TLS_TRUST_STORE_STATUS_NOT_FOUND;
  }
  TlsTrustStore_StatusTypeDef status = tlsTrustStore_ValidateDer(
    der, length, &trustStoreSnapshot.anchors[id]
  );
  if (status == TLS_TRUST_STORE_STATUS_OK)
    status = tlsTrustStore_CommitCandidate();
  (void)xSemaphoreGive(trustStoreMutex);
  return status;
}

TlsTrustStore_StatusTypeDef TlsTrustStore_Delete(uint8_t id) {
  if ((id > TLS_TRUST_STORE_MAX_PERSISTED) || (trustStoreMutex == NULL))
    return TLS_TRUST_STORE_STATUS_NOT_FOUND;
  if (xSemaphoreTake(trustStoreMutex, portMAX_DELAY) != pdTRUE)
    return TLS_TRUST_STORE_STATUS_STORAGE_ERROR;
  if (trustStoreSnapshot.anchors[id].occupied == 0U) {
    (void)xSemaphoreGive(trustStoreMutex);
    return TLS_TRUST_STORE_STATUS_NOT_FOUND;
  }
  memset(
    &trustStoreSnapshot.anchors[id],
    0,
    sizeof(trustStoreSnapshot.anchors[id])
  );
  TlsTrustStore_StatusTypeDef status = tlsTrustStore_CommitCandidate();
  (void)xSemaphoreGive(trustStoreMutex);
  return status;
}

TlsTrustStore_StatusTypeDef TlsTrustStore_Reset(void) {
  if ((trustStoreMutex == NULL)
      || (xSemaphoreTake(trustStoreMutex, portMAX_DELAY) != pdTRUE)) {
    return TLS_TRUST_STORE_STATUS_STORAGE_ERROR;
  }
  uint32_t generation = trustStoreSnapshot.generation;
  memset(&trustStoreSnapshot, 0, sizeof(trustStoreSnapshot));
  trustStoreSnapshot.generation = generation;
  TlsTrustStore_StatusTypeDef status = tlsTrustStore_CommitCandidate();
  (void)xSemaphoreGive(trustStoreMutex);
  return status;
}
