/*
 * Copyright (c) 2016, Freescale Semiconductor, Inc.
 * Copyright 2016-2017 NXP
 * All rights reserved.
 *
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "fsl_device_registers.h"
#include "fsl_debug_console.h"
#include "pin_mux.h"
#include "board.h"
#include "fsl_dma.h"
#include "fsl_i2c.h"
#include "fsl_i2s.h"
#include "fsl_i2s_dma.h"
#include "fsl_codec_common.h"
#include "music.h"

#include "tone.h"

#include <stdbool.h>
#include "fsl_codec_adapter.h"
#include "fsl_cs42448.h"
/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define DEMO_I2S_MASTER_CLOCK_FREQUENCY CLOCK_GetMclkClkFreq()
// For some reason, the only bit width that the driver can handle is 24 even though
// specs say 16, 18, 20 and 24 (p 34)
#define DEMO_AUDIO_BIT_WIDTH            (24)
// In TDM mode, there are 32 clicks per sample, regardless of bitwidth
#define DEMO_AUDIO_TICKS_PER_SAMPLE     (32)
#define DEMO_AUDIO_SAMPLE_RATE          (48000)
#define DEMO_NUM_AUDIO_CH               (8)
#define DEMO_AUDIO_PROTOCOL             kCODEC_BusI2S
#define DEMO_I2S_TX                     (I2S3)
#define DEMO_DMA                        (DMA0)
#define DEMO_I2S_TX_CHANNEL             (7)
// The bit rate required for I2S is the sampling rate (48k) multiplied by the number of bits per sample (32 required) multiplied by the number of
// channels (in this case 8).  To get this value, we need to divide the master clock down by this amount.
#define DEMO_I2S_CLOCK_DIVIDER          DEMO_I2S_MASTER_CLOCK_FREQUENCY / DEMO_AUDIO_SAMPLE_RATE / DEMO_AUDIO_TICKS_PER_SAMPLE / DEMO_NUM_AUDIO_CH
#define DEMO_I2S_TX_MODE                kI2S_MasterSlaveNormalMaster
#define DEMO_CODEC_VOLUME               100U
#define DEMO_TDM_DATA_START_POSITION    1U
#ifndef DEMO_CODEC_VOLUME
#define DEMO_CODEC_VOLUME 30U
#endif
/*******************************************************************************
 * Prototypes
 ******************************************************************************/

static void StartSoundPlayback(void);

static void TxCallback(I2S_Type *base, i2s_dma_handle_t *handle, status_t completionStatus, void *userData);

/*******************************************************************************
 * Variables
 ******************************************************************************/
cs42448_config_t cs42448Config = {
    .DACMode      = kCS42448_ModeSlave,
    .ADCMode      = kCS42448_ModeSlave,
    .reset        = NULL,
    .master       = false,
    .i2cConfig    = {.codecI2CInstance = BOARD_CODEC_I2C_INSTANCE},
    .format       = {.sampleRate = 48000U, .bitWidth = 24U},
    .bus          = kCS42448_BusTDM,
    .slaveAddress = CS42448_I2C_ADDR,
};

codec_config_t boardCodecConfig = {.codecDevType = kCODEC_CS42448, .codecDevConfig = &cs42448Config};

static dma_handle_t s_DmaTxHandle;
static i2s_config_t s_TxConfig;
static i2s_dma_handle_t s_TxHandle;
static i2s_transfer_t s_TxTransfer;
extern codec_config_t boardCodecConfig;
codec_handle_t codecHandle;

uint8_t *buffer = {0};
uint16_t xfer_size = 0;
/*******************************************************************************
 * Code
 ******************************************************************************/

/*!
 * @brief Main function
 */
