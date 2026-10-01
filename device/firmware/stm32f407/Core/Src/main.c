/* USER CODE BEGIN Header */

/**

  ******************************************************************************

  * @file           : main.c

  * @brief          : IntelligentSafetyGuardian modular hardware application

  ******************************************************************************

  */

/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/

#include "main.h"

#include "usb_device.h"



/* Private includes ----------------------------------------------------------*/

/* USER CODE BEGIN Includes */
#include "ov2640.h"
#include "mpu6050.h"
#include "usb_stream.h"
#include "gnss.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
/* USER CODE END Includes */



/* Private typedef -----------------------------------------------------------*/

/* USER CODE BEGIN PTD */
/* Drivers own their private types. */
/* USER CODE END PTD */



/* Private define ------------------------------------------------------------*/

/* USER CODE BEGIN PD */
#define CONTINUOUS_CAPTURE_DELAY_MS 1000U
#define GNSS_TEST_MODE 0U /* 1: print raw NMEA only; 0: normal camera firmware */
/* USER CODE END PD */



/* Private macro -------------------------------------------------------------*/

/* USER CODE BEGIN PM */



/* USER CODE END PM */



/* Private variables ---------------------------------------------------------*/

DCMI_HandleTypeDef hdcmi;

DMA_HandleTypeDef hdma_dcmi;



I2C_HandleTypeDef hi2c1;

I2C_HandleTypeDef hi2c2;

UART_HandleTypeDef huart2;



/* USER CODE BEGIN PV */
OV2640_Handle_t camera;
OV2640_JPEGFrame_t camera_frame;
OV2640_Exposure_t camera_exposure;

MPU6050_Data_t mpu_data;
uint8_t mpu_who_am_i = 0U;
uint8_t mpu_ready = 0U;

char usb_msg[256];
uint32_t frame_number = 0U;
char gnss_line[GNSS_LINE_BUFFER_SIZE];
/* USER CODE END PV */



/* Private function prototypes -----------------------------------------------*/

void SystemClock_Config(void);

static void MX_GPIO_Init(void);

static void MX_DMA_Init(void);

static void MX_I2C2_Init(void);

static void MX_I2C1_Init(void);

static void MX_DCMI_Init(void);

static void MX_USART2_UART_Init(void);

/* USER CODE BEGIN PFP */
static void GNSS_Pump(void);
/* USER CODE END PFP */



/* Private user code ---------------------------------------------------------*/

/* USER CODE BEGIN 0 */
static void GNSS_Pump(void)
{
  while (GNSS_Task(gnss_line, sizeof(gnss_line)))
  {
    if (gnss_line[0] == '$' && strlen(gnss_line) >= 7U &&
        (memcmp(&gnss_line[3], "RMC", 3U) == 0 ||
         memcmp(&gnss_line[3], "GGA", 3U) == 0))
    {
      USB_Stream_Print("GPS_NMEA ");
      USB_Stream_Print(gnss_line);
      USB_Stream_Print("\r\n");
    }
  }
}
/* USER CODE END 0 */



/**

  * @brief  The application entry point.

  * @retval int

  */

int main(void)

