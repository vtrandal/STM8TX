// -----------------------------------------------------------------------------
// Driver for Texas Instruments CC2500 radio
// This driver developed with thanks to the following projects:
//
// https://github.com/betaflight
// http://cleanflight.com/
// https://github.com/pascallanger/DIY-Multiprotocol-TX-Module
// -----------------------------------------------------------------------------

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "config.h"
#include "stm8l.h"
#include "util.h"
#include "spi.h"
#include "gpio.h"
#include "timer.h"
#include "channels.h"
#include "crc.h"
#include "adc.h"
#include "telem_structure.h"
#include "cc2500.h"
#include "buzzer.h"
#include "eeprom.h"

#if SUPPORT_CC2500

/** \addtogroup cc2500 Texas Instruments CC2500 radio module
@{ */

enum {
    CC2500_00_IOCFG2 = 0x00,   // GDO2 output pin configuration
    CC2500_01_IOCFG1 = 0x01,   // GDO1 output pin configuration
    CC2500_02_IOCFG0 = 0x02,   // GDO0 output pin configuration
    CC2500_03_FIFOTHR = 0x03,  // RX FIFO and TX FIFO thresholds
    CC2500_04_SYNC1 = 0x04,    // Sync word, high byte
    CC2500_05_SYNC0 = 0x05,    // Sync word, low byte
    CC2500_06_PKTLEN = 0x06,   // Packet length
    CC2500_07_PKTCTRL1 = 0x07, // Packet automation control
    CC2500_08_PKTCTRL0 = 0x08, // Packet automation control
    CC2500_09_ADDR = 0x09,     // Device address
    CC2500_0A_CHANNR = 0x0A,   // Channel number
    CC2500_0B_FSCTRL1 = 0x0B,  // Frequency synthesizer control
    CC2500_0C_FSCTRL0 = 0x0C,  // Frequency synthesizer control
    CC2500_0D_FREQ2 = 0x0D,    // Frequency control word, high byte
    CC2500_0E_FREQ1 = 0x0E,    // Frequency control word, middle byte
    CC2500_0F_FREQ0 = 0x0F,    // Frequency control word, low byte
    CC2500_10_MDMCFG4 = 0x10,  // Modem configuration
    CC2500_11_MDMCFG3 = 0x11,  // Modem configuration
    CC2500_12_MDMCFG2 = 0x12,  // Modem configuration
    CC2500_13_MDMCFG1 = 0x13,  // Modem configuration
    CC2500_14_MDMCFG0 = 0x14,  // Modem configuration
    CC2500_15_DEVIATN = 0x15,  // Modem deviation setting
    CC2500_16_MCSM2 = 0x16,    // Main Radio Cntrl State Machine config
    CC2500_17_MCSM1 = 0x17,    // Main Radio Cntrl State Machine config
    CC2500_18_MCSM0 = 0x18,    // Main Radio Cntrl State Machine config
    CC2500_19_FOCCFG = 0x19,   // Frequency Offset Compensation config
    CC2500_1A_BSCFG = 0x1A,    // Bit Synchronization configuration
    CC2500_1B_AGCCTRL2 = 0x1B, // AGC control
    CC2500_1C_AGCCTRL1 = 0x1C, // AGC control
    CC2500_1D_AGCCTRL0 = 0x1D, // AGC control
    CC2500_1E_WOREVT1 = 0x1E,  // High byte Event 0 timeout
    CC2500_1F_WOREVT0 = 0x1F,  // Low byte Event 0 timeout
    CC2500_20_WORCTRL = 0x20,  // Wake On Radio control
    CC2500_21_FREND1 = 0x21,   // Front end RX configuration
    CC2500_22_FREND0 = 0x22,   // Front end TX configuration
    CC2500_23_FSCAL3 = 0x23,   // Frequency synthesizer calibration
    CC2500_24_FSCAL2 = 0x24,   // Frequency synthesizer calibration
    CC2500_25_FSCAL1 = 0x25,   // Frequency synthesizer calibration
    CC2500_26_FSCAL0 = 0x26,   // Frequency synthesizer calibration
    CC2500_27_RCCTRL1 = 0x27,  // RC oscillator configuration
    CC2500_28_RCCTRL0 = 0x28,  // RC oscillator configuration
    CC2500_29_FSTEST = 0x29,   // Frequency synthesizer cal control
    CC2500_2A_PTEST = 0x2A,    // Production test
    CC2500_2B_AGCTEST = 0x2B,  // AGC test
    CC2500_2C_TEST2 = 0x2C,    // Various test settings
    CC2500_2D_TEST1 = 0x2D,    // Various test settings
    CC2500_2E_TEST0 = 0x2E,    // Various test settings

