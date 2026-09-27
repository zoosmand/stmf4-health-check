#include "ethernetif.h"
#include "dp83848.h"
#include "lwip/ethip6.h"
#include "lwip/memp.h"
#include "netif/etharp.h"
#include "platform.h"
#include <stddef.h>
#include <string.h>

#define ETH_RX_DESC_COUNT 4U
#define ETH_TX_DESC_COUNT 4U
#define ETH_RX_BUFFER_COUNT 12U
#define ETH_RX_BUFFER_SIZE 1536U
#define ETH_TIMEOUT_MS 100U
#define DESC_OWN 0x80000000UL
#define TX_LS 0x20000000UL
#define TX_FS 0x10000000UL
#define TX_CIC_FULL 0x00C00000UL
#define TX_CHAINED 0x00100000UL
#define TX_ERROR 0x00008000UL
#define RX_FRAME_LEN 0x3FFF0000UL
#define RX_ERROR 0x00008000UL
#define RX_FS 0x00000200UL
#define RX_LS 0x00000100UL
#define RX_CHAINED 0x00004000UL

typedef struct {
  volatile uint32_t status;
  uint32_t control;
  uint32_t buffer;
  uint32_t next;
  uint32_t extendedStatus;
  uint32_t reserved;
  uint32_t timestampLow;
  uint32_t timestampHigh;
} EthernetDescriptor_TypeDef;

typedef struct {
  struct pbuf_custom custom;
  uint8_t buffer[(ETH_RX_BUFFER_SIZE + 31U) & ~31U] __ALIGNED(32);
} EthernetRxBuffer_TypeDef;

LWIP_MEMPOOL_DECLARE(RX_POOL, ETH_RX_BUFFER_COUNT,
  sizeof(EthernetRxBuffer_TypeDef), "Zero-copy RX PBUF pool");

static uint8_t macAddress[ETH_HWADDR_LEN];
static EthernetDescriptor_TypeDef rxDescriptors[ETH_RX_DESC_COUNT] __ALIGNED(4);
static EthernetDescriptor_TypeDef txDescriptors[ETH_TX_DESC_COUNT] __ALIGNED(4);
static uint32_t rxIndex;
static uint32_t txIndex;
static dp83848_Object_t phy;

static int32_t phy_Init(void);
static int32_t phy_Deinit(void);
static int32_t phy_Write(uint32_t device, uint32_t reg, uint32_t value);
static int32_t phy_Read(uint32_t device, uint32_t reg, uint32_t* value);
static int32_t phy_GetTick(void);

static dp83848_IOCtx_t phyIo = {
  phy_Init, phy_Deinit, phy_Write, phy_Read, phy_GetTick
};

static void ethernet_FreeRxBuffer(struct pbuf* packet) {
  LWIP_MEMPOOL_FREE(RX_POOL, (struct pbuf_custom*)packet);
}

static uint8_t* ethernet_AllocateRxBuffer(void) {
  struct pbuf_custom* custom = LWIP_MEMPOOL_ALLOC(RX_POOL);
  if (custom == NULL)
    return NULL;
  uint8_t* buffer = (uint8_t*)custom
    + offsetof(EthernetRxBuffer_TypeDef, buffer);
  custom->custom_free_function = ethernet_FreeRxBuffer;
  pbuf_alloced_custom(PBUF_RAW, 0U, PBUF_REF, custom,
    buffer, ETH_RX_BUFFER_SIZE);
  return buffer;
}

static void ethernet_ConfigureGpio(void) {
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN
    | RCC_AHB1ENR_GPIOCEN;
  RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
  (void)RCC->APB2ENR;
  SYSCFG->PMC |= SYSCFG_PMC_MII_RMII_SEL;
  Platform_GpioConfigure(GPIOC, 1U, 2U, 0U, 3U, 11U);
  Platform_GpioConfigure(GPIOC, 4U, 2U, 0U, 3U, 11U);
  Platform_GpioConfigure(GPIOC, 5U, 2U, 0U, 3U, 11U);
  Platform_GpioConfigure(GPIOA, 1U, 2U, 0U, 3U, 11U);
  Platform_GpioConfigure(GPIOA, 2U, 2U, 0U, 3U, 11U);
  Platform_GpioConfigure(GPIOA, 7U, 2U, 0U, 3U, 11U);
  Platform_GpioConfigure(GPIOB, 11U, 2U, 0U, 3U, 11U);
  Platform_GpioConfigure(GPIOB, 12U, 2U, 0U, 3U, 11U);
  Platform_GpioConfigure(GPIOB, 13U, 2U, 0U, 3U, 11U);
}

