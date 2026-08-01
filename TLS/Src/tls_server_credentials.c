/**
  ******************************************************************************
  * @file           : tls_server_credentials.c
  * @brief          : Runtime-updatable HTTPS management server certificate/key.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 01.08.2026
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

#include "tls_server_credentials.h"

#include "flash_layout.h"
#include "management_server_credentials.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/pk.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/x509_crt.h"
#include "tls_platform.h"
#include "w25q64.h"

#include <string.h>

#define TLS_SERVER_CREDENTIALS_MAGIC    0x54534352UL
#define TLS_SERVER_CREDENTIALS_VERSION  3U
#define TLS_SERVER_CREDENTIALS_SECTOR_A \
  FLASH_LAYOUT_TLS_SERVER_CREDENTIALS_SECTOR_A
#define TLS_SERVER_CREDENTIALS_SECTOR_B \
  FLASH_LAYOUT_TLS_SERVER_CREDENTIALS_SECTOR_B

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  uint32_t generation;
  uint16_t certificateLength;
  uint16_t keyLength;
  uint8_t certificate[TLS_SERVER_CREDENTIALS_MAX_CERTIFICATE_SIZE];
  uint8_t key[TLS_SERVER_CREDENTIALS_MAX_KEY_SIZE];
  uint32_t crc;
} TlsServerCredentials_SnapshotTypeDef;

_Static_assert(
  sizeof(TlsServerCredentials_SnapshotTypeDef) <= W25Q64_SECTOR_SIZE,
  "TLS server credentials exceed their Flash bank"
);

static TlsServerCredentials_SnapshotTypeDef snapshot;
static TlsServerCredentials_SnapshotTypeDef verification;
static uint32_t activeAddress;
static uint8_t hasValidSnapshot;

static uint8_t pendingCertificate[TLS_SERVER_CREDENTIALS_MAX_CERTIFICATE_SIZE];
static size_t pendingCertificateLength;
static uint8_t certificateStaged;
static uint8_t pendingKey[TLS_SERVER_CREDENTIALS_MAX_KEY_SIZE];
static size_t pendingKeyLength;
static uint8_t keyStaged;

static void tlsServerCredentials_ClearStaging(void) {
  mbedtls_platform_zeroize(
    pendingCertificate, sizeof(pendingCertificate)
  );
  mbedtls_platform_zeroize(pendingKey, sizeof(pendingKey));
  pendingCertificateLength = 0U;
  pendingKeyLength = 0U;
  certificateStaged = 0U;
  keyStaged = 0U;
}

static uint32_t tlsServerCredentials_Crc(const void* data, size_t length) {
  const uint8_t* bytes = data;
  uint32_t crc = 0xFFFFFFFFUL;
  while (length-- != 0U) {
    crc ^= *bytes++;
    for (uint8_t bit = 0U; bit < 8U; ++bit)
      crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
  }
  return ~crc;
}

static uint8_t tlsServerCredentials_IsValid(
  const TlsServerCredentials_SnapshotTypeDef* candidate
) {
  return ((candidate->magic == TLS_SERVER_CREDENTIALS_MAGIC)
      && (candidate->version == TLS_SERVER_CREDENTIALS_VERSION)
      && (candidate->certificateLength <= TLS_SERVER_CREDENTIALS_MAX_CERTIFICATE_SIZE)
      && (candidate->keyLength <= TLS_SERVER_CREDENTIALS_MAX_KEY_SIZE)
      && (candidate->crc == tlsServerCredentials_Crc(
        candidate, offsetof(TlsServerCredentials_SnapshotTypeDef, crc)
      )))
    ? 1U
    : 0U;
}

static HAL_StatusTypeDef tlsServerCredentials_Save(
  TlsServerCredentials_SnapshotTypeDef* candidate
) {
  uint32_t target = (activeAddress == TLS_SERVER_CREDENTIALS_SECTOR_A)
    ? TLS_SERVER_CREDENTIALS_SECTOR_B
    : TLS_SERVER_CREDENTIALS_SECTOR_A;
  candidate->magic = TLS_SERVER_CREDENTIALS_MAGIC;
  candidate->version = TLS_SERVER_CREDENTIALS_VERSION;
  ++candidate->generation;
  candidate->crc = tlsServerCredentials_Crc(
    candidate, offsetof(TlsServerCredentials_SnapshotTypeDef, crc)
  );
  if ((W25Q64_EraseSector(target) != HAL_OK)
      || (W25Q64_Program(target, candidate, sizeof(*candidate)) != HAL_OK))
    return HAL_ERROR;
  if ((W25Q64_Read(target, &verification, sizeof(verification)) != HAL_OK)
      || (tlsServerCredentials_IsValid(&verification) == 0U)
      || (verification.generation != candidate->generation))
    return HAL_ERROR;
  snapshot = *candidate;
  activeAddress = target;
  hasValidSnapshot = 1U;
  return HAL_OK;
}

/**
  * @brief Parse and pair the given certificate/key bytes, and if they match,
  *        commit them to Flash and clear the staging buffers.
  * @note Must be called with TlsPlatform_Lock() already held.
  */
