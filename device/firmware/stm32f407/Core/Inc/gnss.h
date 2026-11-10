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
 *   ATGM336H TX -> PA1 / UART4_RX
 *   ATGM336H RX -> PA0 / UART4_TX
 *   UART4       -> 9600 8N1
 */
extern volatile uint32_t gnss_rx_bytes, gnss_rx_errors, gnss_rx_overruns;
void GNSS_Init(UART_HandleTypeDef *huart);

/*
 * Drain bytes collected by the UART4 receive interrupt and assemble NMEA
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