static Platform_StatusTypeDef ethernet_HardwareInit(void) {
  ethernet_ConfigureGpio();
  RCC->AHB1RSTR |= RCC_AHB1RSTR_ETHMACRST;
  RCC->AHB1RSTR &= ~RCC_AHB1RSTR_ETHMACRST;
  RCC->AHB1ENR |= RCC_AHB1ENR_ETHMACEN
    | RCC_AHB1ENR_ETHMACTXEN | RCC_AHB1ENR_ETHMACRXEN;
  (void)RCC->AHB1ENR;
  ETH->DMABMR |= ETH_DMABMR_SR;
  uint32_t started = Platform_GetTick();
  while ((ETH->DMABMR & ETH_DMABMR_SR) != 0U) {
    if ((Platform_GetTick() - started) >= ETH_TIMEOUT_MS)
      return PLATFORM_STATUS_TIMEOUT;
  }

  /* Match the MAC defaults previously selected by HAL.  In particular,
   * strip the FCS from Ethernet-II frames before reporting their length. */
  ETH->MACCR = ETH_MACCR_CSTF | ETH_MACCR_IPCO | ETH_MACCR_RD
    | ETH_MACCR_FES | ETH_MACCR_DM;
  ETH->MACFFR = 0U;
  ETH->MACFCR = 0U;
  ETH->MACVLANTR = 0U;
  ETH->MACA0HR = ((uint32_t)macAddress[5] << 8U) | macAddress[4];
  ETH->MACA0LR = ((uint32_t)macAddress[3] << 24U)
    | ((uint32_t)macAddress[2] << 16U)
    | ((uint32_t)macAddress[1] << 8U) | macAddress[0];
  ETH->DMAOMR = ETH_DMAOMR_RSF | ETH_DMAOMR_TSF | ETH_DMAOMR_OSF;
  ETH->DMABMR = ETH_DMABMR_AAB | ETH_DMABMR_FB | ETH_DMABMR_EDE
    | ETH_DMABMR_USP | ETH_DMABMR_RDP_32Beat | ETH_DMABMR_PBL_32Beat;

  for (uint32_t i = 0U; i < ETH_RX_DESC_COUNT; ++i) {
    uint8_t* buffer = ethernet_AllocateRxBuffer();
    if (buffer == NULL)
      return PLATFORM_STATUS_ERROR;
    rxDescriptors[i].control = RX_CHAINED | ETH_RX_BUFFER_SIZE;
    rxDescriptors[i].buffer = (uint32_t)buffer;
    rxDescriptors[i].next = (uint32_t)&rxDescriptors[(i + 1U) % ETH_RX_DESC_COUNT];
    rxDescriptors[i].extendedStatus = 0U;
    rxDescriptors[i].reserved = 0U;
    rxDescriptors[i].timestampLow = 0U;
    rxDescriptors[i].timestampHigh = 0U;
    rxDescriptors[i].status = DESC_OWN;
  }
  for (uint32_t i = 0U; i < ETH_TX_DESC_COUNT; ++i) {
    txDescriptors[i].status = TX_CHAINED;
    txDescriptors[i].control = 0U;
    txDescriptors[i].buffer = 0U;
    txDescriptors[i].next = (uint32_t)&txDescriptors[(i + 1U) % ETH_TX_DESC_COUNT];
    txDescriptors[i].extendedStatus = 0U;
    txDescriptors[i].reserved = 0U;
    txDescriptors[i].timestampLow = 0U;
    txDescriptors[i].timestampHigh = 0U;
  }
  rxIndex = 0U;
  txIndex = 0U;
  ETH->DMARDLAR = (uint32_t)rxDescriptors;
  ETH->DMATDLAR = (uint32_t)txDescriptors;
  return PLATFORM_STATUS_OK;
}

static void ethernet_Start(void) {
  ETH->MACCR |= ETH_MACCR_TE;
  (void)ETH->MACCR;
  Platform_Delay(1U);
  ETH->MACCR |= ETH_MACCR_RE;
  (void)ETH->MACCR;
  Platform_Delay(1U);
  ETH->DMAOMR |= ETH_DMAOMR_FTF;
  (void)ETH->DMAOMR;
  Platform_Delay(1U);
  ETH->DMAOMR |= ETH_DMAOMR_ST | ETH_DMAOMR_SR;
  ETH->DMARPDR = 0U;
}

