/**
  ******************************************************************************
  * @file           : tls_server_credentials.h
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

#ifndef TLS_SERVER_CREDENTIALS_H
#define TLS_SERVER_CREDENTIALS_H

#include "main.h"

#include <stddef.h>
#include <stdint.h>

#define TLS_SERVER_CREDENTIALS_MAX_CERTIFICATE_SIZE 640U
#define TLS_SERVER_CREDENTIALS_MAX_KEY_SIZE          200U

typedef enum {
  TLS_SERVER_CREDENTIALS_STATUS_ACTIVATED = 0,
  TLS_SERVER_CREDENTIALS_STATUS_PENDING,
  TLS_SERVER_CREDENTIALS_STATUS_INVALID_DATA,
  TLS_SERVER_CREDENTIALS_STATUS_MISMATCH,
  TLS_SERVER_CREDENTIALS_STATUS_STORAGE_ERROR
} TlsServerCredentials_StatusTypeDef;

HAL_StatusTypeDef TlsServerCredentials_Init(void);

/**
  * @brief Stage a DER certificate and activate it if a matching key is
  *        already staged or currently active.
  * @param der (const uint8_t*) Non-null DER-encoded X.509 certificate.
  * @param length (size_t) Certificate length, up to
  *        TLS_SERVER_CREDENTIALS_MAX_CERTIFICATE_SIZE.
  * @retval (TlsServerCredentials_StatusTypeDef) ACTIVATED when the pair was
  *         verified and committed to Flash; PENDING while waiting for the
  *         private key; INVALID_DATA or MISMATCH on parse/pairing failure.
  */
TlsServerCredentials_StatusTypeDef TlsServerCredentials_StageCertificate(
  const uint8_t* der,
  size_t length
);

/**
  * @brief Stage a DER private key and activate it if a matching certificate
  *        is already staged or currently active.
  * @param der (const uint8_t*) Non-null DER-encoded private key.
  * @param length (size_t) Key length, up to TLS_SERVER_CREDENTIALS_MAX_KEY_SIZE.
  * @retval (TlsServerCredentials_StatusTypeDef) See TlsServerCredentials_StageCertificate.
  */
TlsServerCredentials_StatusTypeDef TlsServerCredentials_StagePrivateKey(
  const uint8_t* der,
  size_t length
);

/**
  * @brief Return the certificate to present for the next TLS connection.
  * @param data (const uint8_t**) Non-null; receives a pointer to either the
  *        flash-resident override or the compiled-in default.
  * @param length (size_t*) Non-null; receives the buffer length.
  * @note The caller must already hold TlsPlatform_Lock() for the duration of
  *       both this call and any subsequent use of the returned pointer, since
  *       an in-progress credential update can otherwise mutate the
  *       flash-resident copy this points at.
  */
void TlsServerCredentials_GetCertificate(const uint8_t** data, size_t* length);

/**
  * @brief Return the private key to present for the next TLS connection.
  * @note Same locking precondition as TlsServerCredentials_GetCertificate.
  */
void TlsServerCredentials_GetPrivateKey(const uint8_t** data, size_t* length);

#endif /* TLS_SERVER_CREDENTIALS_H */
