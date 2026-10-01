#ifndef __GNSS_H
#define __GNSS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

#define GNSS_LINE_BUFFER_SIZE 128U

/*
 * Initialize the GNSS driver.
 * For this project:
 *   ATGM336H TX -> PA3 / USART2_RX
 *   ATGM336H RX -> PA2 / USART2_TX
 *   USART2      -> 9600 8N1
 */
void GNSS_Init(UART_HandleTypeDef *huart);

/*
 * Drain bytes collected by the USART2 receive interrupt and assemble NMEA
 * sentences. Safe to call between camera captures and USB transfers.
 *
 * Return:
 *   1: one complete NMEA sentence copied to line_out
 *   0: no complete sentence yet
 */
uint8_t GNSS_Task(char *line_out, uint16_t line_out_size);

#ifdef __cplusplus
}
#endif

#endif /* __GNSS_H */