static void ethernet_Stop(void) {
  ETH->DMAOMR &= ~(ETH_DMAOMR_ST | ETH_DMAOMR_SR);
  ETH->MACCR &= ~(ETH_MACCR_TE | ETH_MACCR_RE);
}

static err_t ethernet_Write(struct pbuf* packet) {
  uint32_t count = 0U;
  for (struct pbuf* part = packet; part != NULL; part = part->next) {
    uint32_t descriptorIndex = (txIndex + count) % ETH_TX_DESC_COUNT;
    if ((count == ETH_TX_DESC_COUNT)
        || ((txDescriptors[descriptorIndex].status & DESC_OWN) != 0U))
      return ERR_MEM;
    txDescriptors[descriptorIndex].buffer = (uint32_t)part->payload;
    txDescriptors[descriptorIndex].control = part->len;
    txDescriptors[descriptorIndex].status = TX_CHAINED | TX_CIC_FULL;
    ++count;
  }
  if (count == 0U)
    return ERR_OK;
  uint32_t lastIndex = (txIndex + count - 1U) % ETH_TX_DESC_COUNT;
  txDescriptors[txIndex].status |= TX_FS;
  txDescriptors[lastIndex].status |= TX_LS;
  __DSB();
  for (uint32_t i = count; i > 0U; --i) {
    uint32_t descriptorIndex = (txIndex + i - 1U) % ETH_TX_DESC_COUNT;
    txDescriptors[descriptorIndex].status |= DESC_OWN;
  }
  ETH->DMASR = ETH_DMASR_TBUS;
  ETH->DMATPDR = 0U;
  uint32_t started = Platform_GetTick();
  while ((txDescriptors[lastIndex].status & DESC_OWN) != 0U) {
    if ((Platform_GetTick() - started) >= 20U)
      return ERR_TIMEOUT;
  }
  uint32_t nextIndex = (lastIndex + 1U) % ETH_TX_DESC_COUNT;
  for (uint32_t i = 0U; i < count; ++i) {
    uint32_t descriptorIndex = (txIndex + i) % ETH_TX_DESC_COUNT;
    if ((txDescriptors[descriptorIndex].status & TX_ERROR) != 0U) {
      txIndex = nextIndex;
      return ERR_IF;
    }
  }
  txIndex = nextIndex;
  return ERR_OK;
}

static struct pbuf* ethernet_Read(void) {
  EthernetDescriptor_TypeDef* descriptor = &rxDescriptors[rxIndex];
  if ((descriptor->status & DESC_OWN) != 0U)
    return NULL;
  uint32_t status = descriptor->status;
  uint16_t length = (uint16_t)((status & RX_FRAME_LEN) >> 16U);
  uint8_t* replacement = ethernet_AllocateRxBuffer();
  struct pbuf* packet = NULL;
  if ((replacement != NULL)
      && ((status & (RX_ERROR | RX_FS | RX_LS)) == (RX_FS | RX_LS))
      && (length <= ETH_RX_BUFFER_SIZE)) {
    packet = (struct pbuf*)(descriptor->buffer
      - offsetof(EthernetRxBuffer_TypeDef, buffer));
    packet->next = NULL;
    packet->len = length;
    packet->tot_len = length;
    descriptor->buffer = (uint32_t)replacement;
  } else if (replacement != NULL) {
    pbuf_free((struct pbuf*)(replacement
      - offsetof(EthernetRxBuffer_TypeDef, buffer)));
  }
  __DSB();
  descriptor->status = DESC_OWN;
  rxIndex = (rxIndex + 1U) % ETH_RX_DESC_COUNT;
  ETH->DMASR = ETH_DMASR_RBUS;
  ETH->DMARPDR = 0U;
  return packet;
}

