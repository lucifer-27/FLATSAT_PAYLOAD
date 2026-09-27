/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "fatfs.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "sd_spi.h"
#include <string.h>
#include <stdio.h>
#include "ff.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum
{
    UART_STATE_WAIT_UTC,
    UART_STATE_WAIT_PROCESS_ACK,
    UART_WAIT_SEND_IMG,
    UART_STATE_DONE
} UART_State_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define CAPTURE_SIZE            9
#define UTC_DATA_SIZE           21
#define ACK_DATA_SIZE           13
#define UTC_RESPONSE_DELAY      200

#define PACKET_SIZE             210

/*
 * RPi sends packet count as:
 *
 * COUNT00125
 *
 * This is exactly 10 bytes.
 */
#define PACKET_COUNT_DATA_SIZE  10

/*
 * Maximum number of packets accepted.
 *
 * 99999 packets is the maximum representable
 * by COUNTxxxxx.
 */
#define MAX_PACKET_COUNT        99999
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

SPI_HandleTypeDef hspi1;
SPI_HandleTypeDef hspi2;

UART_HandleTypeDef huart3;
UART_HandleTypeDef huart6;

/* USER CODE BEGIN PV */
/*
 * SPI / SD variables
 */
uint8_t spi_tx;
uint8_t spi_rx;
uint8_t sd_buffer[512];


/*
 * FATFS variables
 */
FATFS image_fs;
FIL image_file;

FRESULT sd_result;
UINT bytes_written;
UINT bytes_read;


/*
 * SD test string
 */
char sd_test_data[] = "STM32 SD TEST\r\n";


/*
 * SPI packet receive buffer.
 *
 * Complete packet:
 *
 * [0..1]     SYNC
 * [2..5]     PACKET ID
 * [6..7]     PAYLOAD LENGTH
 * [8..9]     CRC
 * [10..209]  PAYLOAD
 */
uint8_t spi2_rx_buffer[PACKET_SIZE];


/*
 * Command sent from STM32 to Raspberry Pi.
 */
uint8_t capture_cmd[] = "CAPTURE\r\n";


/*
 * UTC folder name received from Raspberry Pi.
 */
uint8_t utc_data[UTC_DATA_SIZE];


/*
 * Process ACK received from Raspberry Pi.
 */
uint8_t ack_data[ACK_DATA_SIZE];


/*
 * Packet count received from Raspberry Pi.
 *
 * Expected format:
 *
 * COUNT00125
 *
 * exactly 10 bytes.
 */
uint8_t packet_count_data[PACKET_COUNT_DATA_SIZE];


/*
 * Total number of packets in current image.
 */
volatile uint32_t total_packets = 0;


/*
 * Indicates that packet count was received
 * and successfully decoded.
 */
volatile uint8_t packet_count_received = 0;


/*
 * Current UART state.
 */
volatile UART_State_t uart_state = UART_STATE_WAIT_UTC;


/*
 * Flag indicating UTC data has been received.
 */
volatile uint8_t utc_received = 0;


/*
 * Flag indicating final processing ACK
 * has been received.
 */
volatile uint8_t ack_received = 0;


/*
 * Time at which UTC reception completed.
 */
volatile uint32_t utc_received_time = 0;


/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART6_UART_Init(void);
static void MX_SPI2_Init(void);
static void MX_USART3_UART_Init(void);
/* USER CODE BEGIN PFP */



/* =========================================================
 * CRC-16-CCITT
 * ========================================================= */

uint16_t crc16_ccitt(const uint8_t *data, uint32_t length);


