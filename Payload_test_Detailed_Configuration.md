# Payload_test --- Detailed STM32H753ZITx Configuration

## 1. Project Overview

**Project:** `Payload_test`\
**Target MCU:** `STM32H753ZIT6`\
**MCU Family:** STM32H7\
**Package:** LQFP144\
**Development Board:** NUCLEO-H753ZI\
**Development Environment:** STM32CubeIDE\
**STM32CubeMX Version:** 6.5.0\
**STM32Cube Firmware Package:** STM32Cube FW_H7 V1.10.0\
**Configuration File:** `Payload_test.ioc`

The project is configured around the Cortex-M7 core of the STM32H753ZIT6
and provides:

-   SPI1 configured as a master
-   SPI2 configured as a slave
-   USART3 configured as an asynchronous UART
-   USART6 configured as an asynchronous UART at 9600 baud
-   FATFS support
-   GPIO output on PD14
-   NVIC interrupt support for SPI1 and USART6
-   SysTick-based HAL time base

------------------------------------------------------------------------

# 2. MCU Configuration

The selected microcontroller is:

``` text
STM32H753ZIT6
```

### MCU properties

  Property      Value
  ------------- ---------------
  Family        STM32H7
  Device        STM32H753ZIT6
  CubeMX Name   STM32H753ZITx
  Package       LQFP144
  Core          ARM Cortex-M7
  Board         NUCLEO-H753ZI

The project is configured for the M7 core only.

------------------------------------------------------------------------

# 3. Cortex-M7 Configuration

The Cortex-M7 configuration contains the following settings:

``` text
CORTEX_M7.CPU_DCache = Disabled
CORTEX_M7.CPU_ICache = Disabled
CORTEX_M7.MPU_Control = __NULL
```

### Instruction Cache

``` text
CPU_ICache = Disabled
```

The Cortex-M7 instruction cache is disabled.

### Data Cache

``` text
CPU_DCache = Disabled
```

The Cortex-M7 data cache is disabled.

This is particularly relevant when working with peripherals and DMA
because cache coherency can otherwise need additional handling.

### MPU

``` text
MPU_Control = __NULL
```

No custom MPU configuration is specified in the `.ioc` file.

------------------------------------------------------------------------

# 4. RCC / Clock Configuration

The external high-speed oscillator is configured as:

``` text
HSE = 8 MHz
```

The main system clock is configured to:

``` text
SYSCLK = 64 MHz
```

## Main clock values

  Clock                  Frequency
  -------------------- -----------
  HSE                        8 MHz
  SYSCLK                    64 MHz
  CPU / Cortex Clock        64 MHz
  HCLK                      64 MHz
  AXI Clock                 64 MHz
  APB1                      64 MHz
  APB2                      64 MHz
  APB3                      64 MHz
  APB4                      64 MHz
  AHB4                      64 MHz
  AHB12                     64 MHz
  HCLK3                     64 MHz
  CKPER                     64 MHz

Additional configured peripheral clock values include:

  Peripheral clock     Frequency
  ------------------ -----------
  SPI123                 150 MHz
  USB                    150 MHz
  FDCAN                  150 MHz
  SAI1                   150 MHz
  SAI23                  150 MHz
  SDMMC                  150 MHz
  RNG                     48 MHz
  RTC                     32 kHz

## PLL configuration

The `.ioc` contains:

``` text
RCC.DIVM1 = 4
RCC.DIVN1 = 9
RCC.DIVQ1 = 1
RCC.PLLFRACN = 3072
```

The voltage regulator scale is:

``` text
PWR_REGULATOR_VOLTAGE_SCALE2
```

------------------------------------------------------------------------

# 5. SPI1 Configuration --- Master

SPI1 is configured as a **full-duplex master**.

``` text
SPI1.Mode = SPI_MODE_MASTER
SPI1.Direction = SPI_DIRECTION_2LINES
SPI1.DataSize = SPI_DATASIZE_8BIT
```

## SPI1 Pins

  STM32 Pin   SPI Function   Direction
  ----------- -------------- --------------------
  PA5         SPI1_SCK       Output from master
  PA6         SPI1_MISO      Input to master
  PB5         SPI1_MOSI      Output from master

Therefore:

``` text
PA5 → SCK
PA6 ← MISO
PB5 → MOSI
```

## SPI1 Clock

The configured baud-rate prescaler is:

``` text
SPI_BAUDRATEPRESCALER_256
```

CubeMX calculates:

``` text
SPI1 Baud Rate ≈ 585.937 Kbits/s
```

This is approximately:

``` text
586 kbit/s
```

## SPI1 Data Format

``` text
Data Size = 8 bits
Direction = 2-line full duplex
```

