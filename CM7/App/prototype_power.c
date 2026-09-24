/* Prototype charging-monitor policy; not the final product power manager. */
#include "prototype_power.h"
#include "target_logger.h"
#include "diagnostics.h"
#include <stdbool.h>
#include "main.h"
#include "fuel_gauge_test.h"
#include "magnetometer_test.h"
#include "radio_recorder.h"
#include "sd_test.h"
#include "ui_board_test.h"
#include "usb_test.h"

#define SLEEP_REPORT_PERIOD_SECONDS    300U
#define SLEEP_WAKE_SELF_TEST_SECONDS   10U
#define SLEEP_RAIL_STARTUP_MS          10U
#define SLEEP_RTC_TIMEOUT_MS           1000U

static volatile bool sleep_requested;
static volatile bool sleep_report_due;
static volatile bool sleep_reset_requested;
static bool sleep_mode_active;
static bool sleep_first_wake;

static bool SleepRtcWaitForFlag(uint32_t flag)
{
  const uint32_t start_tick = HAL_GetTick();

  while ((RTC->ISR & flag) == 0U)
  {
    if ((HAL_GetTick() - start_tick) > SLEEP_RTC_TIMEOUT_MS)
    {
      return false;
    }
  }
  return true;
}

static bool SleepRtcArmWakeTimer(uint32_t period_seconds)
{
  if ((period_seconds == 0U) || (period_seconds > 65536U))
  {
    return false;
  }

  /* RTC write-protection unlock sequence. */
  RTC->WPR = 0xCAU;
  RTC->WPR = 0x53U;

  RTC->CR &= ~(RTC_CR_WUTE | RTC_CR_WUTIE);
  if (!SleepRtcWaitForFlag(RTC_ISR_WUTWF))
  {
    RTC->WPR = 0xFFU;
    return false;
  }

  RTC->WUTR = period_seconds - 1U;
  MODIFY_REG(RTC->CR, RTC_CR_WUCKSEL, RTC_CR_WUCKSEL_2); /* ck_spre = 1 Hz */

  /* WUTF is cleared by writing zero while the other writable flags remain one. */
  RTC->ISR = (~(RTC_ISR_WUTF | RTC_ISR_INIT)) |
             (RTC->ISR & RTC_ISR_INIT);
  HAL_EXTI_D1_ClearFlag(EXTI_LINE19);
  HAL_EXTI_D2_ClearFlag(EXTI_LINE19);

  RTC->CR |= RTC_CR_WUTIE | RTC_CR_WUTE;
  RTC->WPR = 0xFFU;
  return true;
}

static bool SleepReportRtcStart(void)
{
  uint32_t start_tick;

  HAL_PWR_EnableBkUpAccess();

  /* SystemClock_Config has already started LSE for the radio reference clock. */
  if ((RCC->BDCR & RCC_BDCR_RTCSEL_Msk) == 0U)
  {
    __HAL_RCC_RTC_CONFIG(RCC_RTCCLKSOURCE_LSE);
  }
  else if ((RCC->BDCR & RCC_BDCR_RTCSEL_Msk) != RCC_RTCCLKSOURCE_LSE)
  {
    return false;
  }

  __HAL_RCC_RTC_ENABLE();
  __HAL_RCC_RTC_CLK_ENABLE();
  __HAL_RCC_RTC_CLK_SLEEP_ENABLE();

  /* Establish a 1 Hz ck_spre timebase from the 32768 Hz LSE. */
  RTC->WPR = 0xCAU;
  RTC->WPR = 0x53U;
  if (((RTC->ISR & RTC_ISR_INITS) == 0U) ||
      ((RTC->PRER & (RTC_PRER_PREDIV_A | RTC_PRER_PREDIV_S)) !=
       ((127U << RTC_PRER_PREDIV_A_Pos) |
        (255U << RTC_PRER_PREDIV_S_Pos))))
  {
    RTC->ISR = 0xFFFFFFFFU;
    start_tick = HAL_GetTick();
    while ((RTC->ISR & RTC_ISR_INITF) == 0U)
    {
      if ((HAL_GetTick() - start_tick) > SLEEP_RTC_TIMEOUT_MS)
      {
        RTC->WPR = 0xFFU;
        return false;
      }
    }
    RTC->PRER = (127U << RTC_PRER_PREDIV_A_Pos) |
                (255U << RTC_PRER_PREDIV_S_Pos);
    RTC->ISR &= ~RTC_ISR_INIT;
  }
  RTC->WPR = 0xFFU;

  /* D1 gets the interrupt.  The D2 event also wakes that domain so UART7 and
   * I2C2 are clock-ready when CM7 performs the charging report. */
  HAL_EXTI_D1_EventInputConfig(EXTI_LINE19, EXTI_MODE_IT, ENABLE);
  HAL_EXTI_D2_EventInputConfig(EXTI_LINE19, EXTI_MODE_EVT, ENABLE);
  EXTI->FTSR1 &= ~(1UL << EXTI_LINE19);
  EXTI->RTSR1 |= (1UL << EXTI_LINE19);
  HAL_NVIC_ClearPendingIRQ(RTC_WKUP_IRQn);
  HAL_NVIC_SetPriority(RTC_WKUP_IRQn, 7U, 0U);
  HAL_NVIC_EnableIRQ(RTC_WKUP_IRQn);

  /* The first wake is deliberately quick so this path can be verified without
   * waiting five minutes.  The foreground re-arms it for the normal cadence. */
  return SleepRtcArmWakeTimer(SLEEP_WAKE_SELF_TEST_SECONDS);
}

