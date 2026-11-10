/* Application for wiring-table V1.2. Included once by main.c. */
volatile BringupDiagnostics bringup_diag = {.magic=0x52475612u, .version=0x00010200u, .modem_csq=-1};
UART_HandleTypeDef huart3, huart5;
static uint8_t modem_byte, mp3_byte;
static uint8_t modem_ring[1024], mp3_ring[128];
static volatile uint16_t modem_head, modem_tail, mp3_head, mp3_tail;
static uint8_t mp3_frame[10], mp3_index;
static char modem_line[160];
static uint16_t modem_index;
static uint8_t modem_discard;
static uint32_t modem_deadline, modem_next, mp3_next;
static uint8_t modem_query_index, modem_waiting, mp3_query_index;
static const char *const modem_queries[] = {"AT\r\n", "ATI\r\n", "AT+CPIN?\r\n", "AT+CSQ\r\n"};

void USART3_IRQHandler(void) { HAL_UART_IRQHandler(&huart3); }
void UART5_IRQHandler(void) { HAL_UART_IRQHandler(&huart5); }

void Bringup_RxComplete(UART_HandleTypeDef *uart) {
    if(uart==&huart3) {
        uint16_t next=(uint16_t)((modem_head+1u)%sizeof modem_ring);
        bringup_diag.modem_bytes++;
        if(next!=modem_tail) {modem_ring[modem_head]=modem_byte; modem_head=next;}
        else bringup_diag.modem_overruns++;
        (void)HAL_UART_Receive_IT(uart,&modem_byte,1);
    } else if(uart==&huart5) {
        uint16_t next=(uint16_t)((mp3_head+1u)%sizeof mp3_ring);
        bringup_diag.mp3_bytes++;
        if(next!=mp3_tail) {mp3_ring[mp3_head]=mp3_byte; mp3_head=next;}
        else bringup_diag.mp3_overruns++;
        (void)HAL_UART_Receive_IT(uart,&mp3_byte,1);
    }
}
void Bringup_RxError(UART_HandleTypeDef *uart) {
    if(uart==&huart3) {bringup_diag.modem_errors++; (void)HAL_UART_Receive_IT(uart,&modem_byte,1);}
    else if(uart==&huart5) {bringup_diag.mp3_errors++; (void)HAL_UART_Receive_IT(uart,&mp3_byte,1);}
}

static void Aux_UART_Init(void) {
    GPIO_InitTypeDef gpio={0};
    __HAL_RCC_GPIOD_CLK_ENABLE(); __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE(); __HAL_RCC_UART5_CLK_ENABLE();
    gpio.Pin=GPIO_PIN_8|GPIO_PIN_9; gpio.Mode=GPIO_MODE_AF_PP;
    gpio.Pull=GPIO_PULLUP; gpio.Speed=GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate=GPIO_AF7_USART3; HAL_GPIO_Init(GPIOD,&gpio);
    gpio.Pin=GPIO_PIN_12; gpio.Alternate=GPIO_AF8_UART5; HAL_GPIO_Init(GPIOC,&gpio);
    gpio.Pin=GPIO_PIN_2; HAL_GPIO_Init(GPIOD,&gpio);
    huart3.Instance=USART3; huart3.Init.BaudRate=115200;
    huart3.Init.WordLength=UART_WORDLENGTH_8B; huart3.Init.StopBits=UART_STOPBITS_1;
    huart3.Init.Parity=UART_PARITY_NONE; huart3.Init.Mode=UART_MODE_TX_RX;
    huart3.Init.HwFlowCtl=UART_HWCONTROL_NONE; huart3.Init.OverSampling=UART_OVERSAMPLING_16;
    if(HAL_UART_Init(&huart3)!=HAL_OK) Error_Handler();
    huart5.Init=huart3.Init; huart5.Instance=UART5; huart5.Init.BaudRate=9600;
    if(HAL_UART_Init(&huart5)!=HAL_OK) Error_Handler();
    HAL_NVIC_SetPriority(USART3_IRQn,2,0); HAL_NVIC_EnableIRQ(USART3_IRQn);
    HAL_NVIC_SetPriority(UART5_IRQn,2,0); HAL_NVIC_EnableIRQ(UART5_IRQn);
    (void)HAL_UART_Receive_IT(&huart3,&modem_byte,1);
    (void)HAL_UART_Receive_IT(&huart5,&mp3_byte,1);
}