/* =========================================================
 * UART RECEIVE CALLBACK
 * ========================================================= */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART6)
    {
        return;
    }


    /* =====================================================
     * STATE 1:
     * WAITING FOR UTC
     * ===================================================== */

    if (uart_state == UART_STATE_WAIT_UTC)
    {
        /*
         * UTC reception completed.
         */

        utc_received = 1;


        /*
         * Echo UTC back to Raspberry Pi.
         */
        HAL_UART_Transmit(
            &huart6,
            utc_data,
            UTC_DATA_SIZE,
            HAL_MAX_DELAY
        );


        /*
         * Next we expect process ACK.
         */
        uart_state = UART_STATE_WAIT_PROCESS_ACK;


        /*
         * Receive process ACK.
         */
        HAL_UART_Receive_IT(
            &huart6,
            ack_data,
            ACK_DATA_SIZE
        );


        /*
         * Store reception time.
         */
        utc_received_time = HAL_GetTick();
    }


    /* =====================================================
     * STATE 2:
     * WAITING FOR PROCESS ACK
     * ===================================================== */

    else if (uart_state == UART_STATE_WAIT_PROCESS_ACK)
    {
        /*
         * Process ACK received.
         */

        ack_received = 1;


        /*
         * Next we expect packet count.
         */
        uart_state = UART_WAIT_SEND_IMG;


        /*
         * Raspberry Pi will send:
         *
         * COUNT00125
         *
         * exactly 10 bytes.
         */
        HAL_UART_Receive_IT(
            &huart6,
            packet_count_data,
            PACKET_COUNT_DATA_SIZE
        );
    }


    /* =====================================================
     * STATE 3:
     * WAITING FOR PACKET COUNT
     * ===================================================== */

    else if (uart_state == UART_WAIT_SEND_IMG)
    {
        /*
         * Expected format:
         *
         * C O U N T 0 0 1 2 5
         *
         * Example:
         *
         * COUNT00125
         */

        total_packets = 0;


        /*
         * Verify "COUNT".
         */
        if (packet_count_data[0] != 'C' ||
            packet_count_data[1] != 'O' ||
            packet_count_data[2] != 'U' ||
            packet_count_data[3] != 'N' ||
            packet_count_data[4] != 'T')
        {
            /*
             * Invalid packet-count header.
             */

            total_packets = 0;
            packet_count_received = 0;

            HAL_UART_Receive_IT(
                &huart6,
                packet_count_data,
                PACKET_COUNT_DATA_SIZE
            );

            return;
        }


        /*
         * Decode five decimal digits.
         *
         * COUNT00001 = 1
         * COUNT00125 = 125
         * COUNT10000 = 10000
         */
        for (uint8_t i = 5; i < 10; i++)
        {
            /*
             * Make sure character is a digit.
             */
            if (packet_count_data[i] < '0' ||
                packet_count_data[i] > '9')
            {
                total_packets = 0;
                packet_count_received = 0;

                HAL_UART_Receive_IT(
                    &huart6,
                    packet_count_data,
                    PACKET_COUNT_DATA_SIZE
                );

                return;
            }


            total_packets =
                (total_packets * 10) +
                (packet_count_data[i] - '0');
        }


        /*
         * Validate packet count.
         */
        if (total_packets == 0 ||
            total_packets > MAX_PACKET_COUNT)
        {
            total_packets = 0;
            packet_count_received = 0;

            HAL_UART_Receive_IT(
                &huart6,
                packet_count_data,
                PACKET_COUNT_DATA_SIZE
            );

            return;
        }


        /*
         * Packet count is valid.
         */
        packet_count_received = 1;
    }
}


/* =========================================================
 * UART ERROR CALLBACK
 * ========================================================= */

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART6)
    {
        return;
    }


    if (uart_state == UART_STATE_WAIT_UTC)
    {
        HAL_UART_Receive_IT(
            &huart6,
            utc_data,
            UTC_DATA_SIZE
        );
    }


    else if (uart_state == UART_STATE_WAIT_PROCESS_ACK)
    {
        HAL_UART_Receive_IT(
            &huart6,
            ack_data,
            ACK_DATA_SIZE
        );
    }


    else if (uart_state == UART_WAIT_SEND_IMG)
    {
        HAL_UART_Receive_IT(
            &huart6,
            packet_count_data,
            PACKET_COUNT_DATA_SIZE
        );
    }
}

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* =========================================================
 * CRC-16-CCITT
 * ========================================================= */