    // Status registers
    CC2500_30_PARTNUM = 0x30,    // Part number
    CC2500_31_VERSION = 0x31,    // Current version number
    CC2500_32_FREQEST = 0x32,    // Frequency offset estimate
    CC2500_33_LQI = 0x33,        // Demodulator estimate for link quality
    CC2500_34_RSSI = 0x34,       // Received signal strength indication
    CC2500_35_MARCSTATE = 0x35,  // Control state machine state
    CC2500_36_WORTIME1 = 0x36,   // High byte of WOR timer
    CC2500_37_WORTIME0 = 0x37,   // Low byte of WOR timer
    CC2500_38_PKTSTATUS = 0x38,  // Current GDOx status and packet status
    CC2500_39_VCO_VC_DAC = 0x39, // Current setting from PLL cal module
    CC2500_3A_TXBYTES = 0x3A,    // Underflow and # of bytes in TXFIFO
    CC2500_3B_RXBYTES = 0x3B,    // Overflow and # of bytes in RXFIFO

    // Multi byte memory locations
    CC2500_3E_PATABLE = 0x3E,
    CC2500_3F_TXFIFO = 0x3F,
    CC2500_3F_RXFIFO = 0x3F
};

// Definitions for burst/single access to registers
#define CC2500_WRITE_SINGLE 0x00
#define CC2500_WRITE_BURST 0x40
#define CC2500_READ_SINGLE 0x80
#define CC2500_READ_BURST 0xC0

// Strobe commands
#define CC2500_SRES 0x30 // Reset chip.
#define CC2500_SFSTXON                                                         \
    0x31 // Enable and calibrate frequency synthesizer (if MCSM0.FS_AUTOCAL=1).
         // If in RX/TX: Go to a wait state where only the synthesizer is
         // running (for quick RX / TX turnaround).
#define CC2500_SXOFF 0x32 // Turn off crystal oscillator.
#define CC2500_SCAL 0x33  // Calibrate frequency synthesizer and turn it off
                          // (enables quick start).
#define CC2500_SRX                                                             \
    0x34 // Enable RX. Perform calibration first if coming from IDLE and
         // MCSM0.FS_AUTOCAL=1.
#define CC2500_STX                                                             \
    0x35 // In IDLE state: Enable TX. Perform calibration first if
         // MCSM0.FS_AUTOCAL=1. If in RX state and CCA is enabled:
         // Only go to TX if channel is clear.
#define CC2500_SIDLE                                                           \
    0x36 // Exit RX / TX, turn off frequency synthesizer and exit
         // Wake-On-Radio mode if applicable.
#define CC2500_SAFC 0x37 // Perform AFC adjustment of the frequency synthesizer
#define CC2500_SWOR 0x38 // Start automatic RX polling sequence (Wake-on-Radio)
#define CC2500_SPWD 0x39 // Enter power down mode when CSn goes high.
#define CC2500_SFRX 0x3A // Flush the RX FIFO buffer.
#define CC2500_SFTX 0x3B // Flush the TX FIFO buffer.
#define CC2500_SWORRST 0x3C // Reset real time clock.
#define CC2500_SNOP                                                            \
    0x3D // No operation. May be used to pad strobe commands to two
         // bytes for simpler software.
//----------------------------------------------------------------------------------
// Chip Status Byte
//----------------------------------------------------------------------------------

// Bit fields in the chip status byte
#define CC2500_STATUS_CHIP_RDYn_BM 0x80
#define CC2500_STATUS_STATE_BM 0x70
#define CC2500_STATUS_FIFO_BYTES_AVAILABLE_BM 0x0F

