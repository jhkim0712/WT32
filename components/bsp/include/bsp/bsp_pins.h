/**
 * @file bsp_pins.h
 * @brief GPIO map for the WT32-SC01 Plus (ZX3D50CE08S-USRC-4832) board.
 *
 * Values are taken from the "WT32-SC01 PLUS Datasheet" (Wireless-Tag), section
 * "Interface Description". Keep this file as the single source of truth for
 * pin numbers - every other component includes it instead of hard-coding a
 * GPIO number.
 */
#pragma once

#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------- */
/* LCD - ST7796UI, 480x320, 8080 (Intel 8080) 8-bit parallel bus           */
/* ---------------------------------------------------------------------- */
#define BSP_LCD_H_RES           480
#define BSP_LCD_V_RES           320
#define BSP_LCD_BITS_PER_PIXEL  16   /* RGB565 */

#define BSP_LCD_PIN_BL          GPIO_NUM_45  /* Backlight PWM, active HIGH */
#define BSP_LCD_PIN_RST         GPIO_NUM_4   /* Shared with touch reset!   */
#define BSP_LCD_PIN_DC          GPIO_NUM_0   /* "LCD_RS", command/data     */
#define BSP_LCD_PIN_WR          GPIO_NUM_47  /* Write strobe / "pclk"      */
#define BSP_LCD_PIN_TE          GPIO_NUM_48  /* Frame sync, unused by the  */
                                             /* i80 driver - left floating */
#define BSP_LCD_PIN_D0          GPIO_NUM_9
#define BSP_LCD_PIN_D1          GPIO_NUM_46
#define BSP_LCD_PIN_D2          GPIO_NUM_3
#define BSP_LCD_PIN_D3          GPIO_NUM_8
#define BSP_LCD_PIN_D4          GPIO_NUM_18
#define BSP_LCD_PIN_D5          GPIO_NUM_17
#define BSP_LCD_PIN_D6          GPIO_NUM_16
#define BSP_LCD_PIN_D7          GPIO_NUM_15

/* There is no dedicated LCD chip-select line on this board (CS is tied low
 * internally), so the i80 panel IO is configured with cs_gpio_num = -1. */

/* ---------------------------------------------------------------------- */
/* Touch - FT6336U capacitive controller over I2C                         */
/* ---------------------------------------------------------------------- */
#define BSP_TP_I2C_PORT         I2C_NUM_0
#define BSP_TP_PIN_SDA          GPIO_NUM_6
#define BSP_TP_PIN_SCL          GPIO_NUM_5
#define BSP_TP_PIN_INT          GPIO_NUM_7
#define BSP_TP_PIN_RST          BSP_LCD_PIN_RST /* Shared with the LCD reset */
#define BSP_TP_I2C_CLK_HZ       400000

/* ---------------------------------------------------------------------- */
/* microSD card - SPI mode                                                */
/* ---------------------------------------------------------------------- */
#define BSP_SD_SPI_HOST         SPI2_HOST
#define BSP_SD_PIN_CS           GPIO_NUM_41
#define BSP_SD_PIN_MOSI         GPIO_NUM_40
#define BSP_SD_PIN_SCLK         GPIO_NUM_39
#define BSP_SD_PIN_MISO         GPIO_NUM_38
#define BSP_SD_MOUNT_POINT      "/sdcard"
#define BSP_SD_MAX_FREQ_KHZ     20000

/* ---------------------------------------------------------------------- */
/* Audio amplifier - I2S output only (no microphone on this board)        */
/* ---------------------------------------------------------------------- */
#define BSP_I2S_PORT            I2S_NUM_0
#define BSP_I2S_PIN_BCLK        GPIO_NUM_36
#define BSP_I2S_PIN_WS          GPIO_NUM_35 /* a.k.a. LRCK */
#define BSP_I2S_PIN_DOUT        GPIO_NUM_37

/* ---------------------------------------------------------------------- */
/* Extended IO header - broken out for user projects, unused by default   */
/* ---------------------------------------------------------------------- */
#define BSP_EXT_IO1             GPIO_NUM_10
#define BSP_EXT_IO2             GPIO_NUM_11
#define BSP_EXT_IO3             GPIO_NUM_12
#define BSP_EXT_IO4             GPIO_NUM_13
#define BSP_EXT_IO5             GPIO_NUM_14
#define BSP_EXT_IO6             GPIO_NUM_21

/* ---------------------------------------------------------------------- */
/* RS485 - not used by this firmware, reserved for future use             */
/* ---------------------------------------------------------------------- */
#define BSP_RS485_PIN_RXD       GPIO_NUM_1
#define BSP_RS485_PIN_RTS       GPIO_NUM_2
#define BSP_RS485_PIN_TXD       GPIO_NUM_42

#ifdef __cplusplus
}
#endif
