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
#define TLS_TRUST_STORE_VERSION 1U

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
  TlsTrustStore_AnchorTypeDef anchors[TLS_TRUST_STORE_MAX_PERSISTED];
  uint32_t crc;
} TlsTrustStore_SnapshotTypeDef;

_Static_assert(
  sizeof(TlsTrustStore_SnapshotTypeDef)
    <= (FLASH_LAYOUT_TLS_TRUST_STORE_BANK_SECTORS * W25Q64_SECTOR_SIZE),
  "TLS trust store exceeds its Flash bank"
);

static TlsTrustStore_SnapshotTypeDef trustStoreSnapshot;
static TlsTrustStore_SnapshotTypeDef trustStoreCandidate;
static uint32_t trustStoreActiveAddress;
static StaticSemaphore_t trustStoreMutexControlBlock;
static SemaphoreHandle_t trustStoreMutex;

static const char tlsTrustStore_FactorySubject[] =
  "CN=USERTrust RSA Certification Authority,O=The USERTRUST Network";

/* USERTrust RSA Certification Authority, valid until 18 January 2038. */
const char tlsTrustStore_UserTrustRsa[] =
  "-----BEGIN CERTIFICATE-----\n"
  "MIIF3jCCA8agAwIBAgIQAf1tMPyjylGoG7xkDjUDLTANBgkqhkiG9w0BAQwFADCB\n"
  "iDELMAkGA1UEBhMCVVMxEzARBgNVBAgTCk5ldyBKZXJzZXkxFDASBgNVBAcTC0pl\n"
  "cnNleSBDaXR5MR4wHAYDVQQKExVUaGUgVVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNV\n"
  "BAMTJVVTRVJUcnVzdCBSU0EgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwHhcNMTAw\n"
  "MjAxMDAwMDAwWhcNMzgwMTE4MjM1OTU5WjCBiDELMAkGA1UEBhMCVVMxEzARBgNV\n"
  "BAgTCk5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNleSBDaXR5MR4wHAYDVQQKExVU\n"
  "aGUgVVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMTJVVTRVJUcnVzdCBSU0EgQ2Vy\n"
  "dGlmaWNhdGlvbiBBdXRob3JpdHkwggIiMA0GCSqGSIb3DQEBAQUAA4ICDwAwggIK\n"
  "AoICAQCAEmUXNg7D2wiz0KxXDXbtzSfTTK1Qg2HiqiBNCS1kCdzOiZ/MPans9s/B\n"
  "3PHTsdZ7NygRK0faOca8Ohm0X6a9fZ2jY0K2dvKpOyuR+OJv0OwWIJAJPuLodMkY\n"
  "tJHUYmTbf6MG8YgYapAiPLz+E/CHFHv25B+O1ORRxhFnRghRy4YUVD+8M/5+bJz/\n"
  "Fp0YvVGONaanZshyZ9shZrHUm3gDwFA66Mzw3LyeTP6vBZY1H1dat//O+T23LLb2\n"
  "VN3I5xI6Ta5MirdcmrS3ID3KfyI0rn47aGYBROcBTkZTmzNg95S+UzeQc0PzMsNT\n"
  "79uq/nROacdrjGCT3sTHDN/hMq7MkztReJVni+49Vv4M0GkPGw/zJSZrM233bkf6\n"
  "c0Plfg6lZrEpfDKEY1WJxA3Bk1QwGROs0303p+tdOmw1XNtB1xLaqUkL39iAigmT\n"
  "Yo61Zs8liM2EuLE/pDkP2QKe6xJMlXzzawWpXhaDzLhn4ugTncxbgtNMs+1b/97l\n"
  "c6wjOy0AvzVVdAlJ2ElYGn+SNuZRkg7zJn0cTRe8yexDJtC/QV9AqURE9JnnV4ee\n"
  "UB9XVKg+/XRjL7FQZQnmWEIuQxpMtPAlR1n6BB6T1CZGSlCBst6+eLf8ZxXhyVeE\n"
  "Hg9j1uliutZfVS7qXMYoCAQlObgOK6nyTJccBz8NUvXt7y+CDwIDAQABo0IwQDAd\n"
  "BgNVHQ4EFgQUU3m/WqorSs9UgOHYm8Cd8rIDZsswDgYDVR0PAQH/BAQDAgEGMA8G\n"
  "A1UdEwEB/wQFMAMBAf8wDQYJKoZIhvcNAQEMBQADggIBAFzUfA3P9wF9QZllDHPF\n"
  "Up/L+M+ZBn8b2kMVn54CVVeWFPFSPCeHlCjtHzoBN6J2/FNQwISbxmtOuowhT6KO\n"
  "VWKR82kV2LyI48SqC/3vqOlLVSoGIG1VeCkZ7l8wXEskEVX/JJpuXior7gtNn3/3\n"
  "ATiUFJVDBwn7YKnuHKsSjKCaXqeYalltiz8I+8jRRa8YFWSQEg9zKC7F4iRO/Fjs\n"
  "8PRF/iKz6y+O0tlFYQXBl2+odnKPi4w2r78NBc5xjeambx9spnFixdjQg3IM8WcR\n"
  "iQycE0xyNN+81XHfqnHd4blsjDwSXWXavVcStkNr/+XeTWYRUc+ZruwXtuhxkYze\n"
  "Sf7dNXGiFSeUHM9h4ya7b6NnJSFd5t0dCy5oGzuCr+yDZ4XUmFF0sbmZgIn/f3gZ\n"
  "XHlKYC6SQK5MNyosycdiyA5d9zZbyuAlJQG03RoHnHcAP9Dc1ew91Pq7P8yF1m9/\n"
  "qS3fuQL39ZeatTXaw2ewh0qpKJ4jjv9cJ2vhsE/zB+4ALtRZh8tSQZXq9EfX7mRB\n"
  "VXyNWQKV3WKdwrnuWih0hKWbt5DHDAff9Yk2dDLWKMGwsAvgnEzDHNb842m1R0aB\n"
  "L6KCq9NjRHDEjf8tM7qtj3u1cIiuPhnPQCjY/MiQu12ZIvVS5ljFH4gxQ+6IHdfG\n"
  "jjxDah2nGN59PRbxYvnKkKj9\n"
  "-----END CERTIFICATE-----\n";

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
  for (uint8_t index = 0U; index < TLS_TRUST_STORE_MAX_PERSISTED; ++index) {
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

static HAL_StatusTypeDef tlsTrustStore_ReadSnapshot(
  uint32_t address,
  TlsTrustStore_SnapshotTypeDef* target
) {
  if (W25Q64_Read(address, target, sizeof(*target)) != HAL_OK)
    return HAL_ERROR;
  return tlsTrustStore_IsSnapshotValid(target) != 0U ? HAL_OK : HAL_ERROR;
}

static HAL_StatusTypeDef tlsTrustStore_Save(
  const TlsTrustStore_SnapshotTypeDef* candidate
) {
  uint32_t target =
    (trustStoreActiveAddress == FLASH_LAYOUT_TLS_TRUST_STORE_BANK_A)
      ? FLASH_LAYOUT_TLS_TRUST_STORE_BANK_B
      : FLASH_LAYOUT_TLS_TRUST_STORE_BANK_A;
  for (uint8_t sector = 0U;
       sector < FLASH_LAYOUT_TLS_TRUST_STORE_BANK_SECTORS;
       ++sector) {
    if (W25Q64_EraseSector(
          target + ((uint32_t)sector * W25Q64_SECTOR_SIZE)
        ) != HAL_OK) {
      return HAL_ERROR;
    }
  }
  if (W25Q64_Program(target, candidate, sizeof(*candidate)) != HAL_OK)
    return HAL_ERROR;
  if (tlsTrustStore_ReadSnapshot(target, &trustStoreCandidate) != HAL_OK)
    return HAL_ERROR;
  trustStoreSnapshot = trustStoreCandidate;
  trustStoreActiveAddress = target;
  return HAL_OK;
}

static TlsTrustStore_StatusTypeDef tlsTrustStore_CommitCandidate(void) {
  trustStoreCandidate.magic = TLS_TRUST_STORE_MAGIC;
  trustStoreCandidate.version = TLS_TRUST_STORE_VERSION;
  trustStoreCandidate.reserved = 0U;
  ++trustStoreCandidate.generation;
  trustStoreCandidate.crc = tlsTrustStore_Crc(
    &trustStoreCandidate, offsetof(TlsTrustStore_SnapshotTypeDef, crc)
  );
  return (tlsTrustStore_Save(&trustStoreCandidate) == HAL_OK)
    ? TLS_TRUST_STORE_STATUS_OK
    : TLS_TRUST_STORE_STATUS_STORAGE_ERROR;
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
  if ((mbedtls_x509_crt_get_ca_istrue(&certificate) == 0)
      || (mbedtls_x509_crt_check_key_usage(
        &certificate, MBEDTLS_X509_KU_KEY_CERT_SIGN
      ) != 0)) {
    mbedtls_x509_crt_free(&certificate);
    return TLS_TRUST_STORE_STATUS_NOT_CA;
  }
  memset(anchor, 0, sizeof(*anchor));
  int subjectLength = mbedtls_x509_dn_gets(
    anchor->subject, sizeof(anchor->subject), &certificate.subject
  );
  if ((subjectLength <= 0)
      || ((size_t)subjectLength >= sizeof(anchor->subject))) {
    mbedtls_x509_crt_free(&certificate);
    return TLS_TRUST_STORE_STATUS_INVALID_CERTIFICATE;
  }
  anchor->occupied = 1U;
  anchor->derLength = (uint16_t)length;
  memcpy(anchor->der, der, length);
  mbedtls_x509_crt_free(&certificate);
  return TLS_TRUST_STORE_STATUS_OK;
}

HAL_StatusTypeDef TlsTrustStore_Init(void) {
  trustStoreMutex = xSemaphoreCreateMutexStatic(
    &trustStoreMutexControlBlock
  );
  if (trustStoreMutex == NULL)
    return HAL_ERROR;

  uint8_t firstValid = (tlsTrustStore_ReadSnapshot(
    FLASH_LAYOUT_TLS_TRUST_STORE_BANK_A, &trustStoreSnapshot
  ) == HAL_OK);
  uint32_t firstGeneration = trustStoreSnapshot.generation;
  uint8_t secondValid = (tlsTrustStore_ReadSnapshot(
    FLASH_LAYOUT_TLS_TRUST_STORE_BANK_B, &trustStoreCandidate
  ) == HAL_OK);

  if ((firstValid != 0U) && ((secondValid == 0U)
      || (firstGeneration >= trustStoreCandidate.generation))) {
    if (tlsTrustStore_ReadSnapshot(
          FLASH_LAYOUT_TLS_TRUST_STORE_BANK_A, &trustStoreSnapshot
        ) != HAL_OK) {
      return HAL_ERROR;
    }
    trustStoreActiveAddress = FLASH_LAYOUT_TLS_TRUST_STORE_BANK_A;
    return HAL_OK;
  }
  if (secondValid != 0U) {
    trustStoreSnapshot = trustStoreCandidate;
    trustStoreActiveAddress = FLASH_LAYOUT_TLS_TRUST_STORE_BANK_B;
    return HAL_OK;
  }

  memset(&trustStoreCandidate, 0, sizeof(trustStoreCandidate));
  trustStoreActiveAddress = FLASH_LAYOUT_TLS_TRUST_STORE_BANK_B;
  return tlsTrustStore_CommitCandidate() == TLS_TRUST_STORE_STATUS_OK
    ? HAL_OK
    : HAL_ERROR;
}

uint8_t TlsTrustStore_Exists(uint8_t id) {
  if (id == TLS_TRUST_STORE_FACTORY_ID)
    return 1U;
  if ((id > TLS_TRUST_STORE_MAX_PERSISTED) || (trustStoreMutex == NULL))
    return 0U;
  uint8_t exists = 0U;
  if (xSemaphoreTake(trustStoreMutex, portMAX_DELAY) == pdTRUE) {
    exists = trustStoreSnapshot.anchors[id - 1U].occupied != 0U;
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
  anchors[count].id = TLS_TRUST_STORE_FACTORY_ID;
  anchors[count].factory = 1U;
  anchors[count].derLength = 0U;
  (void)strncpy(
    anchors[count].subject,
    tlsTrustStore_FactorySubject,
    sizeof(anchors[count].subject) - 1U
  );
  anchors[count].subject[sizeof(anchors[count].subject) - 1U] = '\0';
  ++count;
  if (xSemaphoreTake(trustStoreMutex, portMAX_DELAY) != pdTRUE)
    return count;
  for (uint8_t index = 0U;
       (index < TLS_TRUST_STORE_MAX_PERSISTED) && (count < capacity);
       ++index) {
    const TlsTrustStore_AnchorTypeDef* anchor =
      &trustStoreSnapshot.anchors[index];
    if (anchor->occupied == 0U)
      continue;
    anchors[count].id = index + 1U;
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
  if (id == TLS_TRUST_STORE_FACTORY_ID) {
    int result = mbedtls_x509_crt_parse(
      certificate,
      (const uint8_t*)tlsTrustStore_UserTrustRsa,
      strlen(tlsTrustStore_UserTrustRsa) + 1U
    );
    return result == 0
      ? TLS_TRUST_STORE_STATUS_OK
      : TLS_TRUST_STORE_STATUS_INVALID_CERTIFICATE;
  }
  if ((id > TLS_TRUST_STORE_MAX_PERSISTED) || (trustStoreMutex == NULL))
    return TLS_TRUST_STORE_STATUS_NOT_FOUND;
  if (xSemaphoreTake(trustStoreMutex, portMAX_DELAY) != pdTRUE)
    return TLS_TRUST_STORE_STATUS_STORAGE_ERROR;
  const TlsTrustStore_AnchorTypeDef* anchor =
    &trustStoreSnapshot.anchors[id - 1U];
  TlsTrustStore_StatusTypeDef status = TLS_TRUST_STORE_STATUS_NOT_FOUND;
  if (anchor->occupied != 0U) {
    status = (mbedtls_x509_crt_parse_der(
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
  for (index = 0U; index < TLS_TRUST_STORE_MAX_PERSISTED; ++index) {
    if (trustStoreSnapshot.anchors[index].occupied == 0U)
      break;
  }
  if (index == TLS_TRUST_STORE_MAX_PERSISTED) {
    (void)xSemaphoreGive(trustStoreMutex);
    return TLS_TRUST_STORE_STATUS_FULL;
  }
  trustStoreCandidate = trustStoreSnapshot;
  TlsTrustStore_StatusTypeDef status = tlsTrustStore_ValidateDer(
    der, length, &trustStoreCandidate.anchors[index]
  );
  if (status == TLS_TRUST_STORE_STATUS_OK)
    status = tlsTrustStore_CommitCandidate();
  if ((status == TLS_TRUST_STORE_STATUS_OK) && (assignedId != NULL))
    *assignedId = index + 1U;
  (void)xSemaphoreGive(trustStoreMutex);
  return status;
}

TlsTrustStore_StatusTypeDef TlsTrustStore_Replace(
  uint8_t id,
  const uint8_t* der,
  size_t length
) {
  if (id == TLS_TRUST_STORE_FACTORY_ID)
    return TLS_TRUST_STORE_STATUS_FACTORY_PROTECTED;
  if ((id > TLS_TRUST_STORE_MAX_PERSISTED) || (trustStoreMutex == NULL))
    return TLS_TRUST_STORE_STATUS_NOT_FOUND;
  if (xSemaphoreTake(trustStoreMutex, portMAX_DELAY) != pdTRUE)
    return TLS_TRUST_STORE_STATUS_STORAGE_ERROR;
  if (trustStoreSnapshot.anchors[id - 1U].occupied == 0U) {
    (void)xSemaphoreGive(trustStoreMutex);
    return TLS_TRUST_STORE_STATUS_NOT_FOUND;
  }
  trustStoreCandidate = trustStoreSnapshot;
  TlsTrustStore_StatusTypeDef status = tlsTrustStore_ValidateDer(
    der, length, &trustStoreCandidate.anchors[id - 1U]
  );
  if (status == TLS_TRUST_STORE_STATUS_OK)
    status = tlsTrustStore_CommitCandidate();
  (void)xSemaphoreGive(trustStoreMutex);
  return status;
}

TlsTrustStore_StatusTypeDef TlsTrustStore_Delete(uint8_t id) {
  if (id == TLS_TRUST_STORE_FACTORY_ID)
    return TLS_TRUST_STORE_STATUS_FACTORY_PROTECTED;
  if ((id > TLS_TRUST_STORE_MAX_PERSISTED) || (trustStoreMutex == NULL))
    return TLS_TRUST_STORE_STATUS_NOT_FOUND;
  if (xSemaphoreTake(trustStoreMutex, portMAX_DELAY) != pdTRUE)
    return TLS_TRUST_STORE_STATUS_STORAGE_ERROR;
  if (trustStoreSnapshot.anchors[id - 1U].occupied == 0U) {
    (void)xSemaphoreGive(trustStoreMutex);
    return TLS_TRUST_STORE_STATUS_NOT_FOUND;
  }
  trustStoreCandidate = trustStoreSnapshot;
  memset(
    &trustStoreCandidate.anchors[id - 1U],
    0,
    sizeof(trustStoreCandidate.anchors[id - 1U])
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
  memset(&trustStoreCandidate, 0, sizeof(trustStoreCandidate));
  trustStoreCandidate.generation = trustStoreSnapshot.generation;
  TlsTrustStore_StatusTypeDef status = tlsTrustStore_CommitCandidate();
  (void)xSemaphoreGive(trustStoreMutex);
  return status;
}