// Chip states
#define CC2500_STATE_IDLE 0x00
#define CC2500_STATE_RX 0x10
#define CC2500_STATE_TX 0x20
#define CC2500_STATE_FSTXON 0x30
#define CC2500_STATE_CALIBRATE 0x40
#define CC2500_STATE_SETTLING 0x50
#define CC2500_STATE_RX_OVERFLOW 0x60
#define CC2500_STATE_TX_UNDERFLOW 0x70

//----------------------------------------------------------------------------------
// Other register bit fields
//----------------------------------------------------------------------------------
#define CC2500_LQI_CRC_OK_BM 0x80
#define CC2500_LQI_EST_BM 0x7F

static struct stats {
    uint32_t bad_packets;
    uint32_t recv_errors;
    uint32_t recv_packets;
    uint32_t lost_packets;
    uint32_t timeouts;
} stats;

struct telem_status t_status;
uint8_t telem_ack_value;

#define NUM_CHANNELS 47
static uint8_t calData[NUM_CHANNELS][3];
static uint8_t bindTxId[2];
static int8_t  bindOffset;
static uint8_t bindHopData[NUM_CHANNELS];
static uint8_t channr;
static uint8_t chanskip;
static uint8_t rxnum;
static uint16_t bindcount;


static const uint16_t CRCTable[] = {
        0x0000,0x1189,0x2312,0x329b,0x4624,0x57ad,0x6536,0x74bf,
        0x8c48,0x9dc1,0xaf5a,0xbed3,0xca6c,0xdbe5,0xe97e,0xf8f7,
        0x1081,0x0108,0x3393,0x221a,0x56a5,0x472c,0x75b7,0x643e,
        0x9cc9,0x8d40,0xbfdb,0xae52,0xdaed,0xcb64,0xf9ff,0xe876,
        0x2102,0x308b,0x0210,0x1399,0x6726,0x76af,0x4434,0x55bd,
        0xad4a,0xbcc3,0x8e58,0x9fd1,0xeb6e,0xfae7,0xc87c,0xd9f5,
        0x3183,0x200a,0x1291,0x0318,0x77a7,0x662e,0x54b5,0x453c,
        0xbdcb,0xac42,0x9ed9,0x8f50,0xfbef,0xea66,0xd8fd,0xc974,
        0x4204,0x538d,0x6116,0x709f,0x0420,0x15a9,0x2732,0x36bb,
        0xce4c,0xdfc5,0xed5e,0xfcd7,0x8868,0x99e1,0xab7a,0xbaf3,
        0x5285,0x430c,0x7197,0x601e,0x14a1,0x0528,0x37b3,0x263a,
        0xdecd,0xcf44,0xfddf,0xec56,0x98e9,0x8960,0xbbfb,0xaa72,
        0x6306,0x728f,0x4014,0x519d,0x2522,0x34ab,0x0630,0x17b9,
        0xef4e,0xfec7,0xcc5c,0xddd5,0xa96a,0xb8e3,0x8a78,0x9bf1,
        0x7387,0x620e,0x5095,0x411c,0x35a3,0x242a,0x16b1,0x0738,
        0xffcf,0xee46,0xdcdd,0xcd54,0xb9eb,0xa862,0x9af9,0x8b70,
        0x8408,0x9581,0xa71a,0xb693,0xc22c,0xd3a5,0xe13e,0xf0b7,
        0x0840,0x19c9,0x2b52,0x3adb,0x4e64,0x5fed,0x6d76,0x7cff,
        0x9489,0x8500,0xb79b,0xa612,0xd2ad,0xc324,0xf1bf,0xe036,
        0x18c1,0x0948,0x3bd3,0x2a5a,0x5ee5,0x4f6c,0x7df7,0x6c7e,
        0xa50a,0xb483,0x8618,0x9791,0xe32e,0xf2a7,0xc03c,0xd1b5,
        0x2942,0x38cb,0x0a50,0x1bd9,0x6f66,0x7eef,0x4c74,0x5dfd,
        0xb58b,0xa402,0x9699,0x8710,0xf3af,0xe226,0xd0bd,0xc134,
        0x39c3,0x284a,0x1ad1,0x0b58,0x7fe7,0x6e6e,0x5cf5,0x4d7c,
        0xc60c,0xd785,0xe51e,0xf497,0x8028,0x91a1,0xa33a,0xb2b3,
        0x4a44,0x5bcd,0x6956,0x78df,0x0c60,0x1de9,0x2f72,0x3efb,
        0xd68d,0xc704,0xf59f,0xe416,0x90a9,0x8120,0xb3bb,0xa232,
        0x5ac5,0x4b4c,0x79d7,0x685e,0x1ce1,0x0d68,0x3ff3,0x2e7a,
        0xe70e,0xf687,0xc41c,0xd595,0xa12a,0xb0a3,0x8238,0x93b1,
        0x6b46,0x7acf,0x4854,0x59dd,0x2d62,0x3ceb,0x0e70,0x1ff9,
        0xf78f,0xe606,0xd49d,0xc514,0xb1ab,0xa022,0x92b9,0x8330,
        0x7bc7,0x6a4e,0x58d5,0x495c,0x3de3,0x2c6a,0x1ef1,0x0f78
};

