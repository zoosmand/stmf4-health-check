/**
  ******************************************************************************
  * @file           : api_service.c
  * @brief          : Bounded HTTPS JSON management API.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 31.07.2026
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

#include "api_service.h"

#include "FreeRTOS.h"
#include "auth_service.h"
#include "ds18b20.h"
#include "health_check_config.h"
#include "health_check_log.h"
#include "lwip.h"
#include "lwip/sockets.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/pk.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "rtc.h"
#include "task.h"
#include "temperature_service.h"
#include "tls_platform.h"
#include "tls_server_credentials.h"
#include "tls_transport.h"
#include "tls_trust_store.h"
#include "w25q64.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define API_SERVICE_PORT              443U
#define API_SERVICE_TASK_STACK_DEPTH  3072U
#define API_SERVICE_REQUEST_SIZE      2560U
#define API_SERVICE_RESPONSE_SIZE     2048U
#define API_SERVICE_BODY_SIZE         TLS_TRUST_STORE_MAX_DER_SIZE
#define API_SERVICE_TIMEOUT_MS        10000U

typedef struct {
  int descriptor;
} ApiService_SocketTypeDef;

typedef struct {
  char method[8];
  char path[80];
  char authorization[AUTH_SERVICE_TOKEN_TEXT_SIZE];
  char* body;
  size_t bodyLength;
} ApiService_RequestTypeDef;

static StaticTask_t apiTaskControlBlock;
static StackType_t apiTaskStack[API_SERVICE_TASK_STACK_DEPTH];

static int apiService_Send(
  void* context,
  const unsigned char* data,
  size_t length
) {
  ApiService_SocketTypeDef* socket = context;
  int result = lwip_send(socket->descriptor, data, length, 0);
  if (result >= 0)
    return result;
  return ((errno == EAGAIN) || (errno == EWOULDBLOCK))
    ? MBEDTLS_ERR_SSL_TIMEOUT
    : MBEDTLS_ERR_NET_SEND_FAILED;
}

static int apiService_Receive(
  void* context,
  unsigned char* data,
  size_t length
) {
  ApiService_SocketTypeDef* socket = context;
  int result = lwip_recv(socket->descriptor, data, length, 0);
  if (result > 0)
    return result;
  if (result == 0)
    return MBEDTLS_ERR_SSL_CONN_EOF;
  return ((errno == EAGAIN) || (errno == EWOULDBLOCK))
    ? MBEDTLS_ERR_SSL_TIMEOUT
    : MBEDTLS_ERR_NET_RECV_FAILED;
}

static int apiService_WriteAll(
  mbedtls_ssl_context* ssl,
  const char* data,
  size_t length
) {
  size_t offset = 0U;
  while (offset < length) {
    int result = mbedtls_ssl_write(
      ssl, (const uint8_t*)&data[offset], length - offset
    );
    if (result <= 0)
      return result;
    offset += (size_t)result;
  }
  return 0;
}

static char* apiService_FindHeaderEnd(char* request) {
  return strstr(request, "\r\n\r\n");
}

static int apiService_ReadRequest(
  mbedtls_ssl_context* ssl,
  char* buffer,
  size_t capacity,
  ApiService_RequestTypeDef* request
) {
  size_t used = 0U;
  char* headerEnd = NULL;
  size_t expected = 0U;
  while (used < (capacity - 1U)) {
    int result = mbedtls_ssl_read(
      ssl, (uint8_t*)&buffer[used], capacity - used - 1U
    );
    if (result <= 0)
      return result;
    used += (size_t)result;
    buffer[used] = '\0';
    if (headerEnd == NULL) {
      headerEnd = apiService_FindHeaderEnd(buffer);
      if (headerEnd != NULL) {
        char saved = *headerEnd;
        *headerEnd = '\0';
        char* lengthHeader = strstr(buffer, "\r\nContent-Length:");
        if (lengthHeader == NULL)
          lengthHeader = strstr(buffer, "\r\ncontent-length:");
        if (lengthHeader != NULL) {
          expected = strtoul(lengthHeader + 17U, NULL, 10);
          if (expected > API_SERVICE_BODY_SIZE) {
            *headerEnd = saved;
            return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
          }
        }
        *headerEnd = saved;
      }
    }
    if ((headerEnd != NULL)
        && (used >= ((size_t)(headerEnd + 4U - buffer) + expected)))
      break;
  }
  if (headerEnd == NULL)
    return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
  char saved = *headerEnd;
  *headerEnd = '\0';
  if (sscanf(buffer, "%7s %79s", request->method, request->path) != 2)
    return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
  request->authorization[0] = '\0';
  char* bearer = strstr(buffer, "\r\nAuthorization: Bearer ");
  if (bearer == NULL)
    bearer = strstr(buffer, "\r\nauthorization: Bearer ");
  if (bearer != NULL) {
    bearer += 24U;
    char* end = strstr(bearer, "\r\n");
    size_t length = (end != NULL) ? (size_t)(end - bearer) : 0U;
    if (length >= sizeof(request->authorization))
      return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    memcpy(request->authorization, bearer, length);
    request->authorization[length] = '\0';
  }
  *headerEnd = saved;
  request->body = headerEnd + 4U;
  request->bodyLength = expected;
  request->body[expected] = '\0';
  return 0;
}

static uint8_t apiService_JsonString(
  const char* json,
  const char* key,
  char* output,
  size_t capacity
) {
  char pattern[40];
  if (snprintf(pattern, sizeof(pattern), "\"%s\"", key) <= 0)
    return 0U;
  const char* cursor = strstr(json, pattern);
  if (cursor == NULL)
    return 0U;
  cursor += strlen(pattern);
  while ((*cursor == ' ') || (*cursor == '\t'))
    ++cursor;
  if (*cursor++ != ':')
    return 0U;
  while ((*cursor == ' ') || (*cursor == '\t'))
    ++cursor;
  if (*cursor++ != '"')
    return 0U;
  size_t length = 0U;
  while ((*cursor != '\0') && (*cursor != '"')) {
    if ((*cursor == '\\') || ((uint8_t)*cursor < 0x20U)
        || (length >= (capacity - 1U)))
      return 0U;
    output[length++] = *cursor++;
  }
  if (*cursor != '"')
    return 0U;
  output[length] = '\0';
  return 1U;
}

static uint8_t apiService_JsonBoolean(
  const char* json,
  const char* key,
  uint8_t* value
) {
  char pattern[40];
  (void)snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char* cursor = strstr(json, pattern);
  if (cursor == NULL)
    return 0U;
  cursor = strchr(cursor + strlen(pattern), ':');
  if (cursor == NULL)
    return 0U;
  do {
    ++cursor;
  } while ((*cursor == ' ') || (*cursor == '\t'));
  if (strncmp(cursor, "true", 4U) == 0)
    *value = 1U;
  else if (strncmp(cursor, "false", 5U) == 0)
    *value = 0U;
  else
    return 0U;
  return 1U;
}

static uint8_t apiService_JsonNumber(
  const char* json,
  const char* key,
  uint32_t* value
) {
  char pattern[40];
  (void)snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char* cursor = strstr(json, pattern);
  if (cursor == NULL)
    return 0U;
  cursor = strchr(cursor + strlen(pattern), ':');
  if (cursor == NULL)
    return 0U;
  do {
    ++cursor;
  } while ((*cursor == ' ') || (*cursor == '\t'));
  char* end = NULL;
  unsigned long parsed = strtoul(cursor, &end, 10);
  if (end == cursor)
    return 0U;
  *value = (uint32_t)parsed;
  return 1U;
}

static uint8_t apiService_AppendJsonString(
  char* output,
  size_t capacity,
  size_t* used,
  const char* value
) {
  if ((output == NULL) || (used == NULL) || (value == NULL)
      || (*used >= capacity)) {
    return 0U;
  }
  while (*value != '\0') {
    uint8_t character = (uint8_t)*value++;
    if (character < 0x20U)
      return 0U;
    if ((character == '"') || (character == '\\')) {
      if ((capacity - *used) <= 2U)
        return 0U;
      output[(*used)++] = '\\';
    } else if ((capacity - *used) <= 1U) {
      return 0U;
    }
    output[(*used)++] = (char)character;
  }
  output[*used] = '\0';
  return 1U;
}

static void apiService_HexEncode(
  const uint8_t* data,
  size_t length,
  char* output
) {
  static const char digits[] = "0123456789abcdef";
  for (size_t index = 0U; index < length; ++index) {
    output[index * 2U] = digits[data[index] >> 4U];
    output[index * 2U + 1U] = digits[data[index] & 0x0FU];
  }
  output[length * 2U] = '\0';
}

static const char* apiService_TransportStatusText(
  TlsTransport_StatusTypeDef status
) {
  switch (status) {
    case TLS_TRANSPORT_OK: return "ok";
    case TLS_TRANSPORT_DNS_ERROR: return "dns_error";
    case TLS_TRANSPORT_CONNECT_ERROR: return "connect_error";
    case TLS_TRANSPORT_CONFIG_ERROR: return "config_error";
    case TLS_TRANSPORT_CERTIFICATE_ERROR: return "certificate_error";
    case TLS_TRANSPORT_HANDSHAKE_ERROR: return "handshake_error";
    case TLS_TRANSPORT_IO_ERROR: return "io_error";
    case TLS_TRANSPORT_PROTOCOL_ERROR: return "protocol_error";
    default: return "unknown";
  }
}

static const char* apiService_Ds18b20StatusText(DS18B20_StatusTypeDef status) {
  switch (status) {
    case DS18B20_STATUS_OK: return "ok";
    case DS18B20_STATUS_BUS: return "bus_error";
    case DS18B20_STATUS_TIMEOUT: return "timeout";
    case DS18B20_STATUS_CRC: return "crc_error";
    default: return "unknown";
  }
}

static int apiService_RespondEx(
  mbedtls_ssl_context* ssl,
  int status,
  const char* reason,
  const char* json,
  uint8_t headOnly
) {
  char response[API_SERVICE_RESPONSE_SIZE];
  int length = snprintf(
    response,
    sizeof(response),
    "HTTP/1.1 %d %s\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: %u\r\n"
    "Connection: close\r\n"
    "Cache-Control: no-store\r\n\r\n%s",
    status,
    reason,
    (unsigned int)strlen(json),
    headOnly != 0U ? "" : json
  );
  if ((length <= 0) || ((size_t)length >= sizeof(response)))
    return MBEDTLS_ERR_SSL_BUFFER_TOO_SMALL;
  return apiService_WriteAll(ssl, response, (size_t)length);
}

static int apiService_Respond(
  mbedtls_ssl_context* ssl,
  int status,
  const char* reason,
  const char* json
) {
  return apiService_RespondEx(ssl, status, reason, json, 0U);
}

static int apiService_Error(
  mbedtls_ssl_context* ssl,
  int status,
  const char* reason,
  const char* code
) {
  char json[96];
  (void)snprintf(json, sizeof(json), "{\"error\":\"%s\"}", code);
  return apiService_Respond(ssl, status, reason, json);
}

static int apiService_TlsCredentialsRespond(
  mbedtls_ssl_context* ssl,
  TlsServerCredentials_StatusTypeDef status,
  const char* awaiting,
  const char* invalidError
) {
  switch (status) {
    case TLS_SERVER_CREDENTIALS_STATUS_ACTIVATED:
      return apiService_Respond(ssl, 200, "OK", "{\"status\":\"activated\"}");
    case TLS_SERVER_CREDENTIALS_STATUS_PENDING: {
      char json[64];
      (void)snprintf(
        json,
        sizeof(json),
        "{\"status\":\"pending\",\"awaiting\":\"%s\"}",
        awaiting
      );
      return apiService_Respond(ssl, 202, "Accepted", json);
    }
    case TLS_SERVER_CREDENTIALS_STATUS_MISMATCH:
      return apiService_Error(ssl, 409, "Conflict", "key_mismatch");
    case TLS_SERVER_CREDENTIALS_STATUS_INVALID_DATA:
      return apiService_Error(ssl, 400, "Bad Request", invalidError);
    default:
      return apiService_Error(ssl, 500, "Internal Server Error", "storage_error");
  }
}

static int apiService_TrustStoreError(
  mbedtls_ssl_context* ssl,
  TlsTrustStore_StatusTypeDef status
) {
  switch (status) {
    case TLS_TRUST_STORE_STATUS_INVALID_ARGUMENT:
      return apiService_Error(
        ssl, 400, "Bad Request", "invalid_certificate_body"
      );
    case TLS_TRUST_STORE_STATUS_INVALID_CERTIFICATE:
      return apiService_Error(
        ssl, 400, "Bad Request", "invalid_certificate"
      );
    case TLS_TRUST_STORE_STATUS_NO_MEMORY:
      return apiService_Error(
        ssl, 503, "Service Unavailable", "tls_memory_exhausted"
      );
    case TLS_TRUST_STORE_STATUS_NOT_CA:
      return apiService_Error(ssl, 400, "Bad Request", "certificate_not_ca");
    case TLS_TRUST_STORE_STATUS_NOT_FOUND:
      return apiService_Error(ssl, 404, "Not Found", "trust_anchor_not_found");
    case TLS_TRUST_STORE_STATUS_FULL:
      return apiService_Error(
        ssl, 409, "Conflict", "trust_anchor_limit_reached"
      );
    case TLS_TRUST_STORE_STATUS_FACTORY_PROTECTED:
      return apiService_Error(
        ssl, 403, "Forbidden", "factory_anchor_protected"
      );
    default:
      return apiService_Error(
        ssl, 500, "Internal Server Error", "storage_error"
      );
  }
}

static uint8_t apiService_Authorize(
  const ApiService_RequestTypeDef* request,
  AuthService_PrincipalTypeDef* principal
) {
  return (AuthService_Authorize(
    request->authorization, principal
  ) == AUTH_SERVICE_STATUS_OK) ? 1U : 0U;
}

static int apiService_Tokens(
  mbedtls_ssl_context* ssl,
  const AuthService_TokenPairTypeDef* tokens
) {
  char json[384];
  (void)snprintf(
    json,
    sizeof(json),
    "{\"token_type\":\"Bearer\",\"access_token\":\"%s\","
    "\"expires_in\":%lu,\"refresh_token\":\"%s\","
    "\"refresh_expires_in\":%lu}",
    tokens->accessToken,
    (unsigned long)tokens->accessExpiresIn,
    tokens->refreshToken,
    (unsigned long)tokens->refreshExpiresIn
  );
  return apiService_Respond(ssl, 200, "OK", json);
}

static int apiService_Dispatch(
  mbedtls_ssl_context* ssl,
  const ApiService_RequestTypeDef* request
) {
  if (((strcmp(request->method, "GET") == 0)
        || (strcmp(request->method, "HEAD") == 0))
      && (strcmp(request->path, "/health") == 0)) {
    uint8_t headOnly = (strcmp(request->method, "HEAD") == 0) ? 1U : 0U;
    uint8_t networkHealthy = Lwip_IsReady();
    uint8_t rtcHealthy = Rtc_IsSynchronized();
    uint8_t flashHealthy = W25Q64_IsAvailable();
    uint8_t temperatureHealthy = 0U;
    DS18B20_MeasurementTypeDef measurements[ONEWIRE_MAX_DEVICES];
    size_t measurementCount = TemperatureService_GetMeasurements(
      measurements, ONEWIRE_MAX_DEVICES
    );
    if (measurementCount != 0U) {
      temperatureHealthy = 1U;
      for (size_t index = 0U; index < measurementCount; ++index) {
        if (measurements[index].status != DS18B20_STATUS_OK) {
          temperatureHealthy = 0U;
          break;
        }
      }
    }

    uint8_t healthy = (networkHealthy != 0U)
      && (rtcHealthy != 0U)
      && (flashHealthy != 0U)
      && (temperatureHealthy != 0U);
    char json[192];
    (void)snprintf(
      json,
      sizeof(json),
      "{\"status\":\"%s\",\"systems\":{\"api\":true,"
      "\"network\":%s,\"rtc\":%s,\"flash\":%s,\"temperature\":%s}}",
      healthy != 0U ? "ok" : "failed",
      networkHealthy != 0U ? "true" : "false",
      rtcHealthy != 0U ? "true" : "false",
      flashHealthy != 0U ? "true" : "false",
      temperatureHealthy != 0U ? "true" : "false"
    );
    return apiService_RespondEx(
      ssl,
      healthy != 0U ? 200 : 503,
      healthy != 0U ? "OK" : "Service Unavailable",
      json,
      headOnly
    );
  }

  if ((strcmp(request->method, "POST") == 0)
      && (strcmp(request->path, "/api/v1/auth/token") == 0)) {
    char username[USER_STORE_USERNAME_SIZE];
    char password[129];
    if ((apiService_JsonString(
          request->body, "username", username, sizeof(username)
        ) == 0U)
        || (apiService_JsonString(
          request->body, "password", password, sizeof(password)
        ) == 0U)) {
      return apiService_Error(
        ssl, 400, "Bad Request", "invalid_request"
      );
    }
    AuthService_TokenPairTypeDef tokens;
    AuthService_StatusTypeDef status = AuthService_Login(
      username, (const uint8_t*)password, strlen(password), &tokens
    );
    mbedtls_platform_zeroize(password, sizeof(password));
    if (status != AUTH_SERVICE_STATUS_OK)
      return apiService_Error(
        ssl, 401, "Unauthorized", "invalid_credentials"
      );
    int result = apiService_Tokens(ssl, &tokens);
    mbedtls_platform_zeroize(&tokens, sizeof(tokens));
    return result;
  }

  if ((strcmp(request->method, "POST") == 0)
      && (strcmp(request->path, "/api/v1/auth/refresh") == 0)) {
    char refresh[AUTH_SERVICE_TOKEN_TEXT_SIZE];
    if (apiService_JsonString(
          request->body, "refresh_token", refresh, sizeof(refresh)
        ) == 0U) {
      return apiService_Error(
        ssl, 400, "Bad Request", "invalid_request"
      );
    }
    AuthService_TokenPairTypeDef tokens;
    AuthService_StatusTypeDef status = AuthService_Refresh(
      refresh, &tokens
    );
    mbedtls_platform_zeroize(refresh, sizeof(refresh));
    if (status != AUTH_SERVICE_STATUS_OK)
      return apiService_Error(
        ssl, 401, "Unauthorized", "invalid_refresh_token"
      );
    int result = apiService_Tokens(ssl, &tokens);
    mbedtls_platform_zeroize(&tokens, sizeof(tokens));
    return result;
  }

  AuthService_PrincipalTypeDef principal;
  if (apiService_Authorize(request, &principal) == 0U)
    return apiService_Error(
      ssl, 401, "Unauthorized", "invalid_access_token"
    );

  if ((strcmp(request->method, "POST") == 0)
      && (strcmp(request->path, "/api/v1/auth/revoke") == 0)) {
    if (AuthService_Revoke(request->authorization)
        != AUTH_SERVICE_STATUS_OK) {
      return apiService_Error(
        ssl, 401, "Unauthorized", "invalid_access_token"
      );
    }
    return apiService_Respond(ssl, 200, "OK", "{\"revoked\":true}");
  }

  if ((strcmp(request->method, "GET") == 0)
      && (strcmp(request->path, "/api/v1/users") == 0)) {
    if (principal.role != USER_ROLE_ADMINISTRATOR)
      return apiService_Error(ssl, 403, "Forbidden", "forbidden");
    UserStore_RecordTypeDef users[USER_STORE_MAX_USERS];
    size_t count = AuthService_ListUsers(
      &principal, users, USER_STORE_MAX_USERS
    );
    char json[API_SERVICE_RESPONSE_SIZE - 192U];
    size_t used = (size_t)snprintf(
      json,
      sizeof(json),
      "{\"users\":[{\"username\":\"master\","
      "\"role\":\"administrator\",\"enabled\":true}"
    );
    for (size_t index = 0U; index < count; ++index) {
      int written = snprintf(
        &json[used],
        sizeof(json) - used,
        "%s{\"username\":\"%s\",\"role\":\"%s\",\"enabled\":%s}",
        ",",
        users[index].username,
        users[index].role == USER_ROLE_ADMINISTRATOR ? "administrator" : "user",
        users[index].enabled != 0U ? "true" : "false"
      );
      if ((written <= 0) || ((size_t)written >= (sizeof(json) - used)))
        return apiService_Error(
          ssl, 500, "Internal Server Error", "response_too_large"
        );
      used += (size_t)written;
    }
    (void)snprintf(&json[used], sizeof(json) - used, "]}");
    return apiService_Respond(ssl, 200, "OK", json);
  }

  uint8_t creating = ((strcmp(request->method, "POST") == 0)
      && (strcmp(request->path, "/api/v1/users") == 0));
  const char* prefix = "/api/v1/users/";
  uint8_t updating = ((strcmp(request->method, "PUT") == 0)
      && (strncmp(request->path, prefix, strlen(prefix)) == 0));
  if ((creating != 0U) || (updating != 0U)) {
    if (principal.role != USER_ROLE_ADMINISTRATOR)
      return apiService_Error(ssl, 403, "Forbidden", "forbidden");
    char username[USER_STORE_USERNAME_SIZE];
    char password[129];
    char roleText[20] = "user";
    uint8_t enabled = 1U;
    if (creating != 0U) {
      if (apiService_JsonString(
            request->body, "username", username, sizeof(username)
          ) == 0U) {
        return apiService_Error(
          ssl, 400, "Bad Request", "invalid_request"
        );
      }
    } else {
      (void)strncpy(
        username, request->path + strlen(prefix), sizeof(username) - 1U
      );
      username[sizeof(username) - 1U] = '\0';
      UserStore_RecordTypeDef existing;
      if (UserStore_Find(username, &existing, NULL) != HAL_OK)
        return apiService_Error(
          ssl, 404, "Not Found", "user_not_found"
        );
      enabled = existing.enabled;
      (void)strncpy(
        roleText,
        existing.role == USER_ROLE_ADMINISTRATOR
          ? "administrator"
          : "user",
        sizeof(roleText) - 1U
      );
      roleText[sizeof(roleText) - 1U] = '\0';
    }
    if (apiService_JsonString(
          request->body, "password", password, sizeof(password)
        ) == 0U) {
      return apiService_Error(
        ssl, 400, "Bad Request", "password_required"
      );
    }
    (void)apiService_JsonString(
      request->body, "role", roleText, sizeof(roleText)
    );
    (void)apiService_JsonBoolean(request->body, "enabled", &enabled);
    UserStore_RoleTypeDef role =
      (strcmp(roleText, "administrator") == 0)
        ? USER_ROLE_ADMINISTRATOR
        : USER_ROLE_USER;
    AuthService_StatusTypeDef status = AuthService_PutUser(
      &principal,
      username,
      (const uint8_t*)password,
      strlen(password),
      role,
      enabled,
      updating
    );
    mbedtls_platform_zeroize(password, sizeof(password));
    if (status == AUTH_SERVICE_STATUS_NOT_FOUND)
      return apiService_Error(ssl, 404, "Not Found", "user_not_found");
    if (status != AUTH_SERVICE_STATUS_OK)
      return apiService_Error(
        ssl, 400, "Bad Request", "user_not_saved"
      );
    char json[96];
    (void)snprintf(json, sizeof(json), "{\"username\":\"%s\"}", username);
    return apiService_Respond(
      ssl, creating != 0U ? 201 : 200,
      creating != 0U ? "Created" : "OK",
      json
    );
  }

  if ((strcmp(request->method, "DELETE") == 0)
      && (strncmp(request->path, prefix, strlen(prefix)) == 0)) {
    if (principal.role != USER_ROLE_ADMINISTRATOR)
      return apiService_Error(ssl, 403, "Forbidden", "forbidden");
    char username[USER_STORE_USERNAME_SIZE];
    (void)strncpy(
      username, request->path + strlen(prefix), sizeof(username) - 1U
    );
    username[sizeof(username) - 1U] = '\0';
    AuthService_StatusTypeDef status = AuthService_DeleteUser(
      &principal, username
    );
    if (status == AUTH_SERVICE_STATUS_NOT_FOUND)
      return apiService_Error(ssl, 404, "Not Found", "user_not_found");
    if (status == AUTH_SERVICE_STATUS_FORBIDDEN)
      return apiService_Error(ssl, 403, "Forbidden", "forbidden");
    if (status != AUTH_SERVICE_STATUS_OK)
      return apiService_Error(
        ssl, 500, "Internal Server Error", "storage_error"
      );
    char json[64];
    (void)snprintf(
      json, sizeof(json), "{\"username\":\"%s\",\"deleted\":true}", username
    );
    return apiService_Respond(ssl, 200, "OK", json);
  }

  if ((strcmp(request->method, "PUT") == 0)
      && (strcmp(request->path, "/api/v1/tls/certificate") == 0)) {
    if (principal.role != USER_ROLE_ADMINISTRATOR)
      return apiService_Error(ssl, 403, "Forbidden", "forbidden");
    if (request->bodyLength == 0U)
      return apiService_Error(
        ssl, 400, "Bad Request", "invalid_certificate_body"
      );
    TlsServerCredentials_StatusTypeDef status =
      TlsServerCredentials_StageCertificate(
        (const uint8_t*)request->body, request->bodyLength
      );
    return apiService_TlsCredentialsRespond(
      ssl, status, "private_key", "invalid_certificate"
    );
  }

  if ((strcmp(request->method, "PUT") == 0)
      && (strcmp(request->path, "/api/v1/tls/private-key") == 0)) {
    if (principal.role != USER_ROLE_ADMINISTRATOR)
      return apiService_Error(ssl, 403, "Forbidden", "forbidden");
    if (request->bodyLength == 0U)
      return apiService_Error(
        ssl, 400, "Bad Request", "invalid_private_key_body"
      );
    TlsServerCredentials_StatusTypeDef status =
      TlsServerCredentials_StagePrivateKey(
        (const uint8_t*)request->body, request->bodyLength
      );
    return apiService_TlsCredentialsRespond(
      ssl, status, "certificate", "invalid_private_key"
    );
  }

  if ((strcmp(request->method, "GET") == 0)
      && (strcmp(request->path, "/api/v1/trust-anchors") == 0)) {
    if (principal.role != USER_ROLE_ADMINISTRATOR)
      return apiService_Error(ssl, 403, "Forbidden", "forbidden");
    TlsTrustStore_InfoTypeDef anchors[TLS_TRUST_STORE_MAX_ANCHORS];
    size_t count = TlsTrustStore_List(
      anchors, TLS_TRUST_STORE_MAX_ANCHORS
    );
    char json[API_SERVICE_RESPONSE_SIZE - 192U];
    size_t used = (size_t)snprintf(
      json, sizeof(json), "{\"trust_anchors\":["
    );
    for (size_t index = 0U; index < count; ++index) {
      int written = snprintf(
        &json[used],
        sizeof(json) - used,
        "%s{\"id\":%u,\"factory\":%s,\"der_length\":%u,"
        "\"subject\":\"",
        index == 0U ? "" : ",",
        (unsigned int)anchors[index].id,
        anchors[index].factory != 0U ? "true" : "false",
        (unsigned int)anchors[index].derLength
      );
      if ((written <= 0) || ((size_t)written >= (sizeof(json) - used)))
        return apiService_Error(
          ssl, 500, "Internal Server Error", "response_too_large"
        );
      used += (size_t)written;
      if (apiService_AppendJsonString(
            json, sizeof(json), &used, anchors[index].subject
          ) == 0U) {
        return apiService_Error(
          ssl, 500, "Internal Server Error", "response_too_large"
        );
      }
      written = snprintf(&json[used], sizeof(json) - used, "\"}");
      if ((written <= 0) || ((size_t)written >= (sizeof(json) - used)))
        return apiService_Error(
          ssl, 500, "Internal Server Error", "response_too_large"
        );
      used += (size_t)written;
    }
    (void)snprintf(&json[used], sizeof(json) - used, "]}");
    return apiService_Respond(ssl, 200, "OK", json);
  }

  if ((strcmp(request->method, "POST") == 0)
      && (strcmp(request->path, "/api/v1/trust-anchors") == 0)) {
    if (principal.role != USER_ROLE_ADMINISTRATOR)
      return apiService_Error(ssl, 403, "Forbidden", "forbidden");
    uint8_t assignedId = 0U;
    TlsTrustStore_StatusTypeDef status = TlsTrustStore_Add(
      (const uint8_t*)request->body, request->bodyLength, &assignedId
    );
    if (status != TLS_TRUST_STORE_STATUS_OK)
      return apiService_TrustStoreError(ssl, status);
    char json[48];
    (void)snprintf(
      json, sizeof(json), "{\"id\":%u}", (unsigned int)assignedId
    );
    return apiService_Respond(ssl, 201, "Created", json);
  }

  if ((strcmp(request->method, "DELETE") == 0)
      && (strcmp(request->path, "/api/v1/trust-anchors") == 0)) {
    if (principal.role != USER_ROLE_ADMINISTRATOR)
      return apiService_Error(ssl, 403, "Forbidden", "forbidden");
    if (HealthCheckConfig_ResetTrustAnchors()
        != HEALTH_CHECK_CONFIG_STATUS_OK) {
      return apiService_Error(
        ssl, 500, "Internal Server Error", "storage_error"
      );
    }
    TlsTrustStore_StatusTypeDef status = TlsTrustStore_Reset();
    if (status != TLS_TRUST_STORE_STATUS_OK)
      return apiService_TrustStoreError(ssl, status);
    return apiService_Respond(
      ssl, 200, "OK", "{\"factory_restored\":true}"
    );
  }

  const char* trustAnchorPrefix = "/api/v1/trust-anchors/";
  uint8_t replacingTrustAnchor = ((strcmp(request->method, "PUT") == 0)
      && (strncmp(
        request->path, trustAnchorPrefix, strlen(trustAnchorPrefix)
      ) == 0));
  uint8_t deletingTrustAnchor = ((strcmp(request->method, "DELETE") == 0)
      && (strncmp(
        request->path, trustAnchorPrefix, strlen(trustAnchorPrefix)
      ) == 0));
  if ((replacingTrustAnchor != 0U) || (deletingTrustAnchor != 0U)) {
    if (principal.role != USER_ROLE_ADMINISTRATOR)
      return apiService_Error(ssl, 403, "Forbidden", "forbidden");
    const char* idText = request->path + strlen(trustAnchorPrefix);
    char* idEnd;
    long idValue = strtol(idText, &idEnd, 10);
    if ((idEnd == idText) || (*idEnd != '\0') || (idValue < 0)
        || (idValue > TLS_TRUST_STORE_MAX_PERSISTED)) {
      return apiService_Error(
        ssl, 404, "Not Found", "trust_anchor_not_found"
      );
    }
    uint8_t id = (uint8_t)idValue;
    if (deletingTrustAnchor != 0U) {
      if (id == TLS_TRUST_STORE_FACTORY_ID)
        return apiService_TrustStoreError(
          ssl, TLS_TRUST_STORE_STATUS_FACTORY_PROTECTED
        );
      if (HealthCheckConfig_IsTrustAnchorInUse(id) != 0U)
        return apiService_Error(
          ssl, 409, "Conflict", "trust_anchor_in_use"
        );
      TlsTrustStore_StatusTypeDef status = TlsTrustStore_Delete(id);
      if (status != TLS_TRUST_STORE_STATUS_OK)
        return apiService_TrustStoreError(ssl, status);
      char json[48];
      (void)snprintf(
        json, sizeof(json), "{\"id\":%u,\"deleted\":true}",
        (unsigned int)id
      );
      return apiService_Respond(ssl, 200, "OK", json);
    }
    TlsTrustStore_StatusTypeDef status = TlsTrustStore_Replace(
      id, (const uint8_t*)request->body, request->bodyLength
    );
    if (status != TLS_TRUST_STORE_STATUS_OK)
      return apiService_TrustStoreError(ssl, status);
    char json[48];
    (void)snprintf(
      json, sizeof(json), "{\"id\":%u,\"replaced\":true}",
      (unsigned int)id
    );
    return apiService_Respond(ssl, 200, "OK", json);
  }

  if ((strcmp(request->method, "GET") == 0)
      && (strcmp(request->path, "/api/v1/health-check/config") == 0)) {
    if (principal.role != USER_ROLE_ADMINISTRATOR)
      return apiService_Error(ssl, 403, "Forbidden", "forbidden");
    HealthCheckConfig_ResourceTypeDef resources[
      HEALTH_CHECK_CONFIG_MAX_RESOURCES
    ];
    HealthCheckConfig_GetResources(resources);
    char json[API_SERVICE_RESPONSE_SIZE - 192U];
    size_t used = (size_t)snprintf(
      json,
      sizeof(json),
      "{\"period_seconds\":%lu,\"resources\":[",
      (unsigned long)HealthCheckConfig_GetPeriodSeconds()
    );
    uint8_t first = 1U;
    for (uint8_t index = 0U; index < HEALTH_CHECK_CONFIG_MAX_RESOURCES; ++index) {
      if (resources[index].occupied == 0U)
        continue;
      int written = snprintf(
        &json[used],
        sizeof(json) - used,
        "%s{\"index\":%u,\"host\":\"%s\",\"port\":%u,\"path\":\"%s\","
        "\"enabled\":%s,\"trust_anchor_id\":%u}",
        first != 0U ? "" : ",",
        (unsigned int)index,
        resources[index].host,
        (unsigned int)resources[index].port,
        resources[index].path,
        resources[index].enabled != 0U ? "true" : "false",
        (unsigned int)resources[index].trustAnchorId
      );
      if ((written <= 0) || ((size_t)written >= (sizeof(json) - used)))
        return apiService_Error(
          ssl, 500, "Internal Server Error", "response_too_large"
        );
      used += (size_t)written;
      first = 0U;
    }
    (void)snprintf(&json[used], sizeof(json) - used, "]}");
    return apiService_Respond(ssl, 200, "OK", json);
  }

  if ((strcmp(request->method, "PUT") == 0)
      && (strcmp(request->path, "/api/v1/health-check/config") == 0)) {
    if (principal.role != USER_ROLE_ADMINISTRATOR)
      return apiService_Error(ssl, 403, "Forbidden", "forbidden");
    uint32_t periodValue;
    if (apiService_JsonNumber(
          request->body, "period_seconds", &periodValue
        ) == 0U) {
      return apiService_Error(ssl, 400, "Bad Request", "invalid_request");
    }
    HealthCheckConfig_StatusTypeDef status =
      HealthCheckConfig_SetPeriodSeconds(periodValue);
    if (status == HEALTH_CHECK_CONFIG_STATUS_INVALID_ARGUMENT)
      return apiService_Error(ssl, 400, "Bad Request", "invalid_period");
    if (status != HEALTH_CHECK_CONFIG_STATUS_OK)
      return apiService_Error(
        ssl, 500, "Internal Server Error", "storage_error"
      );
    char json[48];
    (void)snprintf(
      json, sizeof(json), "{\"period_seconds\":%lu}",
      (unsigned long)periodValue
    );
    return apiService_Respond(ssl, 200, "OK", json);
  }

  if ((strcmp(request->method, "POST") == 0)
      && (strcmp(request->path, "/api/v1/health-check/resources") == 0)) {
    if (principal.role != USER_ROLE_ADMINISTRATOR)
      return apiService_Error(ssl, 403, "Forbidden", "forbidden");
    char host[HEALTH_CHECK_CONFIG_HOST_SIZE];
    char path[HEALTH_CHECK_CONFIG_PATH_SIZE];
    uint32_t portValue;
    uint32_t trustAnchorValue = TLS_TRUST_STORE_FACTORY_ID;
    uint8_t enabled = 1U;
    if ((apiService_JsonString(
          request->body, "host", host, sizeof(host)
        ) == 0U)
        || (apiService_JsonString(
          request->body, "path", path, sizeof(path)
        ) == 0U)
        || (apiService_JsonNumber(
          request->body, "port", &portValue
        ) == 0U)) {
      return apiService_Error(ssl, 400, "Bad Request", "invalid_request");
    }
    (void)apiService_JsonBoolean(request->body, "enabled", &enabled);
    (void)apiService_JsonNumber(
      request->body, "trust_anchor_id", &trustAnchorValue
    );
    if ((portValue == 0U) || (portValue > 65535U))
      return apiService_Error(ssl, 400, "Bad Request", "invalid_port");
    if ((trustAnchorValue > TLS_TRUST_STORE_MAX_PERSISTED)
        || (TlsTrustStore_Exists((uint8_t)trustAnchorValue) == 0U)) {
      return apiService_Error(
        ssl, 400, "Bad Request", "invalid_trust_anchor"
      );
    }
    uint8_t assignedIndex = 0U;
    HealthCheckConfig_StatusTypeDef status = HealthCheckConfig_AddResource(
      host,
      (uint16_t)portValue,
      path,
      enabled,
      (uint8_t)trustAnchorValue,
      &assignedIndex
    );
    if (status == HEALTH_CHECK_CONFIG_STATUS_FULL)
      return apiService_Error(ssl, 409, "Conflict", "resource_limit_reached");
    if (status != HEALTH_CHECK_CONFIG_STATUS_OK)
      return apiService_Error(ssl, 400, "Bad Request", "invalid_request");
    char json[256];
    (void)snprintf(
      json,
      sizeof(json),
      "{\"index\":%u,\"host\":\"%s\",\"port\":%u,\"path\":\"%s\","
      "\"enabled\":%s,\"trust_anchor_id\":%u}",
      (unsigned int)assignedIndex,
      host,
      (unsigned int)portValue,
      path,
      enabled != 0U ? "true" : "false",
      (unsigned int)trustAnchorValue
    );
    return apiService_Respond(ssl, 201, "Created", json);
  }

  const char* resourcePrefix = "/api/v1/health-check/resources/";
  uint8_t updatingResource = ((strcmp(request->method, "PUT") == 0)
      && (strncmp(request->path, resourcePrefix, strlen(resourcePrefix)) == 0));
  uint8_t deletingResource = ((strcmp(request->method, "DELETE") == 0)
      && (strncmp(request->path, resourcePrefix, strlen(resourcePrefix)) == 0));
  if ((updatingResource != 0U) || (deletingResource != 0U)) {
    if (principal.role != USER_ROLE_ADMINISTRATOR)
      return apiService_Error(ssl, 403, "Forbidden", "forbidden");
    const char* indexText = request->path + strlen(resourcePrefix);
    char* indexEnd;
    long indexValue = strtol(indexText, &indexEnd, 10);
    if ((indexEnd == indexText) || (*indexEnd != '\0')
        || (indexValue < 0)
        || (indexValue >= HEALTH_CHECK_CONFIG_MAX_RESOURCES)) {
      return apiService_Error(ssl, 404, "Not Found", "resource_not_found");
    }
    uint8_t index = (uint8_t)indexValue;

    if (deletingResource != 0U) {
      HealthCheckConfig_StatusTypeDef status =
        HealthCheckConfig_DeleteResource(index);
      if (status == HEALTH_CHECK_CONFIG_STATUS_NOT_FOUND)
        return apiService_Error(
          ssl, 404, "Not Found", "resource_not_found"
        );
      if (status != HEALTH_CHECK_CONFIG_STATUS_OK)
        return apiService_Error(
          ssl, 500, "Internal Server Error", "storage_error"
        );
      char json[48];
      (void)snprintf(
        json, sizeof(json), "{\"index\":%u,\"deleted\":true}",
        (unsigned int)index
      );
      return apiService_Respond(ssl, 200, "OK", json);
    }

    HealthCheckConfig_ResourceTypeDef resources[
      HEALTH_CHECK_CONFIG_MAX_RESOURCES
    ];
    HealthCheckConfig_GetResources(resources);
    if (resources[index].occupied == 0U)
      return apiService_Error(ssl, 404, "Not Found", "resource_not_found");
    char host[HEALTH_CHECK_CONFIG_HOST_SIZE];
    char path[HEALTH_CHECK_CONFIG_PATH_SIZE];
    uint32_t portValue = resources[index].port;
    uint32_t trustAnchorValue = resources[index].trustAnchorId;
    uint8_t enabled = resources[index].enabled;
    (void)strncpy(host, resources[index].host, sizeof(host) - 1U);
    host[sizeof(host) - 1U] = '\0';
    (void)strncpy(path, resources[index].path, sizeof(path) - 1U);
    path[sizeof(path) - 1U] = '\0';
    (void)apiService_JsonString(request->body, "host", host, sizeof(host));
    (void)apiService_JsonString(request->body, "path", path, sizeof(path));
    (void)apiService_JsonNumber(request->body, "port", &portValue);
    (void)apiService_JsonBoolean(request->body, "enabled", &enabled);
    (void)apiService_JsonNumber(
      request->body, "trust_anchor_id", &trustAnchorValue
    );
    if ((portValue == 0U) || (portValue > 65535U))
      return apiService_Error(ssl, 400, "Bad Request", "invalid_port");
    if ((trustAnchorValue > TLS_TRUST_STORE_MAX_PERSISTED)
        || (TlsTrustStore_Exists((uint8_t)trustAnchorValue) == 0U)) {
      return apiService_Error(
        ssl, 400, "Bad Request", "invalid_trust_anchor"
      );
    }
    HealthCheckConfig_StatusTypeDef status = HealthCheckConfig_UpdateResource(
      index,
      host,
      (uint16_t)portValue,
      path,
      enabled,
      (uint8_t)trustAnchorValue
    );
    if (status == HEALTH_CHECK_CONFIG_STATUS_NOT_FOUND)
      return apiService_Error(ssl, 404, "Not Found", "resource_not_found");
    if (status != HEALTH_CHECK_CONFIG_STATUS_OK)
      return apiService_Error(ssl, 400, "Bad Request", "invalid_request");
    char json[256];
    (void)snprintf(
      json,
      sizeof(json),
      "{\"index\":%u,\"host\":\"%s\",\"port\":%u,\"path\":\"%s\","
      "\"enabled\":%s,\"trust_anchor_id\":%u}",
      (unsigned int)index,
      host,
      (unsigned int)portValue,
      path,
      enabled != 0U ? "true" : "false",
      (unsigned int)trustAnchorValue
    );
    return apiService_Respond(ssl, 200, "OK", json);
  }

  if ((strcmp(request->method, "GET") == 0)
      && (strcmp(request->path, "/api/v1/health-check/logs") == 0)) {
    HealthCheckLog_EntryTypeDef entries[HEALTH_CHECK_LOG_MAX_RESULTS];
    size_t count = HealthCheckLog_GetRecent(
      entries, HEALTH_CHECK_LOG_MAX_RESULTS
    );
    char json[API_SERVICE_RESPONSE_SIZE - 192U];
    size_t used = (size_t)snprintf(json, sizeof(json), "{\"logs\":[");
    for (size_t index = 0U; index < count; ++index) {
      int written = snprintf(
        &json[used],
        sizeof(json) - used,
        "%s{\"sequence\":%lu,\"timestamp\":%lu,\"resource_index\":%u,"
        "\"status\":\"%s\",\"http_status\":%u,\"elapsed_ms\":%lu,"
        "\"detail\":%ld}",
        index == 0U ? "" : ",",
        (unsigned long)entries[index].sequence,
        (unsigned long)entries[index].timestampUnix,
        (unsigned int)entries[index].resourceIndex,
        apiService_TransportStatusText(
          (TlsTransport_StatusTypeDef)entries[index].status
        ),
        (unsigned int)entries[index].httpStatus,
        (unsigned long)entries[index].elapsedMs,
        (long)entries[index].detail
      );
      if ((written <= 0) || ((size_t)written >= (sizeof(json) - used)))
        return apiService_Error(
          ssl, 500, "Internal Server Error", "response_too_large"
        );
      used += (size_t)written;
    }
    (void)snprintf(&json[used], sizeof(json) - used, "]}");
    return apiService_Respond(ssl, 200, "OK", json);
  }

  if ((strcmp(request->method, "GET") == 0)
      && (strcmp(request->path, "/api/v1/temperature") == 0)) {
    DS18B20_MeasurementTypeDef measurements[ONEWIRE_MAX_DEVICES];
    size_t count = TemperatureService_GetMeasurements(
      measurements, ONEWIRE_MAX_DEVICES
    );
    char json[API_SERVICE_RESPONSE_SIZE - 192U];
    size_t used = (size_t)snprintf(json, sizeof(json), "{\"sensors\":[");
    for (size_t index = 0U; index < count; ++index) {
      char romHex[17];
      apiService_HexEncode(
        measurements[index].rom, sizeof(measurements[index].rom), romHex
      );
      int written;
      if (measurements[index].status == DS18B20_STATUS_OK) {
        written = snprintf(
          &json[used],
          sizeof(json) - used,
          "%s{\"rom\":\"%s\",\"temperature_centi_degrees\":%d,"
          "\"status\":\"ok\"}",
          index == 0U ? "" : ",",
          romHex,
          (int)measurements[index].temperatureCentiDegrees
        );
      } else {
        written = snprintf(
          &json[used],
          sizeof(json) - used,
          "%s{\"rom\":\"%s\",\"status\":\"%s\"}",
          index == 0U ? "" : ",",
          romHex,
          apiService_Ds18b20StatusText(measurements[index].status)
        );
      }
      if ((written <= 0) || ((size_t)written >= (sizeof(json) - used)))
        return apiService_Error(
          ssl, 500, "Internal Server Error", "response_too_large"
        );
      used += (size_t)written;
    }
    (void)snprintf(&json[used], sizeof(json) - used, "]}");
    return apiService_Respond(ssl, 200, "OK", json);
  }

  if ((strcmp(request->method, "GET") == 0)
      && (strcmp(request->path, "/api/v1/rtc") == 0)) {
    uint8_t synchronized = Rtc_IsSynchronized();
    uint32_t unixTime = 0U;
    Rtc_DateTimeTypeDef dateTime = {0};
    if (synchronized != 0U) {
      (void)Rtc_GetUnixTime(&unixTime);
      (void)Rtc_GetDateTime(&dateTime);
    }
    char json[196];
    (void)snprintf(
      json,
      sizeof(json),
      "{\"unix_time\":%lu,\"synchronized\":%s,\"date_time\":"
      "{\"year\":%u,\"month\":%u,\"day\":%u,\"hour\":%u,\"minute\":%u,"
      "\"second\":%u}}",
      (unsigned long)unixTime,
      synchronized != 0U ? "true" : "false",
      (unsigned int)dateTime.year,
      (unsigned int)dateTime.month,
      (unsigned int)dateTime.day,
      (unsigned int)dateTime.hour,
      (unsigned int)dateTime.minute,
      (unsigned int)dateTime.second
    );
    return apiService_Respond(ssl, 200, "OK", json);
  }

  return apiService_Error(ssl, 404, "Not Found", "not_found");
}

static void apiService_Task(void* argument) {
  (void)argument;

  if (AuthService_Init() != AUTH_SERVICE_STATUS_OK) {
    printf("Management API: NOR user store initialization failed.\r\n");
    for (;;)
      vTaskDelay(pdMS_TO_TICKS(1000U));
  }

  printf("Management API: user store ready.\r\n");
  while (Lwip_IsReady() == 0U)
    vTaskDelay(pdMS_TO_TICKS(250U));

  struct sockaddr_in address = {
    .sin_family = AF_INET,
    .sin_port = PP_HTONS(API_SERVICE_PORT),
    .sin_addr.s_addr = PP_HTONL(INADDR_ANY),
  };

  int listener;
  for (;;) {
    const char* failureStage = "socket";
    listener = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int socketError = (listener < 0) ? errno : 0;
    if (listener >= 0) {
      int reuse = 1;
      (void)lwip_setsockopt(
        listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)
      );
      if (lwip_bind(
            listener,
            (struct sockaddr*)&address,
            sizeof(address)
          ) != 0) {
        failureStage = "bind";
        socketError = errno;
      } else if (lwip_listen(listener, 2) != 0) {
        failureStage = "listen";
        socketError = errno;
      } else {
        break;
      }
      lwip_close(listener);
    }
    printf(
      "Management API: %s failed, errno=%d; retrying.\r\n",
      failureStage,
      socketError
    );
    vTaskDelay(pdMS_TO_TICKS(5000U));
  }
  printf("Management API: listening on TCP port %u.\r\n", API_SERVICE_PORT);

  for (;;) {
    int client = lwip_accept(listener, NULL, NULL);
    if (client < 0) {
      vTaskDelay(pdMS_TO_TICKS(100U));
      continue;
    }
    struct timeval timeout = {
      .tv_sec = API_SERVICE_TIMEOUT_MS / 1000U,
      .tv_usec = 0,
    };
    (void)lwip_setsockopt(
      client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)
    );
    (void)lwip_setsockopt(
      client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)
    );
    if (TlsPlatform_Lock() != HAL_OK) {
      lwip_close(client);
      continue;
    }

    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_x509_crt certificate;
    mbedtls_pk_context key;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context random;
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&config);
    mbedtls_x509_crt_init(&certificate);
    mbedtls_pk_init(&key);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&random);
    static const uint8_t personalization[] = "stm32-management-api";
    int result = mbedtls_ctr_drbg_seed(
      &random,
      mbedtls_entropy_func,
      &entropy,
      personalization,
      sizeof(personalization) - 1U
    );
    if (result == 0) {
      const uint8_t* certificateData;
      size_t certificateLength;
      TlsServerCredentials_GetCertificate(&certificateData, &certificateLength);
      result = mbedtls_x509_crt_parse(
        &certificate,
        certificateData,
        certificateLength
      );
    }
    if (result == 0) {
      const uint8_t* keyData;
      size_t keyLength;
      TlsServerCredentials_GetPrivateKey(&keyData, &keyLength);
      result = mbedtls_pk_parse_key(
        &key,
        keyData,
        keyLength,
        NULL,
        0U,
        mbedtls_ctr_drbg_random,
        &random
      );
    }
    if (result == 0) {
      result = mbedtls_ssl_config_defaults(
        &config,
        MBEDTLS_SSL_IS_SERVER,
        MBEDTLS_SSL_TRANSPORT_STREAM,
        MBEDTLS_SSL_PRESET_DEFAULT
      );
    }
    if (result == 0) {
      mbedtls_ssl_conf_min_tls_version(
        &config, MBEDTLS_SSL_VERSION_TLS1_3
      );
      mbedtls_ssl_conf_max_tls_version(
        &config, MBEDTLS_SSL_VERSION_TLS1_3
      );
      mbedtls_ssl_conf_rng(
        &config, mbedtls_ctr_drbg_random, &random
      );
      result = mbedtls_ssl_conf_own_cert(
        &config, &certificate, &key
      );
    }
    if (result == 0)
      result = mbedtls_ssl_setup(&ssl, &config);
    ApiService_SocketTypeDef socket = {.descriptor = client};
    if (result == 0) {
      mbedtls_ssl_set_bio(
        &ssl,
        &socket,
        apiService_Send,
        apiService_Receive,
        NULL
      );
      result = mbedtls_ssl_handshake(&ssl);
    }
    if (result == 0) {
      char requestBuffer[API_SERVICE_REQUEST_SIZE];
      ApiService_RequestTypeDef request;
      result = apiService_ReadRequest(
        &ssl, requestBuffer, sizeof(requestBuffer), &request
      );
      if (result == 0)
        (void)apiService_Dispatch(&ssl, &request);
      else
        (void)apiService_Error(
          &ssl, 400, "Bad Request", "invalid_request"
        );
      mbedtls_platform_zeroize(
        requestBuffer, sizeof(requestBuffer)
      );
    }
    (void)mbedtls_ssl_close_notify(&ssl);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&config);
    mbedtls_x509_crt_free(&certificate);
    mbedtls_pk_free(&key);
    mbedtls_ctr_drbg_free(&random);
    mbedtls_entropy_free(&entropy);
    TlsPlatform_Unlock();
    lwip_close(client);
  }
}

HAL_StatusTypeDef ApiService_Init(void) {
  return (xTaskCreateStatic(
    apiService_Task,
    "api",
    API_SERVICE_TASK_STACK_DEPTH,
    NULL,
    tskIDLE_PRIORITY + 1U,
    apiTaskStack,
    &apiTaskControlBlock
  ) != NULL) ? HAL_OK : HAL_ERROR;
}