int main(void)
{
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();

    CLOCK_EnableClock(kCLOCK_InputMux);

    /* I2C */
    CLOCK_AttachClk(kFFRO_to_FLEXCOMM2);

    /* attach AUDIO PLL clock to FLEXCOMM3 (I2S3) */
    // Flexcomm 3 is used by the codec...
    CLOCK_AttachClk(kAUDIO_PLL_to_FLEXCOMM3);

    /* attach AUDIO PLL clock to MCLK */
    CLOCK_AttachClk(kAUDIO_PLL_to_MCLK_CLK);
    CLOCK_SetClkDiv(kCLOCK_DivMclkClk, 1);
    SYSCTL1->MCLKPINDIR = SYSCTL1_MCLKPINDIR_MCLKPINDIR_MASK;

    cs42448Config.i2cConfig.codecI2CSourceClock = CLOCK_GetFlexCommClkFreq(2);
    cs42448Config.format.mclk_HZ                = DEMO_I2S_MASTER_CLOCK_FREQUENCY;

    PRINTF("Configure codec\r\n");
    uint8_t reg_val = 0;

    if (CODEC_Init(&codecHandle, &boardCodecConfig) != kStatus_Success)
    {
        PRINTF("codec_Init failed!\r\n");
        assert(false);
    }
    CS42448_ReadReg((cs42448_handle_t *)((uint32_t)(codecHandle.codecDevHandle)), CS42448_POWER_CONTROL, &reg_val);

    // Turn everything off excpet the headphone jack
    uint8_t pdn_down = kCS42448_ModuleDACPair2 | kCS42448_ModuleDACPair3 | kCS42448_ModuleDACPair4 |
                        kCS42448_ModuleADCPair1 | kCS42448_ModuleADCPair2 | kCS42448_ModuleADCPair3;
    // status_t error_val = CS42448_SetModule((cs42448_handle_t *)((uint32_t)(codecHandle.codecDevHandle)), pdn_down, true);
    CS42448_ReadReg((cs42448_handle_t *)((uint32_t)(codecHandle.codecDevHandle)), CS42448_POWER_CONTROL, &reg_val);
    // if (error_val)
    // {
    //     PRINTF("Attempt to turn off power domains failed with %d", error_val);
    //     assert(false);
    // }

    /* Initial volume kept low for hearing safety.
     * Adjust it to your needs, 0-100, 0 for mute, 100 for maximum volume.
     */
    if (CODEC_SetVolume(&codecHandle, kCODEC_PlayChannelHeadphoneLeft | kCODEC_PlayChannelHeadphoneRight,
                        DEMO_CODEC_VOLUME) != kStatus_Success)
    {
        assert(false);
    }

    initialize_wave(DEMO_AUDIO_SAMPLE_RATE, 1000, 10000, B16);

    PRINTF("Configure I2S\r\n");

    /*
     * masterSlave = kI2S_MasterSlaveNormalMaster;
     * mode = kI2S_ModeI2sClassic;
     * rightLow = false;
     * leftJust = false;
     * pdmData = false;
     * sckPol = false;
     * wsPol = false;
     * divider = 1;
     * oneChannel = false;
     * dataLength = 16;
     * frameLength = 32;
     * position = 0;
     * watermark = 4;
     * txEmptyZero = true;
     * pack48 = false;
     */
    I2S_TxGetDefaultConfig(&s_TxConfig);
    s_TxConfig.divider     = DEMO_I2S_CLOCK_DIVIDER;
    s_TxConfig.mode        = kI2S_ModeDspWsShort;
    s_TxConfig.wsPol       = true;
    // This value is fixed for the codec
    s_TxConfig.dataLength  = 32U;
    // Fix at 8 channels
    s_TxConfig.frameLength = 32U * 8U;
    s_TxConfig.position    = 1U;


    I2S_TxInit(DEMO_I2S_TX, &s_TxConfig);
    I2S_EnableSecondaryChannel(DEMO_I2S_TX, kI2S_SecondaryChannel1, false, 64+DEMO_TDM_DATA_START_POSITION);
    I2S_EnableSecondaryChannel(DEMO_I2S_TX, kI2S_SecondaryChannel2, false, 128+DEMO_TDM_DATA_START_POSITION);
    I2S_EnableSecondaryChannel(DEMO_I2S_TX, kI2S_SecondaryChannel3, false, 192+DEMO_TDM_DATA_START_POSITION);

    DMA_Init(DEMO_DMA);

    DMA_EnableChannel(DEMO_DMA, DEMO_I2S_TX_CHANNEL);
    DMA_SetChannelPriority(DEMO_DMA, DEMO_I2S_TX_CHANNEL, kDMA_ChannelPriority3);
    DMA_CreateHandle(&s_DmaTxHandle, DEMO_DMA, DEMO_I2S_TX_CHANNEL);

    buffer = (uint8_t *)populate_buffer(&xfer_size);
    xfer_size *=4;

    StartSoundPlayback();

    while (1)
    {
    }

    return 0;
}

static void StartSoundPlayback(void)
{
    PRINTF("Setup looping playback of sine wave\r\n");

    s_TxTransfer.data     = buffer;
    s_TxTransfer.dataSize = (size_t)xfer_size;

    I2S_TxTransferCreateHandleDMA(DEMO_I2S_TX, &s_TxHandle, &s_DmaTxHandle, TxCallback, NULL);
    /* need to queue two transmit buffers so when the first one
     * finishes transfer, the other immediatelly starts */
    I2S_TxTransferSendDMA(DEMO_I2S_TX, &s_TxHandle, s_TxTransfer);

    buffer = (uint8_t *)populate_buffer(&xfer_size);

    I2S_TxTransferSendDMA(DEMO_I2S_TX, &s_TxHandle, s_TxTransfer);
    buffer = (uint8_t *)populate_buffer(&xfer_size);

}

static void TxCallback(I2S_Type *base, i2s_dma_handle_t *handle, status_t completionStatus, void *userData)
{
    /* Enqueue the same original buffer all over again */
    s_TxTransfer.data     = buffer;
    s_TxTransfer.dataSize = (size_t)xfer_size*4;

    I2S_TxTransferSendDMA(base, handle, s_TxTransfer);
    buffer = (uint8_t *)populate_buffer(&xfer_size);
}