static void Pump_GPS(void) {
    rg_gps_fix_t fix;
    static uint32_t last_usb_nmea;
    bringup_diag.gps_bytes=gnss_rx_bytes;
    bringup_diag.gps_errors=gnss_rx_errors;
    bringup_diag.gps_overruns=gnss_rx_overruns;
    while(GNSS_Task(gnss_line,sizeof gnss_line)) {
        bringup_diag.gps_lines++;
        memcpy((void *)bringup_diag.gps_last_line,gnss_line,sizeof gnss_line);
        if(rg_gps_parse_rmc(gnss_line,HAL_GetTick(),&fix)) {
            bringup_diag.gps_rmc++; bringup_diag.gps_last_ms=HAL_GetTick();
            bringup_diag.gps_valid=fix.gps_valid;
            bringup_diag.gps_speed_milli_kph=fix.speed_milli_kph;
            bringup_diag.latitude_e7=fix.latitude_e7; bringup_diag.longitude_e7=fix.longitude_e7;
            if((uint32_t)(HAL_GetTick()-last_usb_nmea)>=3000u) {
                last_usb_nmea=HAL_GetTick();
                USB_Stream_Print("GPS_NMEA "); USB_Stream_Print(gnss_line); USB_Stream_Print("\r\n");
            }
        }
    }
    if(bringup_diag.gps_valid && (uint32_t)(HAL_GetTick()-bringup_diag.gps_last_ms)>15000u) {
        bringup_diag.gps_valid=0; bringup_diag.gps_speed_milli_kph=0;
        bringup_diag.latitude_e7=RG_GPS_COORD_INVALID; bringup_diag.longitude_e7=RG_GPS_COORD_INVALID;
    }
}

static void Pump_Aux(void) {
    while(modem_tail!=modem_head) {
        char c=(char)modem_ring[modem_tail]; modem_tail=(uint16_t)((modem_tail+1u)%sizeof modem_ring);
        if(c=='\r') continue;
        if(c=='\n') {
            if(!modem_discard && modem_index) {
                modem_line[modem_index]=0;
                memcpy((void *)bringup_diag.modem_last_line,modem_line,modem_index+1u);
                if(!strcmp(modem_line,"OK")) {
                    bringup_diag.modem_ok++;
                    if(modem_waiting) {modem_waiting=0; modem_next=HAL_GetTick()+300u;}
                } else if(!strcmp(modem_line,"ERROR") || !strncmp(modem_line,"+CME ERROR",10)) {
                    bringup_diag.modem_error_lines++;
                    if(modem_waiting) {modem_waiting=0; modem_next=HAL_GetTick()+300u;}
                }
                if(!strcmp(modem_line,"+CPIN: READY")) bringup_diag.modem_sim_ready=1;
                int csq,ber;
                if(sscanf(modem_line,"+CSQ: %d,%d",&csq,&ber)==2) bringup_diag.modem_csq=csq;
            }
            modem_index=0; modem_discard=0;
        } else if(!modem_discard) {
            if(modem_index<sizeof modem_line-1u) modem_line[modem_index++]=c;
            else {modem_discard=1; modem_index=0;}
        }
    }
    while(mp3_tail!=mp3_head) {
        uint8_t c=mp3_ring[mp3_tail]; mp3_tail=(uint16_t)((mp3_tail+1u)%sizeof mp3_ring);
        if(!mp3_index && c!=0x7e) continue;
        mp3_frame[mp3_index++]=c;
        if(mp3_index==10u) {
            if(dfplayer_valid(mp3_frame)) {
                bringup_diag.mp3_valid_frames++;
                bringup_diag.mp3_last_command=mp3_frame[3];
                bringup_diag.mp3_last_value=((uint32_t)mp3_frame[5]<<8)|mp3_frame[6];
            } else bringup_diag.mp3_bad_frames++;
            mp3_index=0;
        }
    }
    uint32_t now=HAL_GetTick();
    if(modem_waiting && (int32_t)(now-modem_deadline)>=0) {
        modem_waiting=0; bringup_diag.modem_timeouts++; modem_next=now+300u;
    }
    if(!modem_waiting && modem_query_index<4u && (int32_t)(now-modem_next)>=0) {
        const char *q=modem_queries[modem_query_index++];
        if(HAL_UART_Transmit(&huart3,(uint8_t *)q,(uint16_t)strlen(q),50)==HAL_OK) {
            bringup_diag.modem_queries_sent++; modem_waiting=1; modem_deadline=now+3000u;
        } else {bringup_diag.modem_errors++; modem_next=now+300u;}
    }
    if(mp3_query_index<2u && (int32_t)(now-mp3_next)>=0) {
        uint8_t frame[10]; dfplayer_query(mp3_query_index?0x48:0x43,frame);
        if(HAL_UART_Transmit(&huart5,frame,10,50)==HAL_OK) bringup_diag.mp3_queries_sent++;
        else bringup_diag.mp3_errors++;
        mp3_query_index++; mp3_next=now+1500u;
    }
}

