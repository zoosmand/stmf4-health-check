/**
  ******************************************************************************
  * @file           : tls_transport.c
  * @brief          : Authenticated TLS 1.3 HTTP HEAD transport over lwIP.
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

#include "tls_transport.h"

#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "tls_platform.h"
#include "tls_trust_store.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define TLS_TRANSPORT_TIMEOUT_MS       10000
#define TLS_TRANSPORT_STATUS_LINE_SIZE 128U

typedef struct {
  int socketDescriptor;
  int lastError;
} TlsTransport_SocketContextTypeDef;

static uint8_t tlsTransport_IsTimeoutError(int socketError) {
  return ((socketError == EAGAIN)
      || (socketError == EWOULDBLOCK)
      || (socketError == ETIMEDOUT))
    ? 1U
    : 0U;
}

static int tlsTransport_Send(
  void* context,
  const unsigned char* data,
  size_t length
) {
  TlsTransport_SocketContextTypeDef* socketContext = context;
  int sent = lwip_send(
    socketContext->socketDescriptor,
    data,
    length,
    0
  );
  if (sent >= 0)
    return sent;

  socketContext->lastError = errno;
  return (tlsTransport_IsTimeoutError(socketContext->lastError) != 0U)
    ? MBEDTLS_ERR_SSL_TIMEOUT
    : MBEDTLS_ERR_NET_SEND_FAILED;
}

static int tlsTransport_Receive(
  void* context,
  unsigned char* data,
  size_t length
) {
  TlsTransport_SocketContextTypeDef* socketContext = context;
  int received = lwip_recv(
    socketContext->socketDescriptor,
    data,
    length,
    0
  );
  if (received > 0)
    return received;
  if (received == 0)
    return MBEDTLS_ERR_SSL_CONN_EOF;

  socketContext->lastError = errno;
  return (tlsTransport_IsTimeoutError(socketContext->lastError) != 0U)
    ? MBEDTLS_ERR_SSL_TIMEOUT
    : MBEDTLS_ERR_NET_RECV_FAILED;
}

static int tlsTransport_Connect(
  const char* host,
  uint16_t port,
  TlsTransport_StatusTypeDef* status,
  int* detail
) {
  char portText[6];
  (void)snprintf(portText, sizeof(portText), "%u", port);

  struct addrinfo hints = {
    .ai_family = AF_INET,
    .ai_socktype = SOCK_STREAM,
  };
  struct addrinfo* addresses = NULL;
  int resolverResult = lwip_getaddrinfo(
    host,
    portText,
    &hints,
    &addresses
  );
  if (resolverResult != 0) {
    *status = TLS_TRANSPORT_DNS_ERROR;
    *detail = resolverResult;
    return -1;
  }

  *status = TLS_TRANSPORT_CONNECT_ERROR;
  *detail = 0;
  int socketDescriptor = -1;
  for (struct addrinfo* address = addresses;
       address != NULL;
       address = address->ai_next) {
    socketDescriptor = lwip_socket(
      address->ai_family,
      address->ai_socktype,
      address->ai_protocol
    );
    if (socketDescriptor < 0) {
      *detail = -errno;
      continue;
    }

    struct timeval timeout = {
      .tv_sec = TLS_TRANSPORT_TIMEOUT_MS / 1000,
      .tv_usec = 0,
    };
    (void)lwip_setsockopt(
      socketDescriptor,
      SOL_SOCKET,
      SO_RCVTIMEO,
      &timeout,
      sizeof(timeout)
    );
    (void)lwip_setsockopt(
      socketDescriptor,
      SOL_SOCKET,
      SO_SNDTIMEO,
      &timeout,
      sizeof(timeout)
    );

    if (lwip_connect(
          socketDescriptor,
          address->ai_addr,
          address->ai_addrlen
        ) == 0) {
      break;
    }

    int connectError = errno;
    int socketError = 0;
    socklen_t socketErrorLength = sizeof(socketError);
    if ((lwip_getsockopt(
          socketDescriptor,
          SOL_SOCKET,
          SO_ERROR,
          &socketError,
          &socketErrorLength
        ) == 0)
        && (socketError != 0)) {
      *detail = -socketError;
    } else {
      *detail = -connectError;
    }
    lwip_close(socketDescriptor);
    socketDescriptor = -1;
  }

  lwip_freeaddrinfo(addresses);
  return socketDescriptor;
}

static int tlsTransport_WriteAll(
  mbedtls_ssl_context* ssl,
  const unsigned char* data,
  size_t length
) {
  size_t offset = 0U;
  while (offset < length) {
    int result = mbedtls_ssl_write(ssl, &data[offset], length - offset);
    if (result <= 0)
      return result;
    offset += (size_t)result;
  }
  return 0;
}

static int tlsTransport_ReadStatus(
  mbedtls_ssl_context* ssl,
  uint16_t* httpStatus
) {
  char line[TLS_TRANSPORT_STATUS_LINE_SIZE];
  size_t length = 0U;
  while (length < (sizeof(line) - 1U)) {
    int result = mbedtls_ssl_read(
      ssl,
      (unsigned char*)&line[length],
      1U
    );
    if (result <= 0)
      return result;
    ++length;
    if ((length >= 2U)
        && (line[length - 2U] == '\r')
        && (line[length - 1U] == '\n')) {
      break;
    }
  }
  line[length] = '\0';

  unsigned int status;
  if ((length < 12U)
      || (sscanf(line, "HTTP/1.%*u %3u", &status) != 1)
      || (status > 999U)) {
    return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
  }
  *httpStatus = (uint16_t)status;
  return 0;
}

TlsTransport_StatusTypeDef TlsTransport_Head(
  const char* host,
  uint16_t port,
  const char* resource,
  uint8_t trustAnchorId,
  TlsTransport_ResultTypeDef* result
) {
  if ((host == NULL) || (resource == NULL) || (result == NULL))
    return TLS_TRANSPORT_CONFIG_ERROR;

  memset(result, 0, sizeof(*result));
  result->status = TLS_TRANSPORT_CONFIG_ERROR;
  uint32_t started = HAL_GetTick();
  if (TlsPlatform_Lock() != HAL_OK)
    return result->status;

  int socketDescriptor = -1;
  TlsTransport_SocketContextTypeDef socketContext = {
    .socketDescriptor = -1,
    .lastError = 0,
  };

  mbedtls_ssl_context ssl;
  mbedtls_ssl_config config;
  mbedtls_x509_crt trustAnchor;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context random;
  mbedtls_ssl_init(&ssl);
  mbedtls_ssl_config_init(&config);
  mbedtls_x509_crt_init(&trustAnchor);
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&random);

  static const unsigned char personalization[] = "stm32-health-check";
  int detail = mbedtls_ctr_drbg_seed(
    &random,
    mbedtls_entropy_func,
    &entropy,
    personalization,
    sizeof(personalization) - 1U
  );
  if (detail != 0)
    goto cleanup;

  TlsTrustStore_StatusTypeDef trustStatus = TlsTrustStore_Parse(
    trustAnchorId, &trustAnchor
  );
  if (trustStatus != TLS_TRUST_STORE_STATUS_OK) {
    result->status = TLS_TRANSPORT_CERTIFICATE_ERROR;
    detail = MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
    goto cleanup;
  }

  detail = mbedtls_ssl_config_defaults(
    &config,
    MBEDTLS_SSL_IS_CLIENT,
    MBEDTLS_SSL_TRANSPORT_STREAM,
    MBEDTLS_SSL_PRESET_DEFAULT
  );
  if (detail != 0)
    goto cleanup;

  mbedtls_ssl_conf_min_tls_version(
    &config,
    MBEDTLS_SSL_VERSION_TLS1_3
  );
  mbedtls_ssl_conf_max_tls_version(
    &config,
    MBEDTLS_SSL_VERSION_TLS1_3
  );
  mbedtls_ssl_conf_authmode(&config, MBEDTLS_SSL_VERIFY_REQUIRED);
  mbedtls_ssl_conf_ca_chain(&config, &trustAnchor, NULL);
  mbedtls_ssl_conf_rng(&config, mbedtls_ctr_drbg_random, &random);

  detail = mbedtls_ssl_setup(&ssl, &config);
  if (detail != 0)
    goto cleanup;
  detail = mbedtls_ssl_set_hostname(&ssl, host);
  if (detail != 0)
    goto cleanup;

  socketDescriptor = tlsTransport_Connect(
    host,
    port,
    &result->status,
    &detail
  );
  if (socketDescriptor < 0) {
    goto cleanup;
  }
  socketContext.socketDescriptor = socketDescriptor;
  mbedtls_ssl_set_bio(
    &ssl,
    &socketContext,
    tlsTransport_Send,
    tlsTransport_Receive,
    NULL
  );

  detail = mbedtls_ssl_handshake(&ssl);
  if (detail != 0) {
    result->status = (mbedtls_ssl_get_verify_result(&ssl) != 0U)
      ? TLS_TRANSPORT_CERTIFICATE_ERROR
      : TLS_TRANSPORT_HANDSHAKE_ERROR;
    goto cleanup;
  }
  if (mbedtls_ssl_get_verify_result(&ssl) != 0U) {
    result->status = TLS_TRANSPORT_CERTIFICATE_ERROR;
    detail = MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
    goto cleanup;
  }

  result->tlsVersion = mbedtls_ssl_get_version(&ssl);
  result->cipherSuite = mbedtls_ssl_get_ciphersuite(&ssl);

  char request[256];
  int requestLength = snprintf(
    request,
    sizeof(request),
    "HEAD %s HTTP/1.1\r\n"
    "Host: %s\r\n"
    "Connection: close\r\n"
    "User-Agent: stm32-health-check/1\r\n\r\n",
    resource,
    host
  );
  if ((requestLength <= 0)
      || ((size_t)requestLength >= sizeof(request))) {
    detail = MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    goto cleanup;
  }

  detail = tlsTransport_WriteAll(
    &ssl,
    (const unsigned char*)request,
    (size_t)requestLength
  );
  if (detail != 0) {
    result->status = TLS_TRANSPORT_IO_ERROR;
    goto cleanup;
  }
  detail = tlsTransport_ReadStatus(&ssl, &result->httpStatus);
  if (detail != 0) {
    result->status = TLS_TRANSPORT_PROTOCOL_ERROR;
    goto cleanup;
  }

  result->status = TLS_TRANSPORT_OK;
  detail = 0;

cleanup:
  if ((detail != 0) && (socketContext.lastError != 0))
    detail = -socketContext.lastError;
  result->detail = detail;
  result->elapsedMs = HAL_GetTick() - started;
  if (socketDescriptor >= 0) {
    (void)mbedtls_ssl_close_notify(&ssl);
    lwip_close(socketDescriptor);
  }
  mbedtls_ssl_free(&ssl);
  mbedtls_ssl_config_free(&config);
  mbedtls_x509_crt_free(&trustAnchor);
  mbedtls_ctr_drbg_free(&random);
  mbedtls_entropy_free(&entropy);
  TlsPlatform_Unlock();
  return result->status;
}
