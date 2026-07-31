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
#include "lwip.h"
#include "lwip/sockets.h"
#include "management_server_credentials.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/pk.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "task.h"
#include "tls_platform.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define API_SERVICE_PORT              443U
#define API_SERVICE_TASK_STACK_DEPTH  3072U
#define API_SERVICE_REQUEST_SIZE      1536U
#define API_SERVICE_RESPONSE_SIZE     1536U
#define API_SERVICE_BODY_SIZE         512U
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

static int apiService_Respond(
  mbedtls_ssl_context* ssl,
  int status,
  const char* reason,
  const char* json
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
    json
  );
  if ((length <= 0) || ((size_t)length >= sizeof(response)))
    return MBEDTLS_ERR_SSL_BUFFER_TOO_SMALL;
  return apiService_WriteAll(ssl, response, (size_t)length);
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
      result = mbedtls_x509_crt_parse(
        &certificate,
        managementServerCertificate,
        sizeof(managementServerCertificate)
      );
    }
    if (result == 0) {
      result = mbedtls_pk_parse_key(
        &key,
        managementServerPrivateKey,
        sizeof(managementServerPrivateKey),
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