static void PrototypeSleepRun(void (*stop_radio_audio)(void))
{
  sleep_mode_active = true;
  sleep_report_due = false;
  sleep_reset_requested = false;
  sleep_first_wake = true;
  Diagnostics_Record(DIAG_SLEEP, 1U, 0U);

  printf("\r\n[sleep] preparing low-power charging monitor\r\n");
  printf("[sleep] reporting once before shutdown\r\n");
  (void)FuelGaugeTest_ReportNow();
  (void)MagnetometerTest_Sleep();
  UiBoardTest_SafeOff();
  RadioRecorder_Stop();
  SdTest_Stop();
  stop_radio_audio();
  BSP_LED_Off(LED_GREEN);
  BSP_LED_Off(LED_YELLOW);
  BSP_LED_Off(LED_RED);

  /* Give the CDC acknowledgement time to leave before disconnecting the PHY. */
  HAL_Delay(100U);
  UsbTest_Stop();
  printf("[sleep] USB CDC stopped; AUX UART7 remains active\r\n");
  printf("[sleep] RTC wake self-test in 10 seconds, then reports every 5 minutes\r\n");
  printf("[sleep] 3V3_VSYS turns on only while reading the gauge\r\n");
  printf("[sleep] 5V_VSYS and the Babysitter power path remain on\r\n");
  printf("[sleep] press the blue USER button or RESET to reboot\r\n");

  if (!SleepReportRtcStart())
  {
    printf("[sleep] FAIL: LSE/RTC wake timer did not start; rebooting safely\r\n");
    (void)TargetLogger_Quiesce(400U);
    NVIC_SystemReset();
  }
  HAL_GPIO_WritePin(REG_3V3_EN_GPIO_Port, REG_3V3_EN_Pin, GPIO_PIN_RESET);
  __HAL_RCC_I2C2_CLK_DISABLE();
  printf("[sleep] 3V3_VSYS disabled; CM7 entering SLEEP mode\r\n");
  (void)TargetLogger_Quiesce(400U);
  HAL_SuspendTick();

  for (;;)
  {
    HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);

    if (sleep_reset_requested)
    {
      NVIC_SystemReset();
    }
    if (sleep_report_due)
    {
      sleep_report_due = false;
      HAL_ResumeTick();
      Diagnostics_Record(DIAG_SLEEP, 2U, 0U);
      if (sleep_first_wake)
      {
        sleep_first_wake = false;
        if (!SleepRtcArmWakeTimer(SLEEP_REPORT_PERIOD_SECONDS))
        {
          printf("[sleep] FAIL: could not arm five-minute RTC wake; rebooting\r\n");
          (void)TargetLogger_Quiesce(400U);
          NVIC_SystemReset();
        }
        printf("[sleep] RTC wake self-test passed; five-minute cadence armed\r\n");
      }
      HAL_GPIO_WritePin(REG_3V3_EN_GPIO_Port, REG_3V3_EN_Pin, GPIO_PIN_SET);
      HAL_Delay(SLEEP_RAIL_STARTUP_MS);
      __HAL_RCC_I2C2_CLK_ENABLE();
      (void)FuelGaugeTest_ReportNow();
      __HAL_RCC_I2C2_CLK_DISABLE();
      HAL_GPIO_WritePin(REG_3V3_EN_GPIO_Port, REG_3V3_EN_Pin, GPIO_PIN_RESET);
      (void)TargetLogger_Quiesce(400U);
      HAL_SuspendTick();
    }
  }
}

void PrototypePower_OnRtcWake(void)
{
  HAL_EXTI_D1_ClearFlag(EXTI_LINE19);
  HAL_EXTI_D2_ClearFlag(EXTI_LINE19);
  if ((RTC->ISR & RTC_ISR_WUTF) != 0U)
  {
    RTC->ISR = (~(RTC_ISR_WUTF | RTC_ISR_INIT)) |
               (RTC->ISR & RTC_ISR_INIT);
    sleep_report_due = true;
  }
}

void PrototypePower_OnUserButton(void)
{
  if (sleep_mode_active)
  {
    sleep_reset_requested = true;
  }
}

void PrototypePower_RequestSleep(void)
{
  sleep_requested = true;
}

void PrototypePower_Service(void (*stop_radio_audio)(void))
{
  if (sleep_requested && (stop_radio_audio != NULL))
  {
    PrototypeSleepRun(stop_radio_audio);
  }
}
