#include "usb_test.h"

#include "cdc_rx_flow.h"
#include "main.h"
#include "usbd_cdc.h"
#include "usbd_core.h"
#include "usbd_desc.h"
#include "stm32h7xx_ll_usb.h"

#include <string.h>

#define USB_TEST_RX_BUFFER_SIZE  CDC_DATA_FS_OUT_PACKET_SIZE
#define USB_TEST_LINE_SIZE       64U
#define USB_TEST_TX_BUFFER_SIZE 1024U

extern USBD_DescriptorsTypeDef VCP_Desc;
extern volatile uint32_t usb_diag_irq_count;
extern volatile uint32_t usb_diag_reset_count;
extern volatile uint32_t usb_diag_enum_done_count;
extern volatile uint32_t usb_diag_setup_count;
extern volatile uint32_t usb_diag_connect_count;
extern volatile uint32_t usb_diag_disconnect_count;

USBD_HandleTypeDef hUsbDeviceFS;

static uint8_t usb_rx_buffer[USB_TEST_RX_BUFFER_SIZE] __attribute__((aligned(4)));
/* 8lw.24: the OUT endpoint is re-armed only while a full packet fits, so the
 * host is NAKed instead of losing bytes when commands arrive faster than the
 * foreground handles them. */
static CdcRxFlow usb_rx_flow;
static uint8_t usb_tx_buffer[USB_TEST_TX_BUFFER_SIZE] __attribute__((aligned(4)));
static char usb_line_buffer[USB_TEST_LINE_SIZE];
static volatile bool usb_tx_busy;
static uint32_t usb_line_length;
static bool usb_line_overflow;
static bool usb_started;
static bool usb_banner_pending;
static bool usb_vbus_present;
static bool usb_ever_configured;
static uint8_t usb_previous_state;
static uint32_t usb_diag_next_tick;
static UsbTestLineHandler usb_line_handler;

static USBD_CDC_LineCodingTypeDef usb_line_coding =
{
  115200U,
  0U,
  0U,
  8U
};

static int8_t UsbCdcInit(void)
{
  uint32_t USBx_BASE = (uint32_t)USB_OTG_FS;

  /* 8lw.24: the HAL's USB-reset handler unmasks an interrupt for every NAKed OUT
   * token, and its handler only clears the flag. While reception is paused for
   * flow control the host retries continuously, and those interrupts starved the
   * foreground until a recording overran. Configuration follows every reset, so
   * masking it here keeps it masked. */
  USBx_DEVICE->DOEPMSK &= ~USB_OTG_DOEPMSK_NAKM;
  CdcRxFlow_Init(&usb_rx_flow, USB_TEST_RX_BUFFER_SIZE);
  usb_line_length = 0U;
  usb_line_overflow = false;
  usb_tx_busy = false;
  (void)USBD_CDC_SetTxBuffer(&hUsbDeviceFS, usb_tx_buffer, 0U);
  (void)USBD_CDC_SetRxBuffer(&hUsbDeviceFS, usb_rx_buffer);
  return (int8_t)USBD_OK;
}

static int8_t UsbCdcDeInit(void)
{
  CdcRxFlow_Init(&usb_rx_flow, USB_TEST_RX_BUFFER_SIZE);
  usb_line_length = 0U;
  usb_line_overflow = false;
  usb_tx_busy = false;
  return (int8_t)USBD_OK;
}

static int8_t UsbCdcControl(uint8_t command, uint8_t *buffer, uint16_t length)
{
  (void)length;
  if (command == CDC_SET_LINE_CODING)
  {
    if (buffer == NULL)
    {
      return (int8_t)USBD_FAIL;
    }
    usb_line_coding.bitrate = (uint32_t)buffer[0] |
                              ((uint32_t)buffer[1] << 8) |
                              ((uint32_t)buffer[2] << 16) |
                              ((uint32_t)buffer[3] << 24);
    usb_line_coding.format = buffer[4];
    usb_line_coding.paritytype = buffer[5];
    usb_line_coding.datatype = buffer[6];
  }
  else if (command == CDC_GET_LINE_CODING)
  {
    if (buffer == NULL)
    {
      return (int8_t)USBD_FAIL;
    }
    buffer[0] = (uint8_t)usb_line_coding.bitrate;
    buffer[1] = (uint8_t)(usb_line_coding.bitrate >> 8);
    buffer[2] = (uint8_t)(usb_line_coding.bitrate >> 16);
    buffer[3] = (uint8_t)(usb_line_coding.bitrate >> 24);
    buffer[4] = usb_line_coding.format;
    buffer[5] = usb_line_coding.paritytype;
    buffer[6] = usb_line_coding.datatype;
  }
  return (int8_t)USBD_OK;
}

