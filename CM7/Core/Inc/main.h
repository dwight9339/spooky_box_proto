/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"

#include "stm32h7xx_nucleo.h"
#include <stdio.h>

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

extern SD_HandleTypeDef hsd1;
extern DFSDM_Filter_HandleTypeDef hdfsdm1_filter0;
extern DMA_HandleTypeDef hdma_dfsdm1_flt0;

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define SAI_CODEC_MCLK_A_Pin GPIO_PIN_2
#define SAI_CODEC_MCLK_A_GPIO_Port GPIOE
#define SAI_CODEC_SD_B_Pin GPIO_PIN_3
#define SAI_CODEC_SD_B_GPIO_Port GPIOE
#define SAI_CODEC_FS_A_Pin GPIO_PIN_4
#define SAI_CODEC_FS_A_GPIO_Port GPIOE
#define SAI_CODEC_SCK_A_Pin GPIO_PIN_5
#define SAI_CODEC_SCK_A_GPIO_Port GPIOE
#define SAI_CODEC_SD_A_Pin GPIO_PIN_6
#define SAI_CODEC_SD_A_GPIO_Port GPIOE
#define VOL_POT_INPUT_Pin GPIO_PIN_10
#define VOL_POT_INPUT_GPIO_Port GPIOF
#define MIC_CKOUT_Pin GPIO_PIN_2
#define MIC_CKOUT_GPIO_Port GPIOC
#define MIC_DATIN_Pin GPIO_PIN_3
#define MIC_DATIN_GPIO_Port GPIOC
#define SD_CARD_DETECT_Pin GPIO_PIN_3
#define SD_CARD_DETECT_GPIO_Port GPIOA
#define SPI_DISP_MOSI_Pin GPIO_PIN_7
#define SPI_DISP_MOSI_GPIO_Port GPIOA
#define SAI_RADIO_SD_Pin GPIO_PIN_11
#define SAI_RADIO_SD_GPIO_Port GPIOD
#define SAI_RADIO_FS_Pin GPIO_PIN_12
#define SAI_RADIO_FS_GPIO_Port GPIOD
#define SAI_RADIO_SCK_Pin GPIO_PIN_13
#define SAI_RADIO_SCK_GPIO_Port GPIOD
#define RADIO_RCLK_Pin GPIO_PIN_8
#define RADIO_RCLK_GPIO_Port GPIOA
#define SPI_DISP_MISO_Pin GPIO_PIN_12
#define SPI_DISP_MISO_GPIO_Port GPIOG
#define SPI_DISP_SCK_Pin GPIO_PIN_13
#define SPI_DISP_SCK_GPIO_Port GPIOG

/* USER CODE BEGIN Private defines */

/* Minimal backplane bring-up pins owned by CM7.  Keep these assignments in
 * sync with the backplane schematic if the CubeMX project is regenerated. */
#define RADIO_RST_Pin              GPIO_PIN_10
#define RADIO_RST_GPIO_Port        GPIOA
#define LINE_IN_JACK_DETECT_Pin     GPIO_PIN_5
#define LINE_IN_JACK_DETECT_GPIO_Port GPIOA
#define HEADPHONE_JACK_DETECT_Pin  GPIO_PIN_0
#define HEADPHONE_JACK_DETECT_GPIO_Port GPIOE
#define RADIO_GPIO_1_Pin           GPIO_PIN_5
#define RADIO_GPIO_1_GPIO_Port     GPIOC
#define RADIO_SW_SWITCH_Pin        GPIO_PIN_6
#define RADIO_SW_SWITCH_GPIO_Port  GPIOC
#define DEBUG_UART_RX_Pin          GPIO_PIN_7
#define DEBUG_UART_RX_GPIO_Port    GPIOE
#define DEBUG_UART_TX_Pin          GPIO_PIN_8
#define DEBUG_UART_TX_GPIO_Port    GPIOE
#define RADIO_INT_Pin              GPIO_PIN_15
#define RADIO_INT_GPIO_Port        GPIOE
#define MAG_INT_Pin                GPIO_PIN_7
#define MAG_INT_GPIO_Port          GPIOC
#define REG_3V3_EN_Pin             GPIO_PIN_12
#define REG_3V3_EN_GPIO_Port       GPIOB
#define LED_MATRIX_EN_Pin          GPIO_PIN_5
#define LED_MATRIX_EN_GPIO_Port    GPIOB
#define DISP_RST_Pin               GPIO_PIN_15
#define DISP_RST_GPIO_Port         GPIOA
#define DISP_CS_Pin                GPIO_PIN_6
#define DISP_CS_GPIO_Port          GPIOG
#define DISP_DC_Pin                GPIO_PIN_7
#define DISP_DC_GPIO_Port          GPIOG
#define AMP_SD_Pin                 GPIO_PIN_8
#define AMP_SD_GPIO_Port           GPIOG
#define SD_CARD_IS_PRESENT() \
  (HAL_GPIO_ReadPin(SD_CARD_DETECT_GPIO_Port, SD_CARD_DETECT_Pin) == \
   GPIO_PIN_RESET)

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
