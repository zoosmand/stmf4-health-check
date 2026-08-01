/**
  ******************************************************************************
  * @file           : tls_transport.h
  * @brief          : Authenticated HTTPS transport interface.
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

#ifndef TLS_TRANSPORT_H
#define TLS_TRANSPORT_H

#include <stdint.h>

typedef enum {
  TLS_TRANSPORT_OK = 0,
  TLS_TRANSPORT_DNS_ERROR,
  TLS_TRANSPORT_CONNECT_ERROR,
  TLS_TRANSPORT_CONFIG_ERROR,
  TLS_TRANSPORT_CERTIFICATE_ERROR,
  TLS_TRANSPORT_HANDSHAKE_ERROR,
  TLS_TRANSPORT_IO_ERROR,
  TLS_TRANSPORT_PROTOCOL_ERROR
} TlsTransport_StatusTypeDef;

typedef struct {
  TlsTransport_StatusTypeDef status;
  int detail;
  uint16_t httpStatus;
  uint32_t elapsedMs;
  const char* tlsVersion;
  const char* cipherSuite;
} TlsTransport_ResultTypeDef;

/**
  * @brief Perform one authenticated TLS 1.3 HTTP HEAD request.
  * @param host (const char*) DNS hostname used for connection and validation.
  * @param port (uint16_t) TCP destination port.
  * @param resource (const char*) HTTP request path.
  * @param trustAnchorId (uint8_t) Factory or persistent CA anchor ID.
  * @param result (TlsTransport_ResultTypeDef*) Detailed bounded result.
  * @retval (TlsTransport_StatusTypeDef) Final transport stage.
  */
TlsTransport_StatusTypeDef TlsTransport_Head(
  const char* host,
  uint16_t port,
  const char* resource,
  uint8_t trustAnchorId,
  TlsTransport_ResultTypeDef* result
);

#endif /* TLS_TRANSPORT_H */