int main(void) {
    HAL_Init(); SystemClock_Config();
    MX_GPIO_Init(); MX_DMA_Init(); MX_USB_DEVICE_Init();
    MX_I2C1_Init(); MX_I2C2_Init(); MX_DCMI_Init(); MX_UART4_UART_Init();
    bringup_diag.stage=1;
    HAL_Delay(300);
    if(MPU6050_Init(&hi2c1)==HAL_OK && MPU6050_ReadWhoAmI(&mpu_who_am_i)==HAL_OK) {
        mpu_ready=1; bringup_diag.imu_ready=1; bringup_diag.imu_id=mpu_who_am_i;
    }
    bringup_diag.stage=2;
    HAL_StatusTypeDef status=OV2640_Begin(&camera,&hi2c2,&hdcmi,&hdma_dcmi);
    bringup_diag.camera_ready=(status==HAL_OK);
    bringup_diag.camera_pid=((uint32_t)camera.pidh<<8)|camera.pidl;
    bringup_diag.camera_last_status=status;
    bringup_diag.stage=3;
    GNSS_Init(&huart4); Aux_UART_Init();
    modem_next=HAL_GetTick()+1000u; mp3_next=HAL_GetTick()+1500u;
    USB_Stream_Print("\r\nRG_BRINGUP_V1.2 | GPS PA0/PA1 | 4G PD8/PD9 | MP3 PC12/PD2 | NO CELLULAR UPLOAD\r\n");
    uint32_t imu_next=0, camera_next=HAL_GetTick()+3000u, log_next=0;
    bringup_diag.stage=4;
    while(1) {
        uint32_t now=HAL_GetTick(); bringup_diag.heartbeat_ms=now;
        Pump_GPS(); Pump_Aux();
        if(mpu_ready && (int32_t)(now-imu_next)>=0) {
            imu_next=now+100u;
            if(MPU6050_Read(&mpu_data)==HAL_OK) bringup_diag.imu_reads++;
            else bringup_diag.imu_errors++;
        }
        if(bringup_diag.camera_ready && (int32_t)(now-camera_next)>=0) {
            camera_next=now+30000u; frame_number++; bringup_diag.camera_attempts++;
            status=OV2640_CaptureJPEG(&camera,&camera_frame);
            bringup_diag.camera_last_status=status; bringup_diag.camera_bytes=camera_frame.length;
            if(status==HAL_OK) {
                bringup_diag.camera_ok++;
                if(USB_Stream_Connected()) {
                    snprintf(usb_msg,sizeof usb_msg,"JPEG_TX_BEGIN LEN=%lu\r\n",(unsigned long)camera_frame.length);
                    USB_Stream_Print(usb_msg);
                    if(USB_Stream_SendBytes(camera_frame.data,camera_frame.length)==HAL_OK) {
                        USB_Stream_Print("\r\nJPEG_TX_END\r\n"); bringup_diag.usb_image_ok++;
                    } else {USB_Stream_Print("\r\nJPEG_TX_ERROR\r\n");bringup_diag.usb_image_errors++;}
                }
            } else bringup_diag.camera_errors++;
        }
        if((int32_t)(now-log_next)>=0) {
            log_next=now+3000u;
            snprintf(usb_msg,sizeof usb_msg,"RG_STATUS GPS_BYTES=%lu RMC=%lu FIX=%lu IMU_READS=%lu CAM_OK=%lu CAM_ERR=%lu AT_OK=%lu MP3_RX=%lu\r\n",
                (unsigned long)bringup_diag.gps_bytes,(unsigned long)bringup_diag.gps_rmc,
                (unsigned long)bringup_diag.gps_valid,(unsigned long)bringup_diag.imu_reads,
                (unsigned long)bringup_diag.camera_ok,(unsigned long)bringup_diag.camera_errors,
                (unsigned long)bringup_diag.modem_ok,(unsigned long)bringup_diag.mp3_valid_frames);
            USB_Stream_Print(usb_msg);
        }
        HAL_Delay(1);
    }
}
