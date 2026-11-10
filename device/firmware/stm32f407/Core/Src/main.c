/* RiderGuard hardware bring-up, wiring table V1.2, 2026-11-04. */
#include "main.h"
#include "usb_device.h"
#include "usb_stream.h"
#include "gnss.h"
#include "ov2640.h"
#include "mpu6050.h"
#include "bringup_v12.h"
#include "dfplayer_frame.h"
#include "rg_gps.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

DCMI_HandleTypeDef hdcmi;
DMA_HandleTypeDef hdma_dcmi;
I2C_HandleTypeDef hi2c1, hi2c2;
UART_HandleTypeDef huart4;
OV2640_Handle_t camera;
OV2640_JPEGFrame_t camera_frame;
OV2640_Exposure_t camera_exposure;
MPU6050_Data_t mpu_data;
uint8_t mpu_who_am_i, mpu_ready;
char usb_msg[256], gnss_line[GNSS_LINE_BUFFER_SIZE];
uint32_t frame_number;
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_I2C1_Init(void);
static void MX_I2C2_Init(void);
static void MX_DCMI_Init(void);
static void MX_UART4_UART_Init(void);
#ifdef RG_ENTERPRISE_BUILD
#include "enterprise_app.inc"
#else
#include "bringup_v12.inc"
#endif

void SystemClock_Config(void)

{

  RCC_OscInitTypeDef RCC_OscInitStruct = {0};

  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};



  /** Configure the main internal regulator output voltage

  */

  __HAL_RCC_PWR_CLK_ENABLE();

  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);



  /** Initializes the RCC Oscillators according to the specified parameters

  * in the RCC_OscInitTypeDef structure.

  */

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;

  RCC_OscInitStruct.HSEState = RCC_HSE_ON;

  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;

  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;

  RCC_OscInitStruct.PLL.PLLM = 8;

  RCC_OscInitStruct.PLL.PLLN = 336;

  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;

  RCC_OscInitStruct.PLL.PLLQ = 7;

  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)

  {

    Error_Handler();

  }



  /** Initializes the CPU, AHB and APB buses clocks

  */

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK

                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;

  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;

  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;

  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;

  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;



  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)

  {

    Error_Handler();

  }

}



/**

  * @brief DCMI Initialization Function

  * @param None

  * @retval None

  */

static void MX_DCMI_Init(void)

{



  /* USER CODE BEGIN DCMI_Init 0 */



  /* USER CODE END DCMI_Init 0 */



  /* USER CODE BEGIN DCMI_Init 1 */



  /* USER CODE END DCMI_Init 1 */

  hdcmi.Instance = DCMI;

  hdcmi.Init.SynchroMode = DCMI_SYNCHRO_HARDWARE;

  hdcmi.Init.PCKPolarity = DCMI_PCKPOLARITY_RISING;

  hdcmi.Init.VSPolarity = DCMI_VSPOLARITY_LOW;

  hdcmi.Init.HSPolarity = DCMI_HSPOLARITY_LOW;

  hdcmi.Init.CaptureRate = DCMI_CR_ALL_FRAME;

  hdcmi.Init.ExtendedDataMode = DCMI_EXTEND_DATA_8B;

  hdcmi.Init.JPEGMode = DCMI_JPEG_ENABLE;

  if (HAL_DCMI_Init(&hdcmi) != HAL_OK)

  {

    Error_Handler();

  }

  /* USER CODE BEGIN DCMI_Init 2 */



  /* USER CODE END DCMI_Init 2 */



}



/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**

  * @brief I2C2 Initialization Function

  * @param None

  * @retval None

  */

static void MX_I2C2_Init(void)

{



  /* USER CODE BEGIN I2C2_Init 0 */



  /* USER CODE END I2C2_Init 0 */



  /* USER CODE BEGIN I2C2_Init 1 */



  /* USER CODE END I2C2_Init 1 */

  hi2c2.Instance = I2C2;

  hi2c2.Init.ClockSpeed = 50000;

  hi2c2.Init.DutyCycle = I2C_DUTYCYCLE_2;

  hi2c2.Init.OwnAddress1 = 0;

  hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;

  hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;

  hi2c2.Init.OwnAddress2 = 0;

  hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;

  hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

  if (HAL_I2C_Init(&hi2c2) != HAL_OK)

  {

    Error_Handler();

  }

  /* USER CODE BEGIN I2C2_Init 2 */



  /* USER CODE END I2C2_Init 2 */



}



/**
  * @brief UART4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_UART4_UART_Init(void)
{
  /* USER CODE BEGIN UART4_Init 0 */

  /* USER CODE END UART4_Init 0 */

  /* USER CODE BEGIN UART4_Init 1 */

  /* USER CODE END UART4_Init 1 */

  huart4.Instance = UART4;
  huart4.Init.BaudRate = 9600;
  huart4.Init.WordLength = UART_WORDLENGTH_8B;
  huart4.Init.StopBits = UART_STOPBITS_1;
  huart4.Init.Parity = UART_PARITY_NONE;
  huart4.Init.Mode = UART_MODE_TX_RX;
  huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart4.Init.OverSampling = UART_OVERSAMPLING_16;

  if (HAL_UART_Init(&huart4) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN UART4_Init 2 */

  /* USER CODE END UART4_Init 2 */
}


/**

  * Enable DMA controller clock

  */

static void MX_DMA_Init(void)

{



  /* DMA controller clock enable */

  __HAL_RCC_DMA2_CLK_ENABLE();



  /* DMA interrupt init */

  /* DMA2_Stream1_IRQn interrupt configuration */

  HAL_NVIC_SetPriority(DMA2_Stream1_IRQn, 0, 0);

  HAL_NVIC_EnableIRQ(DMA2_Stream1_IRQn);



}



/**

  * @brief GPIO Initialization Function

  * @param None

  * @retval None

  */

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef gpio={0};
  __HAL_RCC_GPIOA_CLK_ENABLE(); __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE(); __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE(); __HAL_RCC_GPIOH_CLK_ENABLE();
  /* PA1 belongs exclusively to UART4 RX. No LED output on this pin. */
  HAL_GPIO_WritePin(GPIOC,CAM_RST_Pin,GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOC,CAM_PWDN_Pin,GPIO_PIN_RESET);
  gpio.Pin=CAM_RST_Pin|CAM_PWDN_Pin; gpio.Mode=GPIO_MODE_OUTPUT_PP;
  gpio.Pull=GPIO_NOPULL; gpio.Speed=GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC,&gpio);
}



/* USER CODE BEGIN 4 */



/* USER CODE END 4 */



/**

  * @brief  This function is executed in case of error occurrence.

  * @retval None

  */

void Error_Handler(void)

{

  /* USER CODE BEGIN Error_Handler_Debug */



    __disable_irq();



    while (1)

    {

    }



  /* USER CODE END Error_Handler_Debug */

}

#ifdef USE_FULL_ASSERT

/**

  * @brief  Reports the name of the source file and the source line number

  *         where the assert_param error has occurred.

  * @param  file: pointer to the source file name

  * @param  line: assert_param error line source number

  * @retval None

  */

void assert_failed(uint8_t *file, uint32_t line)

{

  /* USER CODE BEGIN 6 */



  /* USER CODE END 6 */

}

#endif /* USE_FULL_ASSERT */