static err_t low_level_init(struct netif* netif) {
  uint32_t uid0 = *(__IO uint32_t*)UID_BASE;
  uint32_t uid1 = *(__IO uint32_t*)(UID_BASE + 4U);
  uint32_t uid2 = *(__IO uint32_t*)(UID_BASE + 8U);
  macAddress[0] = 0x02U;
  macAddress[1] = (uint8_t)(uid0 >> 8U);
  macAddress[2] = (uint8_t)(uid0 >> 24U);
  macAddress[3] = (uint8_t)(uid1 ^ (uid2 >> 16U));
  macAddress[4] = (uint8_t)((uid1 >> 16U) ^ uid2);
  macAddress[5] = (uint8_t)((uid0 >> 16U) ^ (uid2 >> 8U));
  LWIP_MEMPOOL_INIT(RX_POOL);
  if (ethernet_HardwareInit() != PLATFORM_STATUS_OK)
    return ERR_IF;
  netif->hwaddr_len = ETH_HWADDR_LEN;
  memcpy(netif->hwaddr, macAddress, ETH_HWADDR_LEN);
  netif->mtu = 1500U;
  netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
  if ((DP83848_RegisterBusIO(&phy, &phyIo) != DP83848_STATUS_OK)
      || (DP83848_Init(&phy) != DP83848_STATUS_OK))
    return ERR_IF;
  ethernet_link_check_state(netif);
  return ERR_OK;
}

static err_t low_level_output(struct netif* netif, struct pbuf* packet) {
  (void)netif;
  return ethernet_Write(packet);
}

void ethernetif_input(struct netif* netif) {
  struct pbuf* packet;
  while ((packet = ethernet_Read()) != NULL) {
    if (netif->input(packet, netif) != ERR_OK)
      pbuf_free(packet);
  }
}

err_t ethernetif_init(struct netif* netif) {
  LWIP_ASSERT("netif != NULL", netif != NULL);
#if LWIP_NETIF_HOSTNAME
  netif->hostname = "lwip";
#endif
  netif->name[0] = 's';
  netif->name[1] = 't';
#if LWIP_IPV4
  netif->output = etharp_output;
#endif
#if LWIP_IPV6
  netif->output_ip6 = ethip6_output;
#endif
  netif->linkoutput = low_level_output;
  return low_level_init(netif);
}

static int32_t phy_Init(void) { return 0; }
static int32_t phy_Deinit(void) { return 0; }

static int32_t phy_Wait(void) {
  uint32_t started = Platform_GetTick();
  while ((ETH->MACMIIAR & ETH_MACMIIAR_MB) != 0U) {
    if ((Platform_GetTick() - started) >= ETH_TIMEOUT_MS)
      return -1;
  }
  return 0;
}

static int32_t phy_Read(uint32_t device, uint32_t reg, uint32_t* value) {
  if ((value == NULL) || (phy_Wait() != 0))
    return -1;
  ETH->MACMIIAR = (device << ETH_MACMIIAR_PA_Pos)
    | (reg << ETH_MACMIIAR_MR_Pos) | ETH_MACMIIAR_CR_Div102
    | ETH_MACMIIAR_MB;
  if (phy_Wait() != 0)
    return -1;
  *value = ETH->MACMIIDR;
  return 0;
}

static int32_t phy_Write(uint32_t device, uint32_t reg, uint32_t value) {
  if (phy_Wait() != 0)
    return -1;
  ETH->MACMIIDR = value;
  ETH->MACMIIAR = (device << ETH_MACMIIAR_PA_Pos)
    | (reg << ETH_MACMIIAR_MR_Pos) | ETH_MACMIIAR_CR_Div102
    | ETH_MACMIIAR_MW | ETH_MACMIIAR_MB;
  return phy_Wait();
}

static int32_t phy_GetTick(void) { return (int32_t)Platform_GetTick(); }

void ethernet_link_check_state(struct netif* netif) {
  int32_t link = DP83848_GetLinkState(&phy);
  if (netif_is_link_up(netif) && (link <= DP83848_STATUS_LINK_DOWN)) {
    ethernet_Stop();
    netif_set_down(netif);
    netif_set_link_down(netif);
    return;
  }
  if (netif_is_link_up(netif) || (link <= DP83848_STATUS_LINK_DOWN))
    return;
  uint32_t mac = ETH->MACCR & ~(ETH_MACCR_DM | ETH_MACCR_FES);
  if ((link == DP83848_STATUS_100MBITS_FULLDUPLEX)
      || (link == DP83848_STATUS_100MBITS_HALFDUPLEX))
    mac |= ETH_MACCR_FES;
  if ((link == DP83848_STATUS_100MBITS_FULLDUPLEX)
      || (link == DP83848_STATUS_10MBITS_FULLDUPLEX))
    mac |= ETH_MACCR_DM;
  ETH->MACCR = mac;
  ethernet_Start();
  netif_set_up(netif);
  netif_set_link_up(netif);
}