static void radio_init_hw(void);

void radio_init(void)
{
    printf("radio_init\n");

    // setup PACTL
    gpio_config(RADIO_PACTL, GPIO_OUTPUT_PUSHPULL);
    gpio_clear(RADIO_PACTL);

    // setup PACTL
    gpio_config(RADIO_PACTL, GPIO_OUTPUT_PUSHPULL);
    gpio_clear(RADIO_PACTL);

    // setup radio CE
    gpio_config(RADIO_CE, GPIO_OUTPUT_PUSHPULL);
    gpio_set(RADIO_CE);

    // setup interrupt
    gpio_config(RADIO_INT, GPIO_INPUT_FLOAT_IRQ);

    radio_init_hw();
}

void cc2500_ReadFifo(uint8_t *dpbuffer, uint8_t len)
{
    const uint8_t reg = CC2500_3F_RXFIFO | CC2500_READ_BURST;
    spi_force_chip_select(true);
    spi_write(1, &reg);
    spi_read(len, dpbuffer);
    spi_force_chip_select(false);
}

void cc2500_WriteFifo(const uint8_t *dpbuffer, uint8_t len)
{
    const uint8_t reg = CC2500_3F_TXFIFO | CC2500_WRITE_BURST;
    spi_force_chip_select(true);
    spi_write(1, &reg);
    spi_write(len, dpbuffer);
    spi_force_chip_select(false);
}

/*
  write one register value
 */
static void cc2500_WriteReg(uint8_t reg, uint8_t value)
{
    uint8_t d[2] = { reg | CC2500_WRITE_SINGLE, value };
    spi_force_chip_select(true);
    spi_write(2, d);
    spi_force_chip_select(false);
}

void cc2500_SetPower(uint8_t power)
{
    const uint8_t patable[8] = {
        0xC5, // -12dbm
        0x97, // -10dbm
        0x6E, // -8dbm
        0x7F, // -6dbm
        0xA9, // -4dbm
        0xBB, // -2dbm
        0xFE, // 0dbm
        0xFF  // 1.5dbm
    };
    if (power > 7) {
        power = 7;
    }
    cc2500_WriteReg(CC2500_3E_PATABLE, patable[power]);
}

/*
  read one register value
 */
static uint8_t cc2500_ReadReg(uint8_t reg)
{
    uint8_t d[2] = { reg | CC2500_READ_SINGLE, 0 };
    spi_transfer(2, d, d);
    return d[1];
}

static uint8_t cc2500_Strobe(uint8_t address)
{
    uint8_t status = 0;
    spi_transfer(1, &address, &status);
    return status;
}