static TlsServerCredentials_StatusTypeDef tlsServerCredentials_TryActivate(
  void
) {
  const uint8_t* certificateData;
  size_t certificateLength;
  if (certificateStaged != 0U) {
    certificateData = pendingCertificate;
    certificateLength = pendingCertificateLength;
  } else if (hasValidSnapshot != 0U) {
    certificateData = snapshot.certificate;
    certificateLength = snapshot.certificateLength;
  } else {
    return TLS_SERVER_CREDENTIALS_STATUS_PENDING;
  }

  const uint8_t* keyData;
  size_t keyLength;
  if (keyStaged != 0U) {
    keyData = pendingKey;
    keyLength = pendingKeyLength;
  } else if (hasValidSnapshot != 0U) {
    keyData = snapshot.key;
    keyLength = snapshot.keyLength;
  } else {
    return TLS_SERVER_CREDENTIALS_STATUS_PENDING;
  }

  mbedtls_x509_crt certificate;
  mbedtls_pk_context key;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context random;
  mbedtls_x509_crt_init(&certificate);
  mbedtls_pk_init(&key);
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&random);

  static const uint8_t personalization[] = "stm32-tls-credentials";
  int result = mbedtls_ctr_drbg_seed(
    &random,
    mbedtls_entropy_func,
    &entropy,
    personalization,
    sizeof(personalization) - 1U
  );
  if (result == 0)
    result = mbedtls_x509_crt_parse(&certificate, certificateData, certificateLength);
  if (result == 0) {
    result = mbedtls_pk_parse_key(
      &key, keyData, keyLength, NULL, 0U, mbedtls_ctr_drbg_random, &random
    );
  }
  uint8_t paired = ((result == 0)
    && (mbedtls_pk_check_pair(
          &certificate.pk, &key, mbedtls_ctr_drbg_random, &random
        ) == 0))
    ? 1U
    : 0U;

  mbedtls_x509_crt_free(&certificate);
  mbedtls_pk_free(&key);
  mbedtls_ctr_drbg_free(&random);
  mbedtls_entropy_free(&entropy);

  if (result != 0) {
    tlsServerCredentials_ClearStaging();
    return TLS_SERVER_CREDENTIALS_STATUS_INVALID_DATA;
  }
  if (paired == 0U) {
    if ((certificateStaged == 0U) || (keyStaged == 0U))
      return TLS_SERVER_CREDENTIALS_STATUS_PENDING;
    tlsServerCredentials_ClearStaging();
    return TLS_SERVER_CREDENTIALS_STATUS_MISMATCH;
  }

  TlsServerCredentials_SnapshotTypeDef candidate = snapshot;
  candidate.certificateLength = (uint16_t)certificateLength;
  memset(candidate.certificate, 0, sizeof(candidate.certificate));
  memcpy(candidate.certificate, certificateData, certificateLength);
  candidate.keyLength = (uint16_t)keyLength;
  memset(candidate.key, 0, sizeof(candidate.key));
  memcpy(candidate.key, keyData, keyLength);
  TlsServerCredentials_StatusTypeDef status =
    (tlsServerCredentials_Save(&candidate) == HAL_OK)
    ? TLS_SERVER_CREDENTIALS_STATUS_ACTIVATED
    : TLS_SERVER_CREDENTIALS_STATUS_STORAGE_ERROR;
  tlsServerCredentials_ClearStaging();
  return status;
}

