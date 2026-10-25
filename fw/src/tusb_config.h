#ifndef RPJTAG_TUSB_CONFIG_H
#define RPJTAG_TUSB_CONFIG_H

#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU OPT_MCU_RP2040
#endif

#define CFG_TUSB_RHPORT0_MODE OPT_MODE_DEVICE
#define CFG_TUSB_OS OPT_OS_PICO
#define CFG_TUD_ENABLED 1
#define CFG_TUD_BOS 1
#define CFG_TUD_ENDPOINT0_SIZE 64
#define CFG_TUD_CDC 1
#define CFG_TUD_CDC_RX_BUFSIZE 64
#define CFG_TUD_CDC_TX_BUFSIZE 64
#define CFG_TUD_CDC_EP_BUFSIZE 64
#define CFG_TUD_VENDOR 2
#define CFG_TUD_VENDOR_EPSIZE 64
#define CFG_TUD_VENDOR_RX_BUFSIZE 256
// 2 KB: a stream frame of the longest boundary register (4,096 bits) fits, and a short host stall
// doesn't drop captures (256 bytes held four XC7S15 frames; GateLab saw 97 drops at 2,372/s).
#define CFG_TUD_VENDOR_TX_BUFSIZE 2048

#endif