static const struct {
    uint8_t reg;
    uint8_t value;
} radio_config[] = {
    {CC2500_02_IOCFG0,   0x01}, // GD0 high on RXFIFO filled or end of packet
    {CC2500_17_MCSM1,    0x0C}, // stay in RX on packet receive, CCA always, TX -> IDLE
    {CC2500_18_MCSM0,    0x18}, // XOSC expire 64, cal on IDLE -> TX or RX
    {CC2500_06_PKTLEN,   0x1E}, // packet length 30
    {CC2500_07_PKTCTRL1, 0x04}, // enable RSSI+LQI, no addr check, no autoflush, PQT=0
    {CC2500_08_PKTCTRL0, 0x01}, // var length mode, no CRC, FIFO enable, no whitening
    {CC2500_3E_PATABLE,  0xFF}, // ?? what are we doing to PA table here?
    {CC2500_0B_FSCTRL1,  0x0A}, // IF=253.90625kHz assuming 26MHz crystal
    {CC2500_0C_FSCTRL0,  0x00}, // freqoffs = 0
    {CC2500_0D_FREQ2,    0x5C}, // freq control high
    {CC2500_0E_FREQ1,    0x76}, // freq control middle
    {CC2500_0F_FREQ0,    0x27}, // freq control low
    {CC2500_10_MDMCFG4,  0x7B}, // data rate control
    {CC2500_11_MDMCFG3,  0x61}, // data rate control
    {CC2500_12_MDMCFG2,  0x13}, // 30/32 sync word bits, no manchester, GFSK, DC filter enabled
    {CC2500_13_MDMCFG1,  0x23}, // chan spacing exponent 3, preamble 4 bytes, FEC disabled
    {CC2500_14_MDMCFG0,  0x7A}, // chan spacing 299.926757kHz for 26MHz crystal
    {CC2500_15_DEVIATN,  0x51}, // modem deviation 25.128906kHz for 26MHz crystal
    {CC2500_19_FOCCFG,   0x16}, // frequency offset compensation
    {CC2500_1A_BSCFG,    0x6C}, // bit sync config
    {CC2500_1B_AGCCTRL2, 0x03}, // target amplitude 33dB
    {CC2500_1C_AGCCTRL1, 0x40}, // AGC control 2
    {CC2500_1D_AGCCTRL0, 0x91}, // AGC control 0
    {CC2500_21_FREND1,   0x56}, // frontend config1
    {CC2500_22_FREND0,   0x10}, // frontend config0
    {CC2500_23_FSCAL3,   0xA9}, // frequency synth cal3
    {CC2500_24_FSCAL2,   0x0A}, // frequency synth cal2
    {CC2500_25_FSCAL1,   0x00}, // frequency synth cal1
    {CC2500_26_FSCAL0,   0x11}, // frequency synth cal0
    //{CC2500_29_FSTEST,   0x59}, disabled FSTEST write
    {CC2500_2C_TEST2,    0x88}, // test settings
    {CC2500_2D_TEST1,    0x31}, // test settings
    {CC2500_2E_TEST0,    0x0B}, // test settings
    {CC2500_03_FIFOTHR,  0x07}, // TX fifo threashold 33, RX fifo threshold 32
    {CC2500_09_ADDR,     0x00}, // device address 0 (broadcast)
};

/*
  reset radio
 */
static bool cc2500_Reset(void)
{
    cc2500_Strobe(CC2500_SRES);
    delay_ms(1);
    return cc2500_ReadReg(CC2500_0E_FREQ1) == 0xC4; // check if reset
}

static void initialiseData(uint8_t adr)
{
    cc2500_WriteReg(CC2500_0C_FSCTRL0, 0);
    cc2500_WriteReg(CC2500_18_MCSM0, 0x8);
    cc2500_WriteReg(CC2500_09_ADDR, adr ? 0x03 : bindTxId[0]);
    cc2500_WriteReg(CC2500_07_PKTCTRL1, 0x0D); // address check, no broadcast, autoflush, status enable
    cc2500_WriteReg(CC2500_19_FOCCFG, 0x16);
}

static void setChannel(uint8_t channel)
{
    cc2500_Strobe(CC2500_SIDLE);
    cc2500_WriteReg(CC2500_23_FSCAL3, calData[channel][0]);
    cc2500_WriteReg(CC2500_24_FSCAL2, calData[channel][1]);
    cc2500_WriteReg(CC2500_25_FSCAL1, calData[channel][2]);
    cc2500_WriteReg(CC2500_0A_CHANNR, bindHopData[channel]);
}