HAL_StatusTypeDef TlsServerCredentials_Init(void) {
  /* Reuse the persistent working snapshots to keep startup RAM bounded. */
  uint8_t firstValid = (W25Q64_Read(
    TLS_SERVER_CREDENTIALS_SECTOR_A, &snapshot, sizeof(snapshot)
  ) == HAL_OK) && tlsServerCredentials_IsValid(&snapshot);
  uint8_t secondValid = (W25Q64_Read(
    TLS_SERVER_CREDENTIALS_SECTOR_B, &verification, sizeof(verification)
  ) == HAL_OK) && tlsServerCredentials_IsValid(&verification);
  if ((firstValid != 0U)
      && ((secondValid == 0U)
        || (snapshot.generation >= verification.generation))) {
    activeAddress = TLS_SERVER_CREDENTIALS_SECTOR_A;
    hasValidSnapshot = 1U;
  } else if (secondValid != 0U) {
    snapshot = verification;
    activeAddress = TLS_SERVER_CREDENTIALS_SECTOR_B;
    hasValidSnapshot = 1U;
  } else {
    memset(&snapshot, 0, sizeof(snapshot));
    activeAddress = TLS_SERVER_CREDENTIALS_SECTOR_B;
    hasValidSnapshot = 0U;
  }
  return HAL_OK;
}

TlsServerCredentials_StatusTypeDef TlsServerCredentials_StageCertificate(
  const uint8_t* der,
  size_t length
) {
  if ((der == NULL) || (length == 0U)
      || (length > TLS_SERVER_CREDENTIALS_MAX_CERTIFICATE_SIZE))
    return TLS_SERVER_CREDENTIALS_STATUS_INVALID_DATA;
  if (TlsPlatform_Lock() != HAL_OK)
    return TLS_SERVER_CREDENTIALS_STATUS_STORAGE_ERROR;
  memcpy(pendingCertificate, der, length);
  pendingCertificateLength = length;
  certificateStaged = 1U;
  TlsServerCredentials_StatusTypeDef status = tlsServerCredentials_TryActivate();
  TlsPlatform_Unlock();
  return status;
}

TlsServerCredentials_StatusTypeDef TlsServerCredentials_StagePrivateKey(
  const uint8_t* der,
  size_t length
) {
  if ((der == NULL) || (length == 0U)
      || (length > TLS_SERVER_CREDENTIALS_MAX_KEY_SIZE))
    return TLS_SERVER_CREDENTIALS_STATUS_INVALID_DATA;
  if (TlsPlatform_Lock() != HAL_OK)
    return TLS_SERVER_CREDENTIALS_STATUS_STORAGE_ERROR;
  memcpy(pendingKey, der, length);
  pendingKeyLength = length;
  keyStaged = 1U;
  TlsServerCredentials_StatusTypeDef status = tlsServerCredentials_TryActivate();
  TlsPlatform_Unlock();
  return status;
}

void TlsServerCredentials_GetCertificate(
  const uint8_t** data,
  size_t* length
) {
  if ((data == NULL) || (length == NULL))
    return;
  if (hasValidSnapshot != 0U) {
    *data = snapshot.certificate;
    *length = snapshot.certificateLength;
  } else {
    *data = managementServerCertificate;
    *length = sizeof(managementServerCertificate);
  }
}

void TlsServerCredentials_GetPrivateKey(
  const uint8_t** data,
  size_t* length
) {
  if ((data == NULL) || (length == NULL))
    return;
  if (hasValidSnapshot != 0U) {
    *data = snapshot.key;
    *length = snapshot.keyLength;
  } else {
    *data = managementServerPrivateKey;
    *length = sizeof(managementServerPrivateKey);
  }
}