static int8_t UsbCdcReceive(uint8_t *buffer, uint32_t *length)
{
  uint32_t packet_length;

  if ((buffer == NULL) || (length == NULL))
  {
    return (int8_t)USBD_FAIL;
  }
  packet_length = (*length <= USB_TEST_RX_BUFFER_SIZE) ?
                  *length : USB_TEST_RX_BUFFER_SIZE;
  if (CdcRxFlow_Push(&usb_rx_flow, buffer, packet_length))
  {
    (void)USBD_CDC_SetRxBuffer(&hUsbDeviceFS, usb_rx_buffer);
    (void)USBD_CDC_ReceivePacket(&hUsbDeviceFS);
  }
  return (int8_t)USBD_OK;
}

/* Re-arms a paused OUT endpoint once the foreground has made room. The endpoint
 * is idle while paused; the interrupt is masked only so that the USB stack's
 * own state is not touched from two contexts at once. */
static void UsbResumeReceive(void)
{
  if (CdcRxFlow_Resume(&usb_rx_flow))
  {
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    (void)USBD_CDC_SetRxBuffer(&hUsbDeviceFS, usb_rx_buffer);
    (void)USBD_CDC_ReceivePacket(&hUsbDeviceFS);
    HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
  }
}

bool UsbTest_HostAttached(void)
{
  return usb_started && (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED);
}

void UsbTest_GetRxStats(UsbTestRxStats *stats)
{
  if (stats == NULL)
  {
    return;
  }
  stats->packets = usb_rx_flow.packets;
  stats->pauses = usb_rx_flow.pauses;
  stats->overruns = usb_rx_flow.overruns;
  stats->paused = usb_rx_flow.paused;
  stats->queued = (uint32_t)(CDC_RX_FLOW_CAPACITY - 1U - CdcRxFlow_Free(&usb_rx_flow));
}

static int8_t UsbCdcTransmitComplete(uint8_t *buffer, uint32_t *length,
                                     uint8_t endpoint)
{
  (void)buffer;
  (void)length;
  (void)endpoint;
  usb_tx_busy = false;
  return (int8_t)USBD_OK;
}

static USBD_CDC_ItfTypeDef usb_cdc_interface =
{
  UsbCdcInit,
  UsbCdcDeInit,
  UsbCdcControl,
  UsbCdcReceive,
  UsbCdcTransmitComplete
};

static bool UsbClockStart(void)
{
  RCC_OscInitTypeDef oscillator = {0};
  RCC_PeriphCLKInitTypeDef peripheral_clock = {0};
  RCC_CRSInitTypeDef crs = {0};

  oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSI48;
  oscillator.HSI48State = RCC_HSI48_ON;
  oscillator.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&oscillator) != HAL_OK)
  {
    return false;
  }

  peripheral_clock.PeriphClockSelection = RCC_PERIPHCLK_USB;
  peripheral_clock.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
  if (HAL_RCCEx_PeriphCLKConfig(&peripheral_clock) != HAL_OK)
  {
    return false;
  }

  __HAL_RCC_CRS_CLK_ENABLE();
  crs.Prescaler = RCC_CRS_SYNC_DIV1;
  crs.Source = RCC_CRS_SYNC_SOURCE_USB2;
  crs.Polarity = RCC_CRS_SYNC_POLARITY_RISING;
  crs.ReloadValue = RCC_CRS_RELOADVALUE_DEFAULT;
  crs.ErrorLimitValue = RCC_CRS_ERRORLIMIT_DEFAULT;
  crs.HSI48CalibrationValue = RCC_CRS_HSI48CALIBRATION_DEFAULT;
  HAL_RCCEx_CRSConfig(&crs);
  return true;
}