/*
  create hopping table - based on DIY-Multiprotocol-TX-Module implementation
 */
static void setup_hopping_table(void)
{
    uint8_t val;
    uint8_t channel = bindTxId[0] & 0x07;
    uint8_t channel_spacing = bindTxId[1];
    uint8_t i;

    //Filter bad tables
    if (channel_spacing < 0x02) {
        channel_spacing += 0x02;
    }
    if (channel_spacing>0xE9) {
        channel_spacing -= 0xE7;
    }
    if (channel_spacing % NUM_CHANNELS == 0) {
        channel_spacing++;
    }

    bindHopData[0] = channel;
    for (i=1; i<NUM_CHANNELS; i++) {
        channel = (channel+channel_spacing) % 0xEB;
        val=channel;
        if ((val==0x00) || (val==0x5A) || (val==0xDC)) {
            val++;
        }
        bindHopData[i] = val;
    }
}

/*
  initialise the radio
 */
static void radio_init_hw(void)
{
    uint8_t i;

    printf("cc2500: radio_init_hw starting\n");
    while (cc2500_ReadReg(CC2500_30_PARTNUM | CC2500_READ_BURST) != 0x80 ||
           cc2500_ReadReg(CC2500_31_VERSION | CC2500_READ_BURST) != 0x03) {
        printf("Bad radio IDs\n");
        delay_ms(200);
    }
    printf("cc2500: found radio\n");

    if (!cc2500_Reset()) {
        printf("cc2500: failed reset\n");
    }

    for (i=0; i<sizeof(radio_config)/sizeof(radio_config[0]); i++) {
        cc2500_WriteReg(radio_config[i].reg, radio_config[i].value);
    }
    cc2500_Strobe(CC2500_SIDLE);	// Go to idle...

    // need to read radio ID from radio
    bindTxId[0] = 15;
    bindTxId[1] = 20;

    chanskip = 1;

    setup_hopping_table();

    for (i=0;i<NUM_CHANNELS;i++) {
        cc2500_Strobe(CC2500_SIDLE);
        cc2500_WriteReg(CC2500_0A_CHANNR, bindHopData[i]);
        cc2500_Strobe(CC2500_SCAL);
        delay_ms(1);
        calData[i][0] = cc2500_ReadReg(CC2500_23_FSCAL3);
        calData[i][1] = cc2500_ReadReg(CC2500_24_FSCAL2);
        calData[i][2] = cc2500_ReadReg(CC2500_25_FSCAL1);
    }
    delay_ms(10);
    cc2500_Strobe(CC2500_SIDLE);
    delay_ms(10);

    // setup for sending bind packets
    initialiseData(1);
}

static uint16_t calc_crc(uint8_t *data, uint8_t len)
{
    uint16_t crc = 0;
    uint8_t i;
    for (i=0; i < len; i++) {
        crc = (crc<<8) ^ (CRCTable[((uint8_t)(crc>>8) ^ *data++) & 0xFF]);
    }
    return crc;
}

static void send_packet(uint8_t len, const uint8_t *packet)
{
    gpio_set(RADIO_PACTL);
    cc2500_Strobe(CC2500_SFTX);
    cc2500_WriteFifo(packet, len);
    cc2500_Strobe(CC2500_STX);
}

static void send_normal_packet(void);

static void start_receive(void)
{
    gpio_clear(RADIO_PACTL);
    channr = (channr + chanskip) % 47;
    cc2500_Strobe(CC2500_SIDLE);
    setChannel(channr);
    cc2500_Strobe(CC2500_SRX);
    timer_call_after_ms(3, send_normal_packet);
}

