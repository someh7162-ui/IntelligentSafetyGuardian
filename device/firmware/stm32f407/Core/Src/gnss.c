#include "gnss.h"

#include <string.h>

#define GNSS_RX_RING_SIZE 4096U

static UART_HandleTypeDef *s_gnss_uart = NULL;
static char s_line_buffer[GNSS_LINE_BUFFER_SIZE];
static uint16_t s_line_index = 0U;
static uint8_t s_rx_byte;
static uint8_t s_rx_ring[GNSS_RX_RING_SIZE];
static volatile uint16_t s_rx_head = 0U;
static volatile uint16_t s_rx_tail = 0U;

void GNSS_Init(UART_HandleTypeDef *huart)
{
    s_gnss_uart = huart;
    s_line_index = 0U;
    s_rx_head = 0U;
    s_rx_tail = 0U;
    memset(s_line_buffer, 0, sizeof(s_line_buffer));
    HAL_NVIC_SetPriority(USART2_IRQn, 2U, 0U);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    (void)HAL_UART_Receive_IT(s_gnss_uart, &s_rx_byte, 1U);
}

uint8_t GNSS_Task(char *line_out, uint16_t line_out_size)
{
    uint8_t byte;

    if ((s_gnss_uart == NULL) ||
        (line_out == NULL) ||
        (line_out_size < 2U))
    {
        return 0U;
    }

    while (s_rx_tail != s_rx_head)
    {
        byte = s_rx_ring[s_rx_tail];
        s_rx_tail = (uint16_t)((s_rx_tail + 1U) % GNSS_RX_RING_SIZE);

    /* NMEA lines normally end with CRLF. Ignore CR. */
        if (byte == '\r') continue;

    /* A new '$' is a safe resynchronization point. */
        if (byte == '$') s_line_index = 0U;

        if (byte == '\n')
        {
            uint16_t copy_len;

            if (s_line_index == 0U) continue;

            s_line_buffer[s_line_index] = '\0';

            copy_len = s_line_index;
            if (copy_len >= line_out_size)
                copy_len = (uint16_t)(line_out_size - 1U);

            memcpy(line_out, s_line_buffer, copy_len);
            line_out[copy_len] = '\0';

            s_line_index = 0U;
            return 1U;
        }

        if (s_line_index < (GNSS_LINE_BUFFER_SIZE - 1U))
            s_line_buffer[s_line_index++] = (char)byte;
        else
            s_line_index = 0U;
    }

    return 0U;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    uint16_t next;
    if (huart != s_gnss_uart) return;
    next = (uint16_t)((s_rx_head + 1U) % GNSS_RX_RING_SIZE);
    if (next != s_rx_tail)
    {
        s_rx_ring[s_rx_head] = s_rx_byte;
        s_rx_head = next;
    }
    (void)HAL_UART_Receive_IT(s_gnss_uart, &s_rx_byte, 1U);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart == s_gnss_uart)
        (void)HAL_UART_Receive_IT(s_gnss_uart, &s_rx_byte, 1U);
}
