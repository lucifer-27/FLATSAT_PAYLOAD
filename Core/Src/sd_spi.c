#include "sd_spi.h"
#include "main.h"
#include <stdio.h>

extern SPI_HandleTypeDef hspi1;
extern UART_HandleTypeDef huart6;
extern UART_HandleTypeDef huart3;


/* ============================================================
 * SD CARD SPI DRIVER
 *
 * SPI:
 *     SCK  -> PA5
 *     MISO -> PA6
 *     MOSI -> PB5
 *     CS   -> PD14
 *
 * SD card:
 *     64 GB SDXC
 *     SPI mode
 * ============================================================ */


/* ------------------------------------------------------------
 * CS CONTROL
 *
 * SD CARD CS = PD14 (CN7 PIN 16)
 * ------------------------------------------------------------ */

void SD_CS_High(void)
{
    HAL_GPIO_WritePin(GPIOD,
                      GPIO_PIN_14,
                      GPIO_PIN_SET);
}


void SD_CS_Low(void)
{
    HAL_GPIO_WritePin(GPIOD,
                      GPIO_PIN_14,
                      GPIO_PIN_RESET);
}


/* ------------------------------------------------------------
 * SPI TRANSFER
 * ------------------------------------------------------------ */

uint8_t SD_SPI_Transfer(uint8_t data)
{
    uint8_t rx = 0xFF;

    HAL_SPI_TransmitReceive(&hspi1,
                            &data,
                            &rx,
                            1,
                            HAL_MAX_DELAY);

    return rx;
}


/* ------------------------------------------------------------
 * SEND DUMMY CLOCKS
 *
 * SD card requires at least 74 clock cycles with CS HIGH
 * before initialization.
 * ------------------------------------------------------------ */

void SD_SendDummyClocks(uint16_t clocks)
{
    uint8_t dummy = 0xFF;

    for(uint16_t i = 0; i < clocks / 8; i++)
    {
        HAL_SPI_Transmit(&hspi1,
                         &dummy,
                         1,
                         HAL_MAX_DELAY);
    }
}


/* ------------------------------------------------------------
 * WAIT FOR SD CARD RESPONSE
 * ------------------------------------------------------------ */

uint8_t SD_WaitResponse(void)
{
    uint8_t response;

    for(uint32_t i = 0; i < 1000; i++)
    {
        response = SD_SPI_Transfer(0xFF);

        if(response != 0xFF)
        {
            return response;
        }
    }

    return 0xFF;
}


/* ------------------------------------------------------------
 * SEND SD COMMAND
 * ------------------------------------------------------------ */

uint8_t SD_SendCommand(uint8_t cmd,
                       uint32_t arg,
                       uint8_t crc)
{
    uint8_t packet[6];
    uint8_t response;

    packet[0] = 0x40 | cmd;

    packet[1] = (arg >> 24) & 0xFF;
    packet[2] = (arg >> 16) & 0xFF;
    packet[3] = (arg >> 8) & 0xFF;
    packet[4] = arg & 0xFF;

    packet[5] = crc | 0x01;

    /*
     * Select card
     */
    /* Select card */
    SD_CS_Low();

    /*
     * Give the SD card 8 additional clock cycles
     * after CS goes LOW and before CMD0.
     */
    SD_SPI_Transfer(0xFF);

    /* Send command */
    HAL_SPI_Transmit(&hspi1,
                     packet,
                     6,
                     HAL_MAX_DELAY);

    /*
     * Wait for R1 response.
     */
    response = SD_WaitResponse();

    return response;
}


/* ------------------------------------------------------------
 * SD CARD INITIALIZATION
 * ------------------------------------------------------------ */

