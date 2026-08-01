/**
  ******************************************************************************
  * @file           : tls_trust_store.h
  * @brief          : Factory and persistent root CA trust-anchor storage.
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

#ifndef TLS_TRUST_STORE_H
#define TLS_TRUST_STORE_H

#include "main.h"
#include "mbedtls/x509_crt.h"

#include <stddef.h>
#include <stdint.h>

#define TLS_TRUST_STORE_FACTORY_ID          0U
#define TLS_TRUST_STORE_MAX_PERSISTED       3U
#define TLS_TRUST_STORE_MAX_DER_SIZE        2048U
#define TLS_TRUST_STORE_SUBJECT_SIZE        128U
#define TLS_TRUST_STORE_MAX_ANCHORS \
  (TLS_TRUST_STORE_MAX_PERSISTED + 1U)

typedef enum {
  TLS_TRUST_STORE_STATUS_OK = 0,
  TLS_TRUST_STORE_STATUS_INVALID_ARGUMENT,
  TLS_TRUST_STORE_STATUS_INVALID_CERTIFICATE,
  TLS_TRUST_STORE_STATUS_NO_MEMORY,
  TLS_TRUST_STORE_STATUS_NOT_CA,
  TLS_TRUST_STORE_STATUS_NOT_FOUND,
  TLS_TRUST_STORE_STATUS_FULL,
  TLS_TRUST_STORE_STATUS_FACTORY_PROTECTED,
  TLS_TRUST_STORE_STATUS_STORAGE_ERROR
} TlsTrustStore_StatusTypeDef;

/**
  * @brief Public metadata for one trust anchor.
  * @param id (uint8_t) Stable anchor ID used by resource configuration.
  * @param factory (uint8_t) Nonzero for the compiled recovery anchor.
  * @param derLength (uint16_t) Encoded certificate size; zero for the factory
  *        PEM anchor.
  * @param subject (char[TLS_TRUST_STORE_SUBJECT_SIZE]) X.509 subject name.
  */
typedef struct {
  uint8_t id;
  uint8_t factory;
  uint16_t derLength;
  char subject[TLS_TRUST_STORE_SUBJECT_SIZE];
} TlsTrustStore_InfoTypeDef;

/**
  * @brief Load the newest valid persistent A/B snapshot.
  * @retval (HAL_StatusTypeDef) HAL_OK when the store is ready.
  */
HAL_StatusTypeDef TlsTrustStore_Init(void);

/**
  * @brief Report whether an anchor ID currently exists.
  * @param id (uint8_t) Factory ID 0 or a persistent slot ID.
  * @retval (uint8_t) Nonzero when the anchor exists.
  */
uint8_t TlsTrustStore_Exists(uint8_t id);

/**
  * @brief Copy metadata for all active anchors, factory anchor first.
  * @param anchors (TlsTrustStore_InfoTypeDef*) Output array.
  * @param capacity (size_t) Number of available output elements.
  * @retval (size_t) Number of anchors copied.
  */
size_t TlsTrustStore_List(
  TlsTrustStore_InfoTypeDef* anchors,
  size_t capacity
);

/**
  * @brief Parse one selected anchor into an initialized Mbed TLS object.
  * @param id (uint8_t) Anchor ID.
  * @param certificate (mbedtls_x509_crt*) Initialized output certificate.
  * @retval (TlsTrustStore_StatusTypeDef) OK when parsing succeeded.
  * @note The caller must hold the global TLS platform lock. Certificate data
  *       is copied by Mbed TLS before the store mutex is released.
  */
TlsTrustStore_StatusTypeDef TlsTrustStore_Parse(
  uint8_t id,
  mbedtls_x509_crt* certificate
);

/**
  * @brief Add a DER-encoded CA certificate to the first free slot.
  * @param der (const uint8_t*) DER certificate bytes.
  * @param length (size_t) DER length, at most TLS_TRUST_STORE_MAX_DER_SIZE.
  * @param assignedId (uint8_t*) Optional assigned persistent ID.
  * @retval (TlsTrustStore_StatusTypeDef) Validation or storage result.
  * @note The caller must hold the global TLS platform lock.
  */
TlsTrustStore_StatusTypeDef TlsTrustStore_Add(
  const uint8_t* der,
  size_t length,
  uint8_t* assignedId
);

/**
  * @brief Replace one persistent trust anchor with a DER CA certificate.
  * @param id (uint8_t) Persistent ID 1 through 3.
  * @param der (const uint8_t*) DER certificate bytes.
  * @param length (size_t) DER length.
  * @retval (TlsTrustStore_StatusTypeDef) Validation or storage result.
  * @note The caller must hold the global TLS platform lock.
  */
TlsTrustStore_StatusTypeDef TlsTrustStore_Replace(
  uint8_t id,
  const uint8_t* der,
  size_t length
);

/**
  * @brief Delete one persistent trust anchor.
  * @param id (uint8_t) Persistent ID 1 through 3.
  * @retval (TlsTrustStore_StatusTypeDef) OK or an ID/storage error.
  */
TlsTrustStore_StatusTypeDef TlsTrustStore_Delete(uint8_t id);

/**
  * @brief Clear all persistent anchors, retaining factory anchor ID 0.
  * @retval (TlsTrustStore_StatusTypeDef) OK or a storage error.
  */
TlsTrustStore_StatusTypeDef TlsTrustStore_Reset(void);

#endif /* TLS_TRUST_STORE_H */