{



  /* USER CODE BEGIN 1 */
HAL_StatusTypeDef status;
/* USER CODE END 1 */



  /* MCU Configuration--------------------------------------------------------*/



  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */

  HAL_Init();



  /* USER CODE BEGIN Init */



  /* USER CODE END Init */



  /* Configure the system clock */

  SystemClock_Config();



  /* USER CODE BEGIN SysInit */



  /* USER CODE END SysInit */



  /* Initialize all configured peripherals */

  MX_GPIO_Init();

  MX_DMA_Init();

  MX_USB_DEVICE_Init();

  MX_I2C2_Init();

  MX_DCMI_Init();

  MX_I2C1_Init();

  MX_USART2_UART_Init();

  /* USER CODE BEGIN 2 */

  /* Give Windows enough time to enumerate USB CDC. */
  HAL_Delay(8000);

  USB_Stream_Print("\r\n");
  USB_Stream_Print("===============================================\r\n");
  USB_Stream_Print("IntelligentSafetyGuardian hardware bring-up\r\n");
  USB_Stream_Print("===============================================\r\n");
  GNSS_Init(&huart2);

#if GNSS_TEST_MODE
  /*
   * GPS bring-up mode.
   * Camera and MPU application code below are intentionally not reached
   * while this test loop is active, so USB output contains clean NMEA text.
   */
  USB_Stream_Print("GNSS TEST MODE\r\n");
  USB_Stream_Print("ATGM336H | USART2 | PA2=TX PA3=RX | 9600 8N1\r\n");
  USB_Stream_Print("Waiting for NMEA sentences...\r\n");
  USB_Stream_Print("-----------------------------------------------\r\n");

  while (1)
  {
    if (GNSS_Task(gnss_line, sizeof(gnss_line)))
    {
      USB_Stream_Print(gnss_line);
      USB_Stream_Print("\r\n");
    }
  }
#endif

  /* MPU6050 lives on I2C1 (PB8/PB9). */
  status = MPU6050_Init(&hi2c1);
  if (status == HAL_OK)
  {
    if (MPU6050_ReadWhoAmI(&mpu_who_am_i) == HAL_OK)
    {
      snprintf(usb_msg, sizeof(usb_msg),
               "MPU6050 OK | WHO_AM_I=0x%02X\r\n",
               mpu_who_am_i);
      USB_Stream_Print(usb_msg);
      mpu_ready = 1U;
    }
  }
  else
  {
    snprintf(usb_msg, sizeof(usb_msg),
             "MPU6050 INIT FAILED | HAL=%d\r\n", status);
    USB_Stream_Print(usb_msg);
  }

  /* OV2640 lives on I2C2 + DCMI + DMA2 Stream1. */
  USB_Stream_Print("Starting OV2640...\r\n");
  status = OV2640_Begin(&camera, &hi2c2, &hdcmi, &hdma_dcmi);

  snprintf(usb_msg, sizeof(usb_msg),
           "OV2640 ID | MID=%02X%02X PID=%02X%02X\r\n",
           camera.midh, camera.midl, camera.pidh, camera.pidl);
  USB_Stream_Print(usb_msg);

  if (status == HAL_OK)
  {
    USB_Stream_Print("OV2640 JPEG QVGA READY.\r\n");

    if (camera.warmup_status != HAL_OK)
    {
      USB_Stream_Print("WARNING: OV2640 warm-up timed out; capture will still run.\r\n");
    }

    if (OV2640_ReadExposure(&camera, &camera_exposure) == HAL_OK)
    {
      snprintf(usb_msg, sizeof(usb_msg),
               "Exposure | COM8=%02X COM9=%02X GAIN=%02X AEC=%02X\r\n",
               camera_exposure.com8,
               camera_exposure.com9,
               camera_exposure.gain,
               camera_exposure.aec);
      USB_Stream_Print(usb_msg);
    }
  }
  else
  {
    snprintf(usb_msg, sizeof(usb_msg),
             "OV2640 INIT FAILED | HAL=%d | table=%u reg=%02X val=%02X\r\n",
             status,
             camera.table_fail_index,
             camera.table_fail_reg,
             camera.table_fail_val);
    USB_Stream_Print(usb_msg);
  }

/* USER CODE END 2 */



  /* Infinite loop */

  /* USER CODE BEGIN WHILE */
  while (1)
  {
    HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);

    if (OV2640_IsReady(&camera))
    {
      frame_number++;
      status = OV2640_CaptureJPEG(&camera, &camera_frame);

      if (status == HAL_OK)
      {
        snprintf(usb_msg, sizeof(usb_msg),
                 "FRAME %lu: JPEG OK | DMA=%lu | JPEG=%lu | ERR=0x%08lX\r\n",
                 (unsigned long)frame_number,
                 (unsigned long)camera_frame.captured_bytes,
                 (unsigned long)camera_frame.length,
                 (unsigned long)camera_frame.dcmi_error);
        USB_Stream_Print(usb_msg);

        snprintf(usb_msg, sizeof(usb_msg),
                 "JPEG_TX_BEGIN LEN=%lu\r\n",
                 (unsigned long)camera_frame.length);
        USB_Stream_Print(usb_msg);
        HAL_Delay(20);

        status = USB_Stream_SendBytes(camera_frame.data, camera_frame.length);

        HAL_Delay(20);
        if (status == HAL_OK)
        {
          USB_Stream_Print("\r\nJPEG_TX_END\r\n");
        }
        else
        {
          USB_Stream_Print("\r\nJPEG_TX_ERROR\r\n");
        }
      }
      else
      {
        snprintf(usb_msg, sizeof(usb_msg),
                 "FRAME %lu: CAPTURE FAILED | HAL=%d | DMA=%lu | ERR=0x%08lX\r\n",
                 (unsigned long)frame_number,
                 status,
                 (unsigned long)camera_frame.captured_bytes,
                 (unsigned long)camera_frame.dcmi_error);
        USB_Stream_Print(usb_msg);
      }
    }

    /* For now MPU6050 is initialized but not streamed continuously so JPEG framing stays clean. */
    (void)mpu_ready;
    (void)mpu_data;

    {
      uint32_t wait_start = HAL_GetTick();
      do
      {
        GNSS_Pump();
        HAL_Delay(10);
      } while ((HAL_GetTick() - wait_start) < CONTINUOUS_CAPTURE_DELAY_MS);
    }
/* USER CODE END WHILE */



    /* USER CODE BEGIN 3 */

    }



  /* USER CODE END 3 */

}



/**

  * @brief System Clock Configuration

  * @retval None

  */

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
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{
  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */

  huart2.Instance = USART2;
  huart2.Init.BaudRate = 9600;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;

  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */
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

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* USER CODE BEGIN MX_GPIO_Init_1 */



  /* USER CODE END MX_GPIO_Init_1 */



  /* GPIO Ports Clock Enable */

  __HAL_RCC_GPIOE_CLK_ENABLE();

  __HAL_RCC_GPIOH_CLK_ENABLE();

  __HAL_RCC_GPIOA_CLK_ENABLE();

  __HAL_RCC_GPIOB_CLK_ENABLE();

  __HAL_RCC_GPIOC_CLK_ENABLE();



  /*Configure GPIO pin Output Level */

  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);



  /*Configure GPIO pin Output Level */

  HAL_GPIO_WritePin(CAM_RST_GPIO_Port, CAM_RST_Pin, GPIO_PIN_SET);



  /*Configure GPIO pin Output Level */

  HAL_GPIO_WritePin(CAM_PWDN_GPIO_Port, CAM_PWDN_Pin, GPIO_PIN_RESET);



  /*Configure GPIO pin : LED_Pin */

  GPIO_InitStruct.Pin = LED_Pin;

  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;

  GPIO_InitStruct.Pull = GPIO_NOPULL;

  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

  HAL_GPIO_Init(LED_GPIO_Port, &GPIO_InitStruct);



  /*Configure GPIO pins : CAM_RST_Pin CAM_PWDN_Pin */

  GPIO_InitStruct.Pin = CAM_RST_Pin|CAM_PWDN_Pin;

  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;

  GPIO_InitStruct.Pull = GPIO_NOPULL;

  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);



  /* USER CODE BEGIN MX_GPIO_Init_2 */



  /* USER CODE END MX_GPIO_Init_2 */

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