static bool UsbSupplyReady(void)
{
  uint32_t start_tick;

  /* MB1363 fits SB18 and SB19, externally supplying both VDD50USB and
     VDD33USB from 3V3. Do not enable the STM32's internal USB regulator;
     enable its supply detector and wait for the external rail to qualify. */
  HAL_PWREx_EnableUSBVoltageDetector();
  start_tick = HAL_GetTick();
  while (__HAL_PWR_GET_FLAG(PWR_FLAG_USB33RDY) == 0U)
  {
    if ((HAL_GetTick() - start_tick) > 10U)
    {
      printf("[usb] FAIL: VDD33USB not ready (PWR_CR3=%08lX)\r\n",
             (unsigned long)PWR->CR3);
      return false;
    }
  }
  printf("[usb] VDD33USB ready from Nucleo 3V3 (PWR_CR3=%08lX)\r\n",
         (unsigned long)PWR->CR3);
  return true;
}

bool UsbTest_Start(void)
{
  printf("\r\n[usb] USB FS CDC command console\r\n");
  printf("[usb] PA9 VBUS sense, PA11 DM, PA12 DP; PA10 remains radio reset\r\n");
  printf("[usb] self-powered configuration; VBUS budget advertised as 500 mA\r\n");
  printf("[usb] system power source is selected by the backplane and Nucleo jumpers\r\n");
  printf("[usb] PA9 monitored in GPIO; core B-session-valid override enabled\r\n");

  if (!UsbSupplyReady())
  {
    return false;
  }
  if (!UsbClockStart())
  {
    printf("[usb] FAIL: HSI48/CRS clock setup\r\n");
    return false;
  }
  if ((USBD_Init(&hUsbDeviceFS, &VCP_Desc, 0U) != USBD_OK) ||
      (USBD_RegisterClass(&hUsbDeviceFS, USBD_CDC_CLASS) != USBD_OK) ||
      (USBD_CDC_RegisterInterface(&hUsbDeviceFS, &usb_cdc_interface) != USBD_OK) ||
      (USBD_Start(&hUsbDeviceFS) != USBD_OK))
  {
    printf("[usb] FAIL: device-stack initialization\r\n");
    return false;
  }

  usb_started = true;
  usb_ever_configured = false;
  usb_previous_state = hUsbDeviceFS.dev_state;
  usb_vbus_present = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_9) == GPIO_PIN_SET;
  usb_diag_next_tick = HAL_GetTick() + 2000U;
  printf("[usb] HSI48 active; VBUS=%s; waiting for host enumeration\r\n",
         usb_vbus_present ? "present" : "absent");
  return true;
}

void UsbTest_Stop(void)
{
  GPIO_InitTypeDef gpio = {0};

  if (usb_started)
  {
    (void)USBD_Stop(&hUsbDeviceFS);
    (void)USBD_DeInit(&hUsbDeviceFS);
  }
  usb_started = false;
  usb_tx_busy = false;
  usb_banner_pending = false;

  HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
  HAL_NVIC_ClearPendingIRQ(OTG_FS_IRQn);
  __HAL_RCC_CRS_CLK_DISABLE();
  __HAL_RCC_HSI48_DISABLE();
  HAL_PWREx_DisableUSBVoltageDetector();

  /* Leave the external USB wires high impedance while CDC is shut down. */
  gpio.Pin = GPIO_PIN_9 | GPIO_PIN_11 | GPIO_PIN_12;
  gpio.Mode = GPIO_MODE_ANALOG;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &gpio);
}