uint8_t SD_Init(void)
{
    uint8_t response;
    uint8_t cmd8_response[4];


    /* Allow SD card and regulator to stabilize */
    HAL_Delay(200);


    /* Make sure card is deselected */
    SD_CS_High();


    /* 80 clock cycles with CS HIGH */
    SD_SendDummyClocks(80);


    /* ========================================================
     * CMD0
     * ======================================================== */

    response = SD_SendCommand(0,
                              0x00000000,
                              0x95);



    {
    	char msg[80];

    	int len = sprintf(
    	    msg,
    	    "CMD0 RESPONSE = 0x%02X\r\n",
    	    response
    	);

    	HAL_UART_Transmit(
    	    &huart3,
    	    (uint8_t *)msg,
    	    len,
    	    HAL_MAX_DELAY
    	);
    }


    if(response != 0x01)
    {
        SD_CS_High();
        SD_SPI_Transfer(0xFF);

        return 1;
    }


    /* Finish CMD0 */
    SD_CS_High();
    SD_SPI_Transfer(0xFF);


    /* ========================================================
     * CMD8
     * ======================================================== */

    {
        uint8_t msg[] = "STARTING CMD8\r\n";

        HAL_UART_Transmit(&huart6,
                          msg,
                          sizeof(msg) - 1,
                          HAL_MAX_DELAY);
    }


    response = SD_SendCommand(8,
                              0x000001AA,
                              0x87);


    {
        char msg[60];

        int len = sprintf(msg,
                          "CMD8 RESPONSE = 0x%02X\r\n",
                          response);

        HAL_UART_Transmit(&huart6,
                          (uint8_t *)msg,
                          len,
                          HAL_MAX_DELAY);
    }


    if(response == 0x01)
    {
        /* Read R7 remaining 4 bytes */

        for(int i = 0; i < 4; i++)
        {
            cmd8_response[i] =
                SD_SPI_Transfer(0xFF);
        }
    }


    /* Finish CMD8 */
    SD_CS_High();
    SD_SPI_Transfer(0xFF);


    /* ========================================================
     * ACMD41
     *
     * CMD55 + ACMD41
     *
     * HCS = 1
     * ======================================================== */

    for(uint32_t i = 0; i < 1000; i++)
    {

        /* ----------------------------------------------------
         * CMD55
         * ---------------------------------------------------- */

        response = SD_SendCommand(55,
                                   0,
                                   0x01);

        SD_CS_High();
        SD_SPI_Transfer(0xFF);


        /* ----------------------------------------------------
         * ACMD41
         * ---------------------------------------------------- */

        response = SD_SendCommand(41,
                                   0x40000000,
                                   0x01);


        /* Print every ACMD41 response */
        {
            char msg[80];

            int len = sprintf(msg,
                              "ACMD41 [%lu] RESPONSE = 0x%02X\r\n",
                              (unsigned long)i,
                              response);

            HAL_UART_Transmit(&huart6,
                              (uint8_t *)msg,
                              len,
                              HAL_MAX_DELAY);
        }


        /* Finish ACMD41 */
        SD_CS_High();
        SD_SPI_Transfer(0xFF);


        /* Card ready */
        if(response == 0x00)
        {
            uint8_t msg[] =
                "SD CARD READY\r\n";

            HAL_UART_Transmit(&huart6,
                              msg,
                              sizeof(msg) - 1,
                              HAL_MAX_DELAY);

            return 0;
        }


        HAL_Delay(10);
    }


    /* ========================================================
     * ACMD41 TIMEOUT
     * ======================================================== */

    {
        uint8_t msg[] =
            "ACMD41 TIMEOUT\r\n";

        HAL_UART_Transmit(&huart6,
                          msg,
                          sizeof(msg) - 1,
                          HAL_MAX_DELAY);
    }

    return 2;
}


/* ------------------------------------------------------------
 * READ ONE 512-BYTE BLOCK
 * ------------------------------------------------------------ */