static void send_normal_packet(void)
{
    uint8_t packet[30];
    uint8_t i, ofs;
    uint16_t lcrc;

    memset(packet, 0, sizeof(packet));

    packet[0] = sizeof(packet)-1;
    packet[1] = bindTxId[0];
    packet[2] = bindTxId[1];
    packet[3] = 0x02;

    packet[4] = (chanskip<<6)|channr;
    packet[5] = (chanskip>>2);
    packet[6] = rxnum;
    packet[7] = 0; // packet type, 0=normal pkt
    packet[8] = 0;

    for (i = 0, ofs=9; i <8 ; i+=2, ofs += 3) {
        uint16_t chan_0 = channel_value(i);
        uint16_t chan_1 = channel_value(i+1);
        packet[ofs]   = chan_0 & 0xFF;
        packet[ofs+1] = (((chan_0>>8) & 0x0F)|(chan_1 << 4));
        packet[ofs+2] = chan_1>>4;
    }

    lcrc = calc_crc(&packet[3], sizeof(packet)-5);
    packet[sizeof(packet)-2] = lcrc>>8;
    packet[sizeof(packet)-1] = lcrc;

    cc2500_Strobe(CC2500_SIDLE);
    cc2500_Strobe(CC2500_SFRX);
    send_packet(sizeof(packet), packet);

    timer_call_after_ms(6, start_receive);
}

/*
  send one bind packet
 */
static void send_bind_packet(void)
{
    uint8_t packet[30]; // US packet is 0x1D (29) long
    static uint8_t idx;
    uint16_t lcrc;
    uint8_t i;

    memset(packet, 0, sizeof(packet));

    packet[0] = sizeof(packet)-1; // US (FCC) version
    packet[1] = 0x03;
    packet[2] = 0x01;
    packet[3] = bindTxId[0];
    packet[4] = bindTxId[1];
    packet[5] = idx;
    for (i=0; i<5; i++) {
        if (idx + i < 47) {
            packet[6+i] = bindHopData[idx+i];
        }
    }
    packet[11] = 0x02;
    packet[12] = 0; // rxnum

    lcrc = calc_crc(&packet[3], sizeof(packet)-5);

    packet[sizeof(packet)-2] = lcrc >> 8;
    packet[sizeof(packet)-1] = lcrc;

    idx += 5;
    if (idx >= 50) {
        idx -= 50;
    }

    cc2500_Strobe(CC2500_SIDLE);
    cc2500_WriteReg(CC2500_0A_CHANNR, 0);
    send_packet(sizeof(packet), packet);

    bindcount++;
    if (bindcount > 500) {
        // send every 9ms
        timer_call_after_ms(9, send_normal_packet);
    } else {
        // send bind every 9ms
        timer_call_after_ms(9, send_bind_packet);
    }
}


/*
  IRQ handler
 */
void radio_irq(void)
{
}

/*
  setup radio for bind on send side
 */
void radio_start_bind_send(bool use_dsm2)
{
}


/*
  setup radio for FCC test
 */
void radio_start_FCC_test(void)
{
}


/*
  setup radio for normal sending
 */
void radio_start_send(bool use_dsm2)
{
    printf("radio_start_send\n");
    setChannel(0);
    timer_call_after_ms(2, send_bind_packet);
}

/*
  setup for factory mode
 */
void radio_start_factory_test(uint8_t test_mode)
{
}

uint8_t get_tx_power(void)
{
    return 0;
}

/*
  called once per main loop to get packets rates and average RSSI values
 */
void radio_set_pps_rssi(void)
{
}

/*
  get the average RSSI from telemetry packets
 */
uint8_t get_telem_rssi(void)
{
    return 0;
}

/*
  get the send rate in PPS
 */
uint8_t get_send_pps(void)
{
    return 0;
}

/*
  get the telemetry receive rate in pps
 */
uint8_t get_telem_pps(void)
{
    return 0;
}

/*
  switch between 3 FCC test modes
 */
void radio_next_FCC_power(void)
{
}

/*
  get FCC test chan
 */
int8_t get_FCC_chan(void)
{
    return 0;
}

/*
  get FCC test power
 */
uint8_t get_FCC_power(void)
{
    return 0;
}

/*
  set CW mode for FCC testing
 */
void radio_set_CW_mode(bool cw)
{
}

void radio_change_FCC_channel(int8_t change)
{
}

void radio_FCC_toggle_scan(void)
{
}

#endif