static void UsbPrintDiagnostics(void)
{
  uint32_t USBx_BASE = (uint32_t)USB_OTG_FS;
  uint32_t gotgctl = USB_OTG_FS->GOTGCTL;
  uint32_t gccfg = USB_OTG_FS->GCCFG;
  uint32_t gusbcfg = USB_OTG_FS->GUSBCFG;
  uint32_t gahbcfg = USB_OTG_FS->GAHBCFG;
  uint32_t gintsts = USB_OTG_FS->GINTSTS;
  uint32_t gintmsk = USB_OTG_FS->GINTMSK;
  uint32_t grstctl = USB_OTG_FS->GRSTCTL;
  uint32_t pcgcctl = USBx_PCGCCTL;
  uint32_t dctl = USBx_DEVICE->DCTL;
  uint32_t dsts = USBx_DEVICE->DSTS;
  uint32_t dcfg = USBx_DEVICE->DCFG;
  uint32_t pwr_cr3 = PWR->CR3;
  uint32_t rcc_cr = RCC->CR;
  uint32_t rcc_d2ccip2r = RCC->D2CCIP2R;
  uint32_t rcc_ahb1enr = RCC->AHB1ENR;
  uint32_t crs_cr = CRS->CR;
  uint32_t crs_cfgr = CRS->CFGR;
  uint32_t crs_isr = CRS->ISR;

  printf("[usb] diag: irq=%lu reset=%lu enum=%lu setup=%lu connect=%lu disconnect=%lu\r\n",
         (unsigned long)usb_diag_irq_count,
         (unsigned long)usb_diag_reset_count,
         (unsigned long)usb_diag_enum_done_count,
         (unsigned long)usb_diag_setup_count,
         (unsigned long)usb_diag_connect_count,
         (unsigned long)usb_diag_disconnect_count);
  printf("[usb] diag: BSVLD=%u BVALOEN=%u BVALOVAL=%u VBDEN=%u SDIS=%u suspend=%u GOTGCTL=%08lX GCCFG=%08lX DSTS=%08lX\r\n",
         (gotgctl & USB_OTG_GOTGCTL_BSESVLD) != 0U,
         (gotgctl & USB_OTG_GOTGCTL_BVALOEN) != 0U,
         (gotgctl & USB_OTG_GOTGCTL_BVALOVAL) != 0U,
         (gccfg & USB_OTG_GCCFG_VBDEN) != 0U,
         (dctl & USB_OTG_DCTL_SDIS) != 0U,
         (dsts & USB_OTG_DSTS_SUSPSTS) != 0U,
         (unsigned long)gotgctl, (unsigned long)gccfg,
         (unsigned long)dsts);
  printf("[usb] diag: mode=%s FDMOD=%u FHMOD=%u PHYSEL=%u STOPCLK=%u GATECLK=%u GUSBCFG=%08lX GINTSTS=%08lX GRSTCTL=%08lX PCGCCTL=%08lX DCFG=%08lX\r\n",
         (gintsts & USB_OTG_GINTSTS_CMOD) != 0U ? "host" : "device",
         (gusbcfg & USB_OTG_GUSBCFG_FDMOD) != 0U,
         (gusbcfg & USB_OTG_GUSBCFG_FHMOD) != 0U,
         (gusbcfg & USB_OTG_GUSBCFG_PHYSEL) != 0U,
         (pcgcctl & USB_OTG_PCGCCTL_STOPCLK) != 0U,
         (pcgcctl & USB_OTG_PCGCCTL_GATECLK) != 0U,
         (unsigned long)gusbcfg, (unsigned long)gintsts,
         (unsigned long)grstctl, (unsigned long)pcgcctl,
         (unsigned long)dcfg);
  printf("[usb] diag: GINT=%u USBRSTM=%u USBRST=%u NVIC_EN=%lu NVIC_PEND=%lu GAHBCFG=%08lX GINTMSK=%08lX DCTL=%08lX\r\n",
         (gahbcfg & USB_OTG_GAHBCFG_GINT) != 0U,
         (gintmsk & USB_OTG_GINTMSK_USBRST) != 0U,
         (gintsts & USB_OTG_GINTSTS_USBRST) != 0U,
         (unsigned long)NVIC_GetEnableIRQ(OTG_FS_IRQn),
         (unsigned long)NVIC_GetPendingIRQ(OTG_FS_IRQn),
         (unsigned long)gahbcfg, (unsigned long)gintmsk,
         (unsigned long)dctl);
  printf("[usb] diag: HSI48ON=%u HSI48RDY=%u USBSEL=%lu USB2CLKEN=%u RCC_CR=%08lX D2CCIP2R=%08lX AHB1ENR=%08lX\r\n",
         (rcc_cr & RCC_CR_HSI48ON) != 0U,
         (rcc_cr & RCC_CR_HSI48RDY) != 0U,
         (unsigned long)((rcc_d2ccip2r & RCC_D2CCIP2R_USBSEL) >>
                         RCC_D2CCIP2R_USBSEL_Pos),
         (rcc_ahb1enr & RCC_AHB1ENR_USB2OTGFSEN) != 0U,
         (unsigned long)rcc_cr, (unsigned long)rcc_d2ccip2r,
         (unsigned long)rcc_ahb1enr);
  printf("[usb] diag: CRS_CEN=%u AUTOTRIM=%u SYNCOK=%u SYNCWARN=%u CRS_CR=%08lX CFGR=%08lX ISR=%08lX\r\n",
         (crs_cr & CRS_CR_CEN) != 0U,
         (crs_cr & CRS_CR_AUTOTRIMEN) != 0U,
         (crs_isr & CRS_ISR_SYNCOKF) != 0U,
         (crs_isr & CRS_ISR_SYNCWARNF) != 0U,
         (unsigned long)crs_cr, (unsigned long)crs_cfgr,
         (unsigned long)crs_isr);
  printf("[usb] diag: USB33RDY=%u USBREGEN=%u USB33DEN=%u PWR_CR3=%08lX\r\n",
         (pwr_cr3 & PWR_CR3_USB33RDY) != 0U,
         (pwr_cr3 & PWR_CR3_USBREGEN) != 0U,
         (pwr_cr3 & PWR_CR3_USB33DEN) != 0U,
         (unsigned long)pwr_cr3);
  printf("[usb] diag: PA11 mode=%lu AF=%lX level=%u; PA12 mode=%lu AF=%lX level=%u\r\n",
         (unsigned long)((GPIOA->MODER >> (11U * 2U)) & 3U),
         (unsigned long)((GPIOA->AFR[1] >> ((11U - 8U) * 4U)) & 0xFU),
         HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_11) == GPIO_PIN_SET,
         (unsigned long)((GPIOA->MODER >> (12U * 2U)) & 3U),
         (unsigned long)((GPIOA->AFR[1] >> ((12U - 8U) * 4U)) & 0xFU),
         HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_12) == GPIO_PIN_SET);

  if ((gintsts & USB_OTG_GINTSTS_CMOD) != 0U)
  {
    printf("[usb] hint: core entered host mode; PA10/USB-ID interaction is overriding device mode\r\n");
  }
  else if ((gotgctl & USB_OTG_GOTGCTL_BSESVLD) == 0U)
  {
    printf("[usb] hint: GPIO PA9 is high but the USB core does not see valid VBUS\r\n");
  }
  else if ((dctl & USB_OTG_DCTL_SDIS) != 0U)
  {
    printf("[usb] hint: PHY is soft-disconnected; firmware/PCD start failed\r\n");
  }
  else if (usb_diag_reset_count == 0U)
  {
    printf("[usb] hint: no host reset seen; check D+ idle-high, cable, D+/D- continuity\r\n");
  }
  else if (usb_diag_setup_count == 0U)
  {
    printf("[usb] hint: host reset seen but no SETUP packet; check D+/D- integrity and 48 MHz clock\r\n");
  }
  else
  {
    printf("[usb] hint: control requests arrived; enumeration stopped in descriptor/control handling\r\n");
  }
}