uint8_t SD_ReadBlock(uint32_t block,
                     uint8_t *buffer)
{
    uint8_t response;
    uint8_t token;

    char msg[100];
    int len;


    /* ========================================================
     * CMD17 - READ SINGLE BLOCK
     * ======================================================== */

    response = SD_SendCommand(17,
                              block,
                              0x01);


    len = sprintf(msg,
                  "CMD17 BLOCK %lu RESPONSE = 0x%02X\r\n",
                  (unsigned long)block,
                  response);


    HAL_UART_Transmit(&huart6,
                      (uint8_t *)msg,
                      len,
                      HAL_MAX_DELAY);


    if(response != 0x00)
    {
        SD_CS_High();
        SD_SPI_Transfer(0xFF);

        return 1;
    }


    /* ========================================================
     * WAIT FOR DATA TOKEN 0xFE
     * ======================================================== */

    for(uint32_t i = 0; i < 100000; i++)
    {
        token = SD_SPI_Transfer(0xFF);


        if(token == 0xFE)
        {
            break;
        }


        if(token != 0xFF)
        {
            SD_CS_High();
            SD_SPI_Transfer(0xFF);

            return 2;
        }


        if(i == 99999)
        {
            SD_CS_High();
            SD_SPI_Transfer(0xFF);

            return 3;
        }
    }


    /* ========================================================
     * READ 512 BYTES
     * ======================================================== */

    if(HAL_SPI_Receive(&hspi1,
                       buffer,
                       512,
                       HAL_MAX_DELAY) != HAL_OK)
    {
        SD_CS_High();
        SD_SPI_Transfer(0xFF);

        return 4;
    }


    /* ========================================================
     * READ CRC
     *
     * CRC is ignored because the SD card is operating
     * in SPI mode with CRC checking disabled.
     * ======================================================== */

    SD_SPI_Transfer(0xFF);
    SD_SPI_Transfer(0xFF);


    /* ========================================================
     * DESELECT CARD
     * ======================================================== */

    SD_CS_High();
    SD_SPI_Transfer(0xFF);


    return 0;
}


/* ------------------------------------------------------------
 * WRITE ONE 512-BYTE BLOCK
 * ------------------------------------------------------------ */

uint8_t SD_WriteBlock(uint32_t block,
                      const uint8_t *buffer)
{
    uint8_t response;
    uint8_t token;
    uint8_t data_response;

    char msg[80];
    int len;


    /* ========================================================
     * CMD24
     * ======================================================== */

    response = SD_SendCommand(24,
                              block,
                              0x01);


    len = sprintf(msg,
                  "CMD24 BLOCK %lu RESPONSE = 0x%02X\r\n",
                  (unsigned long)block,
                  response);


    HAL_UART_Transmit(&huart6,
                      (uint8_t *)msg,
                      len,
                      HAL_MAX_DELAY);


    if(response != 0x00)
    {
        SD_CS_High();
        SD_SPI_Transfer(0xFF);

        return 1;
    }


    /* Dummy byte */
    SD_SPI_Transfer(0xFF);


    /* Data token */
    token = 0xFE;


    HAL_SPI_Transmit(&hspi1,
                     &token,
                     1,
                     HAL_MAX_DELAY);


    /* 512 bytes */
    HAL_SPI_Transmit(&hspi1,
                     (uint8_t *)buffer,
                     512,
                     HAL_MAX_DELAY);


    /* Dummy CRC */
    uint8_t crc[2] = {0xFF, 0xFF};


    HAL_SPI_Transmit(&hspi1,
                     crc,
                     2,
                     HAL_MAX_DELAY);


    /* Data response */
    data_response = SD_SPI_Transfer(0xFF);


    len = sprintf(msg,
                  "DATA RESPONSE = 0x%02X\r\n",
                  data_response);


    HAL_UART_Transmit(&huart6,
                      (uint8_t *)msg,
                      len,
                      HAL_MAX_DELAY);


    if((data_response & 0x1F) != 0x05)
    {
        SD_CS_High();
        SD_SPI_Transfer(0xFF);

        return 2;
    }


    /* Wait until card is ready */
    for(uint32_t i = 0;
        i < 1000000;
        i++)
    {
        if(SD_SPI_Transfer(0xFF) == 0xFF)
        {
            SD_CS_High();
            SD_SPI_Transfer(0xFF);

            return 0;
        }
    }


    /* Timeout */
    SD_CS_High();
    SD_SPI_Transfer(0xFF);


    return 3;
}