This means SPI1 can transmit and receive simultaneously.

## NSS Pulse

``` text
NSSPMode = SPI_NSS_PULSE_DISABLE
```

The SPI NSS pulse feature is disabled.

## SPI1 Summary

``` text
                 SPI1 MASTER

             ┌───────────────┐
             │   STM32H753    │
             │                │
PA5  ───────►│ SCK            │
PB5  ───────►│ MOSI           │
PA6  ◄───────│ MISO           │
             │                │
             └───────────────┘

Speed ≈ 585.937 kbits/s
Data = 8-bit
Mode = Full Duplex Master
```

------------------------------------------------------------------------

# 6. SPI2 Configuration --- Slave

SPI2 is configured as a **full-duplex slave**.

``` text
SPI2.Mode = SPI_MODE_SLAVE
SPI2.Direction = SPI_DIRECTION_2LINES
SPI2.DataSize = SPI_DATASIZE_8BIT
```

Unlike SPI1, SPI2 does not generate the SPI clock. The external SPI
master supplies the clock.

## SPI2 Pins

  STM32 Pin   SPI Function   Description
  ----------- -------------- ----------------------------
  PB10        SPI2_SCK       Slave clock input
  PB12        SPI2_NSS       Hardware chip-select input
  PB15        SPI2_MOSI      Data input to STM32
  PC2_C       SPI2_MISO      Data output from STM32

Signal relationship:

``` text
External Master
      |
      +──── SCK  ─────► PB10
      |
      +──── NSS  ─────► PB12
      |
      +──── MOSI ─────► PB15
      |
      ◄─── MISO ────── PC2_C
```

## NSS Configuration

SPI2 uses:

``` text
SPI2.VirtualNSS = VM_NSSHARD
```

This corresponds to a hardware NSS input.

Therefore, the external master controls the slave-selection signal.

## SPI2 Summary

``` text
Mode       = Full Duplex Slave
Data size  = 8-bit
Direction  = 2-line
NSS        = Hardware input
```

------------------------------------------------------------------------

# 7. SPI1 ↔ SPI2 Communication Concept

The project contains both an SPI master and SPI slave interface.

Conceptually:

``` text
                 SPI MASTER
                  SPI1
             ┌─────────────┐
             │ STM32 /     │
             │ External    │
             │ Master      │
             └──────┬──────┘
                    │
          ┌─────────┼─────────┐
          │         │         │
         SCK       MOSI      MISO
          │         │         │
          ▼         ▼         ▼
       PB10       PB15      PC2_C
             ┌─────────────┐
             │    SPI2     │
             │    SLAVE    │
             └─────────────┘
```

The actual physical connection depends on the external hardware
architecture.

------------------------------------------------------------------------

# 8. USART6 Configuration

USART6 is configured as an asynchronous UART.

``` text
USART6.VirtualMode-Asynchronous = VM_ASYNC
```

## USART6 Pins

  Pin   Function
  ----- -----------
  PC6   USART6_TX
  PC7   USART6_RX

Signal direction:

``` text
STM32 PC6 (TX) ─────► External RX

STM32 PC7 (RX) ◄───── External TX
```

## Baud Rate

The configured baud rate is:

``` text
9600 baud
```

This means the UART communicates at:

``` text
9600 bits/second
```

assuming the remaining UART frame parameters use their CubeMX/HAL
defaults.

## USART6 Interrupt

The USART6 interrupt is enabled:

``` text
NVIC.USART6_IRQn = true
```

This allows interrupt-driven UART operations such as:

``` c
HAL_UART_Receive_IT(...)
HAL_UART_Transmit_IT(...)
```

to be used.

The corresponding interrupt handler is expected to call the HAL UART IRQ
handler:

``` c
HAL_UART_IRQHandler(&huart6);
```

------------------------------------------------------------------------

# 9. USART3 Configuration

USART3 is also configured in asynchronous mode:

``` text
USART3.VirtualMode-Asynchronous = VM_ASYNC
```

## USART3 Pins

  Pin    Function
  ------ -----------
  PD8    USART3_TX
  PB11   USART3_RX

Signal direction:

``` text
PD8  → USART3_TX
PB11 → USART3_RX
```

Unlike USART6, no USART3 interrupt is explicitly enabled in the provided
NVIC configuration.

------------------------------------------------------------------------

# 10. USART3 vs USART6

  Feature                          USART3         USART6
  -------------------------------- -------------- --------------
  Mode                             Asynchronous   Asynchronous
  TX                               PD8            PC6
  RX                               PB11           PC7
  Baud rate explicitly specified   No             9600 baud
  NVIC interrupt                   Not enabled    Enabled