bool UsbTest_SendData(const uint8_t *data, uint16_t length)
{
  if ((data == NULL) || (length == 0U) ||
      (length > USB_TEST_TX_BUFFER_SIZE) || usb_tx_busy)
  {
    return false;
  }
  if (data != usb_tx_buffer)
  {
    memcpy(usb_tx_buffer, data, length);
  }
  usb_tx_busy = true;
  (void)USBD_CDC_SetTxBuffer(&hUsbDeviceFS, usb_tx_buffer, length);
  if (USBD_CDC_TransmitPacket(&hUsbDeviceFS) != USBD_OK)
  {
    usb_tx_busy = false;
    return false;
  }
  return true;
}

void UsbTest_SetLineHandler(UsbTestLineHandler handler)
{
  usb_line_handler = handler;
}

bool UsbTest_SendText(const char *message)
{
  if (message == NULL)
  {
    return false;
  }
  size_t length = strlen(message);
  return (length <= UINT16_MAX) &&
         UsbTest_SendData((const uint8_t *)message, (uint16_t)length);
}

static void UsbProcessNextLine(void)
{
  uint8_t character;

  while (CdcRxFlow_Pop(&usb_rx_flow, &character))
  {

    if ((character == '\r') || (character == '\n'))
    {
      if (usb_line_overflow)
      {
        (void)UsbTest_SendText("ERR command too long\r\n");
        usb_line_length = 0U;
        usb_line_overflow = false;
        return;
      }
      if (usb_line_length == 0U)
      {
        continue;
      }

      usb_line_buffer[usb_line_length] = '\0';
      if (usb_line_handler != NULL)
      {
        usb_line_handler(usb_line_buffer);
      }
      else
      {
        (void)UsbTest_SendText("ERR no command handler\r\n");
      }
      usb_line_length = 0U;
      return;
    }

    if (!usb_line_overflow)
    {
      if (usb_line_length < (USB_TEST_LINE_SIZE - 1U))
      {
        usb_line_buffer[usb_line_length++] = (char)character;
      }
      else
      {
        usb_line_overflow = true;
      }
    }
  }
}