uint16_t crc16_ccitt(const uint8_t *data, uint32_t length)
{
    uint16_t crc = 0xFFFF;


    for (uint32_t i = 0; i < length; i++)
    {
        crc ^= ((uint16_t)data[i] << 8);


        for (uint8_t j = 0; j < 8; j++)
        {
            if (crc & 0x8000)
            {
                crc = (crc << 1) ^ 0x1021;
            }
            else
            {
                crc <<= 1;
            }
        }
    }


    return crc;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

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
  MX_SPI1_Init();
  MX_USART6_UART_Init();
  MX_SPI2_Init();
  MX_FATFS_Init();
  MX_USART3_UART_Init();
  /* USER CODE BEGIN 2 */

  /* =========================================================
   * SD CARD INITIALIZATION TEST
   * ========================================================= */

  HAL_Delay(500);


  /*
   * Tell PC that SD test is starting.
   */
  HAL_UART_Transmit(
      &huart3,
      (uint8_t *)"STARTING SD TEST\r\n",
      strlen("STARTING SD TEST\r\n"),
      HAL_MAX_DELAY
  );


  /*
   * Initialize SD card.
   */
  uint8_t sd_status = SD_Init();


  /*
   * Print SD initialization result.
   */
  char sd_msg[80];

  int sd_len = sprintf(
      sd_msg,
      "SD_Init() RETURN = %u\r\n",
      sd_status
  );

  HAL_UART_Transmit(
      &huart3,
      (uint8_t *)sd_msg,
      sd_len,
      HAL_MAX_DELAY
  );


  HAL_Delay(300);


  /* =========================================================
   * START IMAGE CAPTURE / PROCESSING SEQUENCE
   * ========================================================= */


  /*
   * Tell Raspberry Pi to capture/process image.
   */
  HAL_UART_Transmit(
      &huart6,
      capture_cmd,
      sizeof(capture_cmd) - 1,
      HAL_MAX_DELAY
  );


  /*
   * We are now waiting for UTC folder name.
   */
  uart_state = UART_STATE_WAIT_UTC;


  /*
   * Start interrupt-based UTC reception.
   */
  HAL_UART_Receive_IT(
      &huart6,
      utc_data,
      UTC_DATA_SIZE
  );
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	    /*
	     * =========================================================
	     * WAIT FOR PACKET COUNT
	     * =========================================================
	     *
	     * RPi sends:
	     *
	     * COUNT00010
	     * COUNT00125
	     * COUNT01000
	     *
	     * Exactly 10 bytes.
	     */

	    if (packet_count_received)
	    {
	        packet_count_received = 0;

	        HAL_UART_Transmit(
	            &huart3,
	            (uint8_t *)"STEP 1: PACKET COUNT RECEIVED\r\n",
	            strlen("STEP 1: PACKET COUNT RECEIVED\r\n"),
	            HAL_MAX_DELAY
	        );

	        /*
	         * -----------------------------------------------------
	         * PRINT TOTAL PACKET COUNT
	         * -----------------------------------------------------
	         */

	        char count_msg[100];

	        int count_len = sprintf(
	            count_msg,
	            "\r\nTOTAL PACKETS: %lu\r\n",
	            total_packets
	        );

	        HAL_UART_Transmit(
	            &huart3,
	            (uint8_t *)count_msg,
	            count_len,
	            HAL_MAX_DELAY
	        );


	        /*
	         * -----------------------------------------------------
	         * CHECK PACKET COUNT
	         * -----------------------------------------------------
	         */

	        if (total_packets == 0 ||
	            total_packets > MAX_PACKET_COUNT)
	        {
	            HAL_UART_Transmit(
	                &huart3,
	                (uint8_t *)"INVALID PACKET COUNT\r\n",
	                strlen("INVALID PACKET COUNT\r\n"),
	                HAL_MAX_DELAY
	            );

	            total_packets = 0;

	            continue;
	        }


	        /*
	         * -----------------------------------------------------
	         * VARIABLES
	         * -----------------------------------------------------
	         */

	        FRESULT fres;

	        UINT bytes_written;

	        uint8_t packet_buffer[PACKET_SIZE];

	        uint8_t transfer_ok = 1;


	        /*
	         * -----------------------------------------------------
	         * MOUNT FAT FILESYSTEM
	         * -----------------------------------------------------
	         */

	        HAL_UART_Transmit(
	            &huart3,
	            (uint8_t *)"STEP 2: STARTING FATFS MOUNT\r\n",
	            strlen("STEP 2: STARTING FATFS MOUNT\r\n"),
	            HAL_MAX_DELAY
	        );

	        fres = f_mount(
	            &USERFatFS,
	            USERPath,
	            1
	        );

	        HAL_UART_Transmit(
	            &huart3,
	            (uint8_t *)"STEP 3: FATFS MOUNT RETURNED\r\n",
	            strlen("STEP 3: FATFS MOUNT RETURNED\r\n"),
	            HAL_MAX_DELAY
	        );

	        if (fres != FR_OK)
	        {
	            char msg[80];

	            int len = sprintf(
	                msg,
	                "FATFS MOUNT ERROR: %d\r\n",
	                fres
	            );

	            HAL_UART_Transmit(
	                &huart3,
	                (uint8_t *)msg,
	                len,
	                HAL_MAX_DELAY
	            );

	            total_packets = 0;

	            continue;
	        }

	        HAL_UART_Transmit(
	            &huart3,
	            (uint8_t *)"FATFS MOUNT OK\r\n",
	            strlen("FATFS MOUNT OK\r\n"),
	            HAL_MAX_DELAY
	        );


	        /*
	         * -----------------------------------------------------
	         * OPEN IMAGE FILE
	         * -----------------------------------------------------
	         */

	        HAL_UART_Transmit(
	            &huart3,
	            (uint8_t *)"STEP 4: OPENING image.bin\r\n",
	            strlen("STEP 4: OPENING image.bin\r\n"),
	            HAL_MAX_DELAY
	        );

	        fres = f_open(
	            &USERFile,
	            "image.bin",
	            FA_CREATE_ALWAYS | FA_WRITE
	        );

	        HAL_UART_Transmit(
	            &huart3,
	            (uint8_t *)"STEP 5: f_open RETURNED\r\n",
	            strlen("STEP 5: f_open RETURNED\r\n"),
	            HAL_MAX_DELAY
	        );

	        if (fres != FR_OK)
	        {
	            char msg[80];

	            int len = sprintf(
	                msg,
	                "image.bin OPEN ERROR: %d\r\n",
	                fres
	            );

	            HAL_UART_Transmit(
	                &huart3,
	                (uint8_t *)msg,
	                len,
	                HAL_MAX_DELAY
	            );

	            total_packets = 0;

	            continue;
	        }

	        HAL_UART_Transmit(
	            &huart3,
	            (uint8_t *)"image.bin OPEN OK\r\n",
	            strlen("image.bin OPEN OK\r\n"),
	            HAL_MAX_DELAY
	        );


	        /*
	         * -----------------------------------------------------
	         * TELL RPI TO START SPI TRANSFER
	         * -----------------------------------------------------
	         */
	        HAL_UART_Transmit(
	            &huart3,
	            (uint8_t *)"STEP 6: ABOUT TO SEND SEND IMAGE\r\n",
	            strlen("STEP 6: ABOUT TO SEND SEND IMAGE\r\n"),
	            HAL_MAX_DELAY
	        );

	        uint8_t tx_msg[] = "SEND IMAGE\r\n";

	        HAL_UART_Transmit(
	            &huart6,
	            tx_msg,
	            sizeof(tx_msg) - 1,
	            HAL_MAX_DELAY
	        );

	        HAL_UART_Transmit(
	            &huart3,
	            (uint8_t *)"STEP 7: SEND IMAGE SENT\r\n",
	            strlen("STEP 7: SEND IMAGE SENT\r\n"),
	            HAL_MAX_DELAY
	        );

	        /*
	         * =====================================================
	         * RECEIVE ALL PACKETS
	         * =====================================================
	         */

	        for (uint32_t expected_packet_id = 0;
	             expected_packet_id < total_packets;
	             expected_packet_id++)
	        {
	            /*
	             * -------------------------------------------------
	             * TELL RPI STM32 IS READY
	             * -------------------------------------------------
	             */

	            uint8_t ready_msg[] = "SPI READY\r\n";

	            HAL_UART_Transmit(
	                &huart6,
	                ready_msg,
	                sizeof(ready_msg) - 1,
	                HAL_MAX_DELAY
	            );


	            /*
	             * -------------------------------------------------
	             * RECEIVE 210 BYTE SPI PACKET
	             * -------------------------------------------------
	             */

	            HAL_StatusTypeDef spi_status;

	            spi_status = HAL_SPI_Receive(
	                &hspi2,
	                packet_buffer,
	                PACKET_SIZE,
	                5000
	            );

	            if (spi_status != HAL_OK)
	            {
	                uint32_t error =
	                    HAL_SPI_GetError(&hspi2);

	                char error_msg[120];

	                int len = sprintf(
	                    error_msg,
	                    "PACKET %lu SPI ERROR: 0x%08lX\r\n",
	                    expected_packet_id,
	                    error
	                );

	                HAL_UART_Transmit(
	                    &huart3,
	                    (uint8_t *)error_msg,
	                    len,
	                    HAL_MAX_DELAY
	                );

	                transfer_ok = 0;

	                break;
	            }


	            /*
	             * -------------------------------------------------
	             * READ PACKET HEADER
	             * -------------------------------------------------
	             */

	            uint16_t sync =
	                ((uint16_t)packet_buffer[0] << 8) |
	                packet_buffer[1];


	            uint32_t packet_id =
	                ((uint32_t)packet_buffer[2] << 24) |
	                ((uint32_t)packet_buffer[3] << 16) |
	                ((uint32_t)packet_buffer[4] << 8) |
	                packet_buffer[5];


	            uint16_t payload_length =
	                ((uint16_t)packet_buffer[6] << 8) |
	                packet_buffer[7];


	            uint16_t received_crc =
	                ((uint16_t)packet_buffer[8] << 8) |
	                packet_buffer[9];


	            /*
	             * -------------------------------------------------
	             * CHECK PAYLOAD LENGTH
	             * -------------------------------------------------
	             */

	            if (payload_length > 200)
	            {
	                char error_msg[120];

	                int len = sprintf(
	                    error_msg,
	                    "PACKET %lu INVALID LENGTH: %u\r\n",
	                    expected_packet_id,
	                    payload_length
	                );

	                HAL_UART_Transmit(
	                    &huart3,
	                    (uint8_t *)error_msg,
	                    len,
	                    HAL_MAX_DELAY
	                );

	                transfer_ok = 0;

	                break;
	            }


	            /*
	             * -------------------------------------------------
	             * CRC
	             * -------------------------------------------------
	             */

	            uint8_t crc_buffer[208];

	            memcpy(
	                crc_buffer,
	                packet_buffer,
	                8
	            );

	            memcpy(
	                &crc_buffer[8],
	                &packet_buffer[10],
	                payload_length
	            );

	            uint16_t calculated_crc =
	                crc16_ccitt(
	                    crc_buffer,
	                    8 + payload_length
	                );


	            /*
	             * -------------------------------------------------
	             * PRINT PACKET INFO
	             * -------------------------------------------------
	             */

	            char msg[220];

	            snprintf(
	                msg,
	                sizeof(msg),
	                "\r\nPACKET ID: %lu\r\n"
	                "EXPECTED ID: %lu\r\n"
	                "SYNC: 0x%04X\r\n"
	                "PAYLOAD LENGTH: %u\r\n"
	                "RECEIVED CRC: 0x%04X\r\n"
	                "CALCULATED CRC: 0x%04X\r\n",
	                packet_id,
	                expected_packet_id,
	                sync,
	                payload_length,
	                received_crc,
	                calculated_crc
	            );

	            HAL_UART_Transmit(
	                &huart3,
	                (uint8_t *)msg,
	                strlen(msg),
	                HAL_MAX_DELAY
	            );


	            /*
	             * -------------------------------------------------
	             * VALIDATE PACKET
	             * -------------------------------------------------
	             */

	            if (sync != 0xAA55 ||
	                packet_id != expected_packet_id ||
	                received_crc != calculated_crc)
	            {
	                char error_msg[140];

	                int len = sprintf(
	                    error_msg,
	                    "PACKET %lu VALIDATION FAILED\r\n",
	                    expected_packet_id
	                );

	                HAL_UART_Transmit(
	                    &huart3,
	                    (uint8_t *)error_msg,
	                    len,
	                    HAL_MAX_DELAY
	                );

	                transfer_ok = 0;

	                break;
	            }


	            /*
	             * -------------------------------------------------
	             * CRC OK
	             * -------------------------------------------------
	             */

	            char crc_ok_msg[100];

	            int crc_ok_len = sprintf(
	                crc_ok_msg,
	                "PACKET %lu CRC OK\r\n",
	                expected_packet_id
	            );

	            HAL_UART_Transmit(
	                &huart3,
	                (uint8_t *)crc_ok_msg,
	                crc_ok_len,
	                HAL_MAX_DELAY
	            );


	            /*
	             * -------------------------------------------------
	             * WRITE PAYLOAD TO SD
	             * -------------------------------------------------
	             */

	            fres = f_write(
	                &USERFile,
	                &packet_buffer[10],
	                payload_length,
	                &bytes_written
	            );


	            if (fres != FR_OK ||
	                bytes_written != payload_length)
	            {
	                char write_msg[140];

	                int len = sprintf(
	                    write_msg,
	                    "PACKET %lu WRITE ERROR: result=%d bytes=%u\r\n",
	                    expected_packet_id,
	                    fres,
	                    bytes_written
	                );

	                HAL_UART_Transmit(
	                    &huart3,
	                    (uint8_t *)write_msg,
	                    len,
	                    HAL_MAX_DELAY
	                );

	                transfer_ok = 0;

	                break;
	            }


	            /*
	             * -------------------------------------------------
	             * WRITE SUCCESS
	             * -------------------------------------------------
	             */

	            char write_ok_msg[120];

	            int write_ok_len = sprintf(
	                write_ok_msg,
	                "PACKET %lu WRITE OK: %u BYTES\r\n",
	                expected_packet_id,
	                bytes_written
	            );

	            HAL_UART_Transmit(
	                &huart3,
	                (uint8_t *)write_ok_msg,
	                write_ok_len,
	                HAL_MAX_DELAY
	            );
	        }


	        /*
	         * =====================================================
	         * CLOSE FILE
	         * =====================================================
	         */

	        fres = f_close(&USERFile);

	        if (fres == FR_OK)
	        {
	            HAL_UART_Transmit(
	                &huart3,
	                (uint8_t *)"image.bin CLOSE OK\r\n",
	                strlen("image.bin CLOSE OK\r\n"),
	                HAL_MAX_DELAY
	            );
	        }
	        else
	        {
	            char close_msg[100];

	            int len = sprintf(
	                close_msg,
	                "image.bin CLOSE ERROR: %d\r\n",
	                fres
	            );

	            HAL_UART_Transmit(
	                &huart3,
	                (uint8_t *)close_msg,
	                len,
	                HAL_MAX_DELAY
	            );

	            transfer_ok = 0;
	        }


	        /*
	         * =====================================================
	         * FINAL RESULT
	         * =====================================================
	         */

	        if (transfer_ok)
	        {
	            char complete_msg[140];

	            int complete_len = sprintf(
	                complete_msg,
	                "\r\n================================\r\n"
	                "COMPLETE IMAGE RECEIVED\r\n"
	                "TOTAL PACKETS: %lu\r\n"
	                "PACKET RANGE: 0-%lu\r\n"
	                "FILE: image.bin\r\n"
	                "================================\r\n",
	                total_packets,
	                total_packets - 1
	            );

	            HAL_UART_Transmit(
	                &huart3,
	                (uint8_t *)complete_msg,
	                complete_len,
	                HAL_MAX_DELAY
	            );
	        }
	        else
	        {
	            HAL_UART_Transmit(
	                &huart3,
	                (uint8_t *)"COMPLETE IMAGE TRANSFER FAILED\r\n",
	                strlen("COMPLETE IMAGE TRANSFER FAILED\r\n"),
	                HAL_MAX_DELAY
	            );
	        }


	        /*
	         * -----------------------------------------------------
	         * FINISHED
	         * -----------------------------------------------------
	         */

	        total_packets = 0;

	        uart_state = UART_STATE_DONE;

	        /*
	         * Stop main loop after one image.
	         */
	        break;
	    }

	    /* USER CODE END 3 */
	  }

	  /* USER CODE END WHILE */

	}   /* <-- closes main() */

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 9;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 1;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOMEDIUM;
  RCC_OscInitStruct.PLL.PLLFRACN = 3072;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV1;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 0x0;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  hspi1.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
  hspi1.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
  hspi1.Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  hspi1.Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  hspi1.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
  hspi1.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
  hspi1.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
  hspi1.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_DISABLE;
  hspi1.Init.IOSwap = SPI_IO_SWAP_DISABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief SPI2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI2_Init(void)
{

  /* USER CODE BEGIN SPI2_Init 0 */

  /* USER CODE END SPI2_Init 0 */

  /* USER CODE BEGIN SPI2_Init 1 */

  /* USER CODE END SPI2_Init 1 */
  /* SPI2 parameter configuration*/
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_SLAVE;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_HARD_INPUT;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 0x0;
  hspi2.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  hspi2.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
  hspi2.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
  hspi2.Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  hspi2.Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  hspi2.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
  hspi2.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
  hspi2.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
  hspi2.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_DISABLE;
  hspi2.Init.IOSwap = SPI_IO_SWAP_DISABLE;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI2_Init 2 */

  /* USER CODE END SPI2_Init 2 */

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart3.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart3, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart3, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

}

/**
  * @brief USART6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART6_UART_Init(void)
{

  /* USER CODE BEGIN USART6_Init 0 */

  /* USER CODE END USART6_Init 0 */

  /* USER CODE BEGIN USART6_Init 1 */

  /* USER CODE END USART6_Init 1 */
  huart6.Instance = USART6;
  huart6.Init.BaudRate = 9600;
  huart6.Init.WordLength = UART_WORDLENGTH_8B;
  huart6.Init.StopBits = UART_STOPBITS_1;
  huart6.Init.Parity = UART_PARITY_NONE;
  huart6.Init.Mode = UART_MODE_TX_RX;
  huart6.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart6.Init.OverSampling = UART_OVERSAMPLING_16;
  huart6.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart6.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart6.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart6) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart6, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart6, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart6) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART6_Init 2 */

  /* USER CODE END USART6_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_14, GPIO_PIN_RESET);

  /*Configure GPIO pin : PD14 */
  GPIO_InitStruct.Pin = GPIO_PIN_14;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pins : PB6 PB7 */
  GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

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
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
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
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