Therefore, USART6 is the explicitly configured interrupt-driven UART
interface in this `.ioc` file.

------------------------------------------------------------------------

# 11. GPIO Configuration

PD14 is configured as a GPIO output.

``` text
PD14.Signal = GPIO_Output
```

The pin is also marked as locked:

``` text
PD14.Locked = true
```

This prevents CubeMX from automatically reallocating the pin to another
peripheral during configuration changes.

The application can control PD14 using HAL GPIO functions such as:

``` c
HAL_GPIO_WritePin(...)
HAL_GPIO_TogglePin(...)
```

depending on the generated GPIO configuration and application code.

------------------------------------------------------------------------

# 12. FATFS Configuration

FATFS is enabled in the project.

The virtual FATFS interface is:

``` text
VP_FATFS_VS_Generic
```

with:

``` text
Mode   = User_defined
Signal = FATFS_VS_Generic
```

The project therefore contains the FATFS middleware and related
generated application/target files.

The `.ioc` configuration alone does not specify a particular physical
storage interface such as SDMMC or SPI-based SD card access for FATFS.
The actual storage driver/interface must therefore be verified from the
generated source code and board hardware.

------------------------------------------------------------------------

# 13. SysTick Configuration

The project uses the SysTick virtual peripheral:

``` text
VP_SYS_VS_Systick
```

with:

``` text
Mode   = SysTick
Signal = SYS_VS_Systick
```

SysTick is enabled in the NVIC:

``` text
NVIC.SysTick_IRQn = true
```

SysTick is normally used by STM32 HAL for timing functions such as:

``` c
HAL_Delay(...)
HAL_GetTick(...)
```

------------------------------------------------------------------------

# 14. NVIC Configuration

The NVIC priority grouping is:

``` text
NVIC_PRIORITYGROUP_4
```

The following peripheral interrupts are explicitly enabled:

### SPI1

``` text
NVIC.SPI1_IRQn = true
```

SPI1 interrupt priority configuration:

``` text
Priority = 0
Subpriority = 0
```

### USART6

``` text
NVIC.USART6_IRQn = true
```

USART6 interrupt priority configuration:

``` text
Priority = 0
Subpriority = 0
```

### System Exceptions

The configuration also enables the Cortex-M7 system exception entries:

-   Non-Maskable Interrupt
-   HardFault
-   Memory Management Fault
-   BusFault
-   UsageFault
-   SVCall
-   Debug Monitor
-   PendSV
-   SysTick

The configuration also specifies:

``` text
NVIC.ForceEnableDMAVector = true
```

------------------------------------------------------------------------

# 15. Complete Pin Mapping

The complete application-relevant pin allocation from the `.ioc` file
is:

  Pin     Mode           Signal
  ------- -------------- -------------
  PC2_C   SPI2 Slave     SPI2_MISO
  PA5     SPI1 Master    SPI1_SCK
  PA6     SPI1 Master    SPI1_MISO
  PB10    SPI2 Slave     SPI2_SCK
  PB11    Asynchronous   USART3_RX
  PB12    Hardware NSS   SPI2_NSS
  PB15    SPI2 Slave     SPI2_MOSI
  PD8     Asynchronous   USART3_TX
  PD14    GPIO Output    GPIO_Output
  PC6     Asynchronous   USART6_TX
  PC7     Asynchronous   USART6_RX
  PB5     SPI1 Master    SPI1_MOSI
  PB6     Locked         USART1_TX
  PB7     Locked         USART1_RX

### Locked pins

The `.ioc` file marks these pins as locked:

``` text
PB5
PB6
PB7
PB15
PD14
```

PB6 and PB7 retain USART1 signal assignments in the configuration even
though USART1 is not listed among the enabled IP peripherals.

------------------------------------------------------------------------

# 16. Peripheral Initialization Order

CubeMX specifies the following initialization order:

``` text
1. SystemClock_Config
2. MX_GPIO_Init
3. MX_SPI1_Init
4. MX_USART6_UART_Init
5. MX_SPI2_Init
6. MX_FATFS_Init
7. MX_USART3_UART_Init
8. MX_CORTEX_M7_Init
```

This order determines how the generated `main.c` initializes the
peripherals.

------------------------------------------------------------------------

# 17. Project Manager Configuration

Important project-generation settings include:

``` text
ProjectManager.ProjectName       = Payload_test
ProjectManager.ProjectFileName   = Payload_test.ioc
ProjectManager.TargetToolchain   = STM32CubeIDE
ProjectManager.MainLocation      = Core/Src
ProjectManager.KeepUserCode     = true
ProjectManager.CompilerOptimize = 6
ProjectManager.StackSize        = 0x400
ProjectManager.HeapSize         = 0x200
```

### User code preservation