void UsbTest_Service(void)
{
  static const uint8_t banner[] =
    "Spooky Box USB CLI ready\r\nType HELP for commands.\r\n";
  bool vbus_present;
  uint8_t state;

  if (!usb_started)
  {
    return;
  }

  vbus_present = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_9) == GPIO_PIN_SET;
  if (vbus_present != usb_vbus_present)
  {
    usb_vbus_present = vbus_present;
    printf("[usb] VBUS %s\r\n", vbus_present ? "present" : "removed");
  }

  state = hUsbDeviceFS.dev_state;
  if (state != usb_previous_state)
  {
    if (state == USBD_STATE_CONFIGURED)
    {
      if (!usb_ever_configured)
      {
        printf("[usb] PASS: host configured Spooky Box USB CDC CLI\r\n");
        usb_banner_pending = true;
      }
      usb_ever_configured = true;
      BSP_LED_On(LED_YELLOW);
    }
    else if (state == USBD_STATE_SUSPENDED)
    {
      usb_tx_busy = false;
    }
    else if (usb_ever_configured &&
             (state != USBD_STATE_SUSPENDED) &&
             ((usb_previous_state == USBD_STATE_CONFIGURED) ||
              (usb_previous_state == USBD_STATE_SUSPENDED)))
    {
      printf("[usb] host disconnected or deconfigured (state=%u)\r\n", state);
      BSP_LED_Off(LED_YELLOW);
      usb_tx_busy = false;
    }
    usb_previous_state = state;
  }

  if (!usb_ever_configured &&
      (state != USBD_STATE_CONFIGURED) &&
      (state != USBD_STATE_SUSPENDED) &&
      ((int32_t)(HAL_GetTick() - usb_diag_next_tick) >= 0))
  {
    UsbPrintDiagnostics();
    usb_diag_next_tick = HAL_GetTick() + 5000U;
  }

  if ((state != USBD_STATE_CONFIGURED) || usb_tx_busy)
  {
    return;
  }
  if (usb_banner_pending &&
      UsbTest_SendData(banner, (uint16_t)(sizeof(banner) - 1U)))
  {
    usb_banner_pending = false;
    return;
  }
  UsbProcessNextLine();
  UsbResumeReceive();
}
