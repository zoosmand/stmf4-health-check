#ifndef CALLBACK_SERVICE_H
#define CALLBACK_SERVICE_H

#include "main.h"
#include "tls_transport.h"

#include <stdint.h>

ErrorStatus CallbackService_Init(void);
void CallbackService_Enqueue(
  uint8_t resource,
  uint8_t healthy,
  const TlsTransport_ResultTypeDef* result
);

#endif /* CALLBACK_SERVICE_H */