``` text
KeepUserCode = true
```

CubeMX is configured to preserve code placed inside the appropriate:

``` c
/* USER CODE BEGIN */
...
/* USER CODE END */
```

sections when code is regenerated.

### Compiler optimization

The configuration specifies:

``` text
CompilerOptimize = 6
```

This should be interpreted together with the actual STM32CubeIDE
compiler settings used by the project.

------------------------------------------------------------------------

# 18. Linker Configuration

Two linker scripts are referenced:

``` text
STM32H753ZITX_FLASH.ld
STM32H753ZITX_RAM.ld
```

These define the memory layout used when linking the application for
flash/RAM execution configurations.

The exact memory regions and addresses should be taken from the linker
scripts themselves rather than inferred from the `.ioc` file.

------------------------------------------------------------------------

# 19. Important Communication Parameters

  ----------------------------------------------------------------------------
  Interface         Mode                   Pins              Data/Speed
  ----------------- ---------------------- ----------------- -----------------
  SPI1              Master, Full Duplex    PA5, PA6, PB5     8-bit, \~585.937
                                                             kbit/s

  SPI2              Slave, Full Duplex     PB10, PB12, PB15, 8-bit
                                           PC2_C             

  USART3            Async UART             PD8, PB11         Baud not
                                                             specified in
                                                             `.ioc`

  USART6            Async UART             PC6, PC7          9600 baud

  FATFS             Generic/User-defined   Virtual interface Application
                                                             dependent

  GPIO              Output                 PD14              Digital output
  ----------------------------------------------------------------------------

------------------------------------------------------------------------

# 20. Recommended Repository Structure

For GitHub, the project should ideally retain the source/configuration
files and avoid generated build output.

Recommended structure:

``` text
Payload_test/
│
├── Core/
│   ├── Inc/
│   └── Src/
│
├── Drivers/
│
├── FATFS/
│
├── Middlewares/
│
├── .settings/
│
├── Payload_test.ioc
├── STM32H753ZITX_FLASH.ld
├── STM32H753ZITX_RAM.ld
├── .project
├── .cproject
└── .gitignore
```

Generated build directories such as:

``` text
Debug/
Release/
```

are normally excluded from source control.

------------------------------------------------------------------------

# 21. Configuration at a Glance

``` text
MCU
└── STM32H753ZIT6
    └── Cortex-M7
        ├── I-Cache: Disabled
        ├── D-Cache: Disabled
        └── MPU: Not configured

CLOCK
└── HSE: 8 MHz
    └── SYSCLK: 64 MHz

SPI1
└── Master
    ├── SCK  → PA5
    ├── MISO ← PA6
    ├── MOSI → PB5
    ├── 8-bit
    └── ≈585.937 kbit/s

SPI2
└── Slave
    ├── SCK  ← PB10
    ├── NSS  ← PB12
    ├── MOSI ← PB15
    ├── MISO → PC2_C
    └── 8-bit

USART3
└── Asynchronous
    ├── TX → PD8
    └── RX ← PB11

USART6
└── Asynchronous
    ├── TX → PC6
    ├── RX ← PC7
    ├── Baud: 9600
    └── Interrupt: Enabled

GPIO
└── PD14
    └── Output

FATFS
└── Generic / User-defined

SYS
└── SysTick
```

------------------------------------------------------------------------

# 22. Notes and Limitations

This document describes the configuration explicitly present in the
supplied `.ioc` file. Some runtime behavior cannot be determined from
the `.ioc` alone.

In particular:

1.  **USART3 baud rate** is not explicitly present in the supplied
    configuration.
2.  **UART word length, parity, stop bits, and flow control** are not
    explicitly specified in the supplied `.ioc` and should be checked in
    the generated `main.c`.
3.  **FATFS physical storage hardware** is not established by the
    generic FATFS configuration alone.
4.  The actual **SPI transaction protocol**, packet format, chip-select
    timing, and application-level data flow must be obtained from the
    firmware source code.
5.  The `.ioc` specifies peripheral configuration, but it does not
    describe the complete application behavior.
6.  The generated `Debug/` directory contains build artifacts and is
    generally not required for source-code version control.

------------------------------------------------------------------------

## Source

This documentation was generated from the supplied STM32CubeMX
configuration for:

``` text
Payload_test.ioc
```

Key configuration identifiers:

``` text
Mcu.CPN       = STM32H753ZIT6
Mcu.Name      = STM32H753ZITx
Mcu.Family    = STM32H7
Mcu.Package   = LQFP144
Project Name  = Payload_test
Board         = NUCLEO-H753ZI
CubeMX        = 6.5.0
Firmware      = STM32Cube FW_H7 V1.10.0
```
