/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * RTL8814AU — Master header for the Realtek RTL8814AU native USB WiFi driver.
 *
 * Contains hardware register addresses, bit definitions, descriptor formats,
 * USB device IDs, and shared constants. All register addresses are sourced from
 * the reference driver (ulli-kroll/rtl8814au, include/rtl8814a_spec.h).
 *
 * Register I/O: All registers are accessed via USB vendor control transfers.
 * There is no memory-mapped I/O on USB devices. See RegisterIO.h for the
 * read/write interface.
 */
#ifndef RTL8814AU_H
#define RTL8814AU_H


#include <SupportDefs.h>


// ---------------------------------------------------------------------------
// Driver identity
// ---------------------------------------------------------------------------

#define RTL8814AU_DRIVER_NAME		"rtl8814au"
#define RTL8814AU_DEVICE_PATH_BASE	"net/" RTL8814AU_DRIVER_NAME

// Maximum number of simultaneously attached adapters
static const uint32 kMaxDeviceCount = 8;


// ---------------------------------------------------------------------------
// USB device IDs
// ---------------------------------------------------------------------------

struct RTL8814AUDeviceID {
	uint16	vendorID;
	uint16	productID;
	const char*	name;
};

// Devices known to use the RTL8814AU chipset. This table is checked during
// USB device_added() to decide whether we claim a device.
static const RTL8814AUDeviceID kSupportedDevices[] = {
	{ 0x0b05, 0x1817, "ASUS USB-AC68" },
	{ 0x7392, 0xa833, "Edimax AC1750" },
	{ 0x0b05, 0x1852, "ASUS USB-AC68 (rev 2)" },
	{ 0x0846, 0x9054, "Netgear A7000" },
	{ 0x2001, 0x331a, "D-Link DWA-192" },
	{ 0x2357, 0x0106, "TP-Link Archer T9UH" },
	{ 0x20f4, 0x809a, "TRENDnet TEW-809UB" },
	{ 0x056e, 0x400b, "Elecom WDB-867DU3S" },
	{ 0x056e, 0x400d, "Elecom WDC-867DU3S" },
};

static const uint32 kSupportedDeviceCount
	= sizeof(kSupportedDevices) / sizeof(kSupportedDevices[0]);


// ---------------------------------------------------------------------------
// Chip identification
// ---------------------------------------------------------------------------

static const uint32 kRTL8814AU_ChipID		= 0x8814;
static const uint32 kMaxFirmwareSize		= 0x18000;	// 96 KB

// TX buffer: 2048 pages of 128 bytes each = 256 KB total
static const uint32 kTxPageSize				= 128;
static const uint32 kTxPageCount			= 2048;
static const uint32 kTxBufferSize			= kTxPageSize * kTxPageCount;

// RX buffer size in hardware
static const uint32 kRxBufferSize			= 0x5C00;	// ~23 KB

// Station tracking and security
static const uint32 kMaxMacIDCount			= 128;
static const uint32 kMaxSecurityCamEntries	= 64;

// H2C mailbox system
static const uint32 kH2CMailboxCount		= 4;
static const uint32 kH2CCommandSize		= 7;		// 4 standard + 3 ext

// EFUSE
static const uint32 kEfuseTotalSize			= 1024;
static const uint32 kEfuseMapSize			= 512;

// RF paths — the RTL8814AU has 4 independent radio chains
static const uint32 kRfPathCount			= 4;

// AMPDU parameters
static const uint8 kAmpduMaxTime			= 0x70;
static const uint32 kAmpduMaxLength			= 0x1FFFF;	// 128 KB

// Firmware path on Haiku filesystem
#define RTL8814AU_FIRMWARE_PATH \
	"/boot/system/data/firmware/rtl8814au/rtl8814aufw.bin"


// ---------------------------------------------------------------------------
// USB endpoint configuration
//
// The RTL8814AU exposes 5 endpoints:
//   3 bulk OUT (TX, priority-mapped)
//   1 bulk IN  (RX data + status)
//   1 interrupt IN (C2H firmware events)
// ---------------------------------------------------------------------------

static const uint32 kBulkOutEndpointCount	= 3;
static const uint32 kBulkInEndpointCount	= 1;
static const uint32 kInterruptInEndpointCount = 1;

// USB bulk transfer buffer sizes
static const uint32 kUsbTxBufferSize		= 16384;	// 16 KB per bulk OUT
static const uint32 kUsbRxBufferSize		= 32768;	// 32 KB for bulk IN
static const uint32 kUsbInterruptBufferSize	= 64;		// for interrupt IN

// Queue-to-endpoint mapping (WMM priority → bulk OUT pipe index)
enum TxQueueSelect {
	kTxQueueVO		= 0,	// Voice — highest priority → OUT #1
	kTxQueueVI		= 0,	// Video — high priority → OUT #1
	kTxQueueBE		= 1,	// Best effort — normal → OUT #2
	kTxQueueBK		= 1,	// Background — low priority → OUT #2
	kTxQueueMGT		= 2,	// Management → OUT #3
	kTxQueueCMD		= 2,	// H2C commands → OUT #3
	kTxQueueBCN		= 2,	// Beacons → OUT #3
	kTxQueueHIGH	= 0,	// High priority → OUT #1
};


// ---------------------------------------------------------------------------
// Register addresses — System Configuration (0x0000 – 0x00FF)
//
// Power control, clock generation, GPIO, analog front-end, and interrupt
// management. These registers must be configured before any other block.
// ---------------------------------------------------------------------------

// System isolation and function enable
static const uint16 kRegSysCfg				= 0x00F0;
static const uint16 kRegSysFuncEn			= 0x0002;
static const uint16 kRegAfeCtrl1			= 0x0024;
static const uint16 kRegAfeCtrl2			= 0x0028;
static const uint16 kRegAfeCtrl3			= 0x002C;
static const uint16 kRegRsvCtrl			= 0x001C;
static const uint16 kRegApsRsvd			= 0x001E;

// GPIO and pad control
static const uint16 kRegGpioMuxCfg			= 0x0040;
static const uint16 kRegGpioPinCtrl		= 0x0044;
static const uint16 kRegGpioIntm			= 0x0048;
static const uint16 kRegLedCfg				= 0x004C;

// Power sequence control
static const uint16 kRegPwrData			= 0x0038;
static const uint16 kRegCalTimer			= 0x003C;

// SYS_FUNC_EN bit definitions — controls which blocks are powered on
static const uint16 kSysFuncEnBBRSTB		= (1 << 0);
static const uint16 kSysFuncEnBBGlbRst		= (1 << 1);
static const uint16 kSysFuncEnUSBA			= (1 << 2);
static const uint16 kSysFuncEnUPLL			= (1 << 3);
static const uint16 kSysFuncEnUSBD			= (1 << 4);
static const uint16 kSysFuncEnCpuEn		= (1 << 12);  // Lexra 3081 MCU
static const uint16 kSysFuncEnDcore			= (1 << 13);
static const uint16 kSysFuncEnELDR			= (1 << 14);
static const uint16 kSysFuncEnHWPDN			= (1 << 15);

// Host interrupt mask and status (set 0 and set 1)
static const uint16 kRegHIMR0				= 0x00B0;
static const uint16 kRegHISR0				= 0x00B4;
static const uint16 kRegHIMR1				= 0x00B8;
static const uint16 kRegHISR1				= 0x00BC;

// HIMR0 bit definitions
static const uint32 kHIMR0_RxOK			= (1 << 0);
static const uint32 kHIMR0_RxErr			= (1 << 1);
static const uint32 kHIMR0_TxOK_VO			= (1 << 4);
static const uint32 kHIMR0_TxOK_VI			= (1 << 5);
static const uint32 kHIMR0_TxOK_BE			= (1 << 6);
static const uint32 kHIMR0_TxOK_BK			= (1 << 7);
static const uint32 kHIMR0_TxBcnOK			= (1 << 8);
static const uint32 kHIMR0_TxBcnErr		= (1 << 9);
static const uint32 kHIMR0_C2HCmd			= (1 << 16);
static const uint32 kHIMR0_CPWM			= (1 << 20);

// Multifunction control
static const uint16 kRegMultiFuncCtrl		= 0x0068;


// ---------------------------------------------------------------------------
// Register addresses — Firmware Control (0x0080 area)
//
// The RTL8814AU uses a Lexra 3081 MIPS-derived MCU. Firmware is loaded via
// page-based register writes: the host selects a 4 KB page and writes data
// to 0x1000–0x1FFF, then advances to the next page. After all pages are
// written, the MCU validates a checksum and signals readiness.
//
// Reference: rtl8814a_hal_init.c — FirmwareDownload8814A(),
//            _FWDownloadEnable_8814A(), _WriteFW_8814A(), _FWFreeToGo8814A()
// ---------------------------------------------------------------------------

static const uint16 kRegMcuFwDl				= 0x0080;

// kRegMcuFwDl bit definitions (32-bit register at 0x0080)
// Byte 0 (0x0080):
static const uint32 kMcuFwDlEn				= (1 << 0);	// FW download enable
static const uint32 kMcuFwDlRdy			= (1 << 1);	// Set by host after write
static const uint32 kMcuFwDlChksumRpt		= (1 << 2);	// Checksum OK from MCU
static const uint32 kMcuWintiniRdy			= (1 << 6);	// FW init complete
// Byte 2 (0x0082):
static const uint32 kMcuFwDlPageShift		= 16;			// Page number in [18:16]
static const uint32 kMcuFwDlPageMask		= (0x07 << 16);
static const uint32 kMcuRst8051			= (1 << 19);	// MCU running (clear=reset)

// Firmware page-based download constants
static const uint16 kFwStartAddress			= 0x1000;	// Write target per page
static const uint32 kFwPageSize				= 4096;		// 4 KB per page
static const uint32 kFwHeaderSize			= 32;		// RT_8814A_FIRMWARE_HDR

// Firmware polling
static const uint32 kFirmwarePollAttempts	= 6000;
static const bigtime_t kFirmwarePollDelay	= 5;		// 5 µs between polls


// ---------------------------------------------------------------------------
// Register addresses — MAC General Configuration (0x0100 – 0x01FF)
//
// Command register, packet buffer control, DMA configuration, and the
// H2C/C2H mailbox interface for host-firmware communication.
// ---------------------------------------------------------------------------

static const uint16 kRegCR					= 0x0100;
static const uint16 kRegPBP				= 0x0104;
static const uint16 kRegTrxDmaCfg			= 0x010C;
static const uint16 kRegTrxFF_BNDY			= 0x0114;

// CR (command register) bit definitions
static const uint32 kCR_HCI_TxDMA_En		= (1 << 0);
static const uint32 kCR_HCI_RxDMA_En		= (1 << 1);
static const uint32 kCR_TxDMA_En			= (1 << 2);
static const uint32 kCR_RxDMA_En			= (1 << 3);
static const uint32 kCR_Protocol_En			= (1 << 4);
static const uint32 kCR_Schedule_En			= (1 << 5);
static const uint32 kCR_MAC_TX_En			= (1 << 6);
static const uint32 kCR_MAC_RX_En			= (1 << 7);
static const uint32 kCR_Enswbcnio			= (1 << 12);
static const uint32 kCR_EnsecCAMTx			= (1 << 13);
static const uint32 kCR_EnsecCAMRx			= (1 << 14);

// H2C mailboxes — 4 rotating boxes, each with a 4-byte standard and
// 4-byte extended register. The firmware reads these to process commands
// from the host (scan, associate, set channel, power mode, etc.).
static const uint16 kRegHMEBox0				= 0x01D0;
static const uint16 kRegHMEBox1				= 0x01D4;
static const uint16 kRegHMEBox2				= 0x01D8;
static const uint16 kRegHMEBox3				= 0x01DC;
static const uint16 kRegHMEBoxExt0			= 0x01F0;
static const uint16 kRegHMEBoxExt1			= 0x01F4;
static const uint16 kRegHMEBoxExt2			= 0x01F8;
static const uint16 kRegHMEBoxExt3			= 0x01FC;

// Convenience arrays for indexed access in the H2C send loop
static const uint16 kRegHMEBox[kH2CMailboxCount]
	= { 0x01D0, 0x01D4, 0x01D8, 0x01DC };
static const uint16 kRegHMEBoxExt[kH2CMailboxCount]
	= { 0x01F0, 0x01F4, 0x01F8, 0x01FC };


// ---------------------------------------------------------------------------
// Register addresses — TX DMA (0x0200 – 0x027F)
//
// Controls the TX FIFO page allocation and DMA engine. The TX buffer is
// organized as 2048 pages of 128 bytes each (256 KB total), divided among
// 8 hardware queues.
// ---------------------------------------------------------------------------

static const uint16 kRegRQPN				= 0x0200;
static const uint16 kRegFIFOPage			= 0x0204;
static const uint16 kRegTDectrl				= 0x0208;
static const uint16 kRegTxDmaOffsetChk		= 0x020C;
static const uint16 kRegTxDmaStatus			= 0x0210;
static const uint16 kRegRQPN_NPQ			= 0x0214;

// Per-queue page counts — initial allocation from reference driver
static const uint32 kTxPubQPages			= 219;
static const uint32 kTxHiQPages				= 0;
static const uint32 kTxLoQPages				= 0;
static const uint32 kTxNorQPages			= 0;
static const uint32 kTxExQPages				= 0;


// ---------------------------------------------------------------------------
// Register addresses — RX DMA (0x0280 – 0x02FF)
// ---------------------------------------------------------------------------

static const uint16 kRegRxDmaAggPgTh		= 0x0280;
static const uint16 kRegRxPktNum			= 0x0284;
static const uint16 kRegRxDmaCtrl			= 0x0286;
static const uint16 kRegRxDmaStatus			= 0x0288;


// ---------------------------------------------------------------------------
// Register addresses — Protocol Engine (0x0400 – 0x047F)
//
// Rate adaptation, AMPDU aggregation, and MACID station tracking.
// ---------------------------------------------------------------------------

static const uint16 kRegVOParams			= 0x0400;
static const uint16 kRegVIParams			= 0x0404;
static const uint16 kRegBEParams			= 0x0408;
static const uint16 kRegBKParams			= 0x040C;

static const uint16 kRegSpecSIFS			= 0x0428;
static const uint16 kRegMacSpecSIFS			= 0x042C;
static const uint16 kRegSIFS_CTX			= 0x0514;
static const uint16 kRegSIFS_TRX			= 0x0516;

static const uint16 kRegARFR0				= 0x0444;
static const uint16 kRegARFR1				= 0x044C;
static const uint16 kRegARFR2				= 0x0454;
static const uint16 kRegARFR3				= 0x045C;
static const uint16 kRegARFR4				= 0x0464;
static const uint16 kRegARFR5				= 0x046C;

static const uint16 kRegAmpduMaxTime		= 0x0456;
static const uint16 kRegAmpduMaxLength		= 0x0458;

static const uint16 kRegFastEdcaCtrl		= 0x0460;


// ---------------------------------------------------------------------------
// Register addresses — EDCA / Timing (0x0500 – 0x05FF)
//
// WMM queue parameters, beacon timing, and TSF (Timing Synchronization
// Function) management.
// ---------------------------------------------------------------------------

static const uint16 kRegEdcaVoParam			= 0x0500;
static const uint16 kRegEdcaViParam			= 0x0504;
static const uint16 kRegEdcaBeParam			= 0x0508;
static const uint16 kRegEdcaBkParam			= 0x050C;

static const uint16 kRegBcnTcfg			= 0x0510;
static const uint16 kRegPifs				= 0x0512;
static const uint16 kRegAggBreakTime		= 0x051A;
static const uint16 kRegSlot				= 0x051B;
static const uint16 kRegTxPause				= 0x0522;
static const uint16 kRegDIS_TXREQ_CLR		= 0x0523;

static const uint16 kRegBcnInterval			= 0x0554;
static const uint16 kRegAtimWnd				= 0x055A;
static const uint16 kRegBcnDmaTime			= 0x0559;
static const uint16 kRegDrvEarlyInt			= 0x0558;

static const uint16 kRegTsftr				= 0x0560;	// 64-bit TSF timer
static const uint16 kRegTsftrSyncOffset		= 0x0568;
static const uint16 kRegAggBKTime			= 0x0569;

static const uint16 kRegTBTT_Prohibit		= 0x0540;
static const uint16 kRegBcnCtrl				= 0x0550;


// ---------------------------------------------------------------------------
// Register addresses — Wireless MAC (0x0600 – 0x07FF)
//
// MAC address, BSSID, frame filtering (RCR), security CAM (hardware
// encryption), and multi-port BSSID management.
// ---------------------------------------------------------------------------

static const uint16 kRegRCR				= 0x0608;
static const uint16 kRegMAC_ADDR			= 0x0610;	// 6 bytes
static const uint16 kRegBSSID				= 0x0618;	// 6 bytes
static const uint16 kRegMAC_ADDR_1			= 0x0700;	// Port 1 MAC
static const uint16 kRegBSSID_1			= 0x0708;	// Port 1 BSSID

// RCR (Receive Configuration Register) bit definitions — controls which
// frames the hardware passes up to the host vs. filtering silently.
static const uint32 kRCR_AAP				= (1 << 0);	// Accept all packets
static const uint32 kRCR_APM				= (1 << 1);	// Accept PM frames
static const uint32 kRCR_AM				= (1 << 2);	// Accept multicast
static const uint32 kRCR_AB				= (1 << 3);	// Accept broadcast
static const uint32 kRCR_ACRC32			= (1 << 4);	// Accept CRC errors
static const uint32 kRCR_AICV				= (1 << 5);	// Accept ICV errors
static const uint32 kRCR_ADF				= (1 << 7);	// Accept data frames
static const uint32 kRCR_ACF				= (1 << 8);	// Accept ctrl frames
static const uint32 kRCR_AMF				= (1 << 9);	// Accept mgmt frames
static const uint32 kRCR_CBSSID_BCN		= (1 << 12);	// Check BSSID beacon
static const uint32 kRCR_CBSSID_DATA		= (1 << 13);	// Check BSSID data

// Security configuration
static const uint16 kRegSecCfg				= 0x0680;
static const uint16 kRegCamCmd				= 0x0670;
static const uint16 kRegCamWrite			= 0x0674;
static const uint16 kRegCamRead				= 0x0678;
static const uint16 kRegCamDbg				= 0x067C;

// Security type encoding in TX/RX descriptors
enum SecurityType {
	kSecurityNone		= 0,
	kSecurityWEP40		= 1,
	kSecurityTKIP		= 2,
	kSecurityAESCCMP	= 3,
	kSecurityWEP104		= 4,
};

// Retry limit
static const uint16 kRegRetryLimit			= 0x042A;
static const uint16 kRegRespSIFSOFDM		= 0x063A;
static const uint16 kRegRespSIFSCCK		= 0x063C;
static const uint16 kRegACKTo				= 0x0640;


// ---------------------------------------------------------------------------
// Register addresses — Baseband / PHY (per-path)
//
// The RTL8814AU has 4 independent RF/BB paths (A, B, C, D), each with its
// own register space at a different base address. All 4 paths must be
// configured independently during initialization and channel switching.
// ---------------------------------------------------------------------------

static const uint16 kBBRegPathA				= 0x2800;
static const uint16 kBBRegPathB				= 0x2C00;
static const uint16 kBBRegPathC				= 0x3800;
static const uint16 kBBRegPathD				= 0x3C00;

static const uint16 kBBRegPathBase[kRfPathCount]
	= { 0x2800, 0x2C00, 0x3800, 0x3C00 };

// Common PHY register offsets (relative to path base)
static const uint16 kRegRFMod				= 0x0000;
static const uint16 kRegAGCRSSITable		= 0x0040;
static const uint16 kRegOFDM0TRxPathEn		= 0x0040;
static const uint16 kRegOFDM0TRMuxPar		= 0x0044;
static const uint16 kRegCCK0AFESetting		= 0x0000;

// RF register access (indirect via BB registers)
static const uint16 kRegRFCtrl				= 0x001C;
static const uint16 kRegRFPara				= 0x0020;
static const uint16 kRegRFReadData			= 0x0024;

// RF transceiver registers — accessed via kRegRFCtrl indirect path
static const uint8 kRfRegMode				= 0x00;
static const uint8 kRfRegChannelStandalone	= 0x18;
static const uint8 kRfRegTxGain				= 0x56;
static const uint8 kRfRegLNA				= 0xDF;


// ---------------------------------------------------------------------------
// TX descriptor format — 40 bytes prepended to every transmitted frame
//
// The driver builds this descriptor before submitting the frame via USB
// bulk OUT. Fields are packed in little-endian format. Bit positions match
// the hardware expectation.
// ---------------------------------------------------------------------------

static const uint32 kTxDescSize = 40;

// TX descriptor DWORD 0 (offset 0x00)
static const uint32 kTxDescPktLen_Shift		= 0;
static const uint32 kTxDescPktLen_Mask		= 0x0000FFFF;
static const uint32 kTxDescOffset_Shift		= 16;
static const uint32 kTxDescOffset_Mask		= 0x00FF0000;
static const uint32 kTxDescBMC				= (1 << 24);	// Broadcast/Multicast
static const uint32 kTxDescHTC				= (1 << 25);	// HT control present
static const uint32 kTxDescLS				= (1 << 26);	// Last segment
static const uint32 kTxDescFS				= (1 << 27);	// First segment
static const uint32 kTxDescOWN				= (1 << 31);	// Owned by hardware

// TX descriptor DWORD 1 (offset 0x04)
static const uint32 kTxDescMACID_Shift		= 0;
static const uint32 kTxDescMACID_Mask		= 0x0000007F;
static const uint32 kTxDescQueueSel_Shift	= 8;
static const uint32 kTxDescQueueSel_Mask	= 0x00001F00;
static const uint32 kTxDescRateID_Shift		= 16;
static const uint32 kTxDescRateID_Mask		= 0x001F0000;
static const uint32 kTxDescSecType_Shift	= 22;
static const uint32 kTxDescSecType_Mask		= 0x00C00000;
static const uint32 kTxDescPktOffset_Shift	= 24;
static const uint32 kTxDescPktOffset_Mask	= 0x1F000000;

// TX descriptor DWORD 2 (offset 0x08)
static const uint32 kTxDescAGGEn			= (1 << 12);
static const uint32 kTxDescBKRdy			= (1 << 13);

// TX descriptor DWORD 3 (offset 0x0C)
static const uint32 kTxDescSeq_Shift		= 16;
static const uint32 kTxDescSeq_Mask			= 0x0FFF0000;

// TX descriptor DWORD 4 (offset 0x10) — rate and bandwidth
static const uint32 kTxDescDataRate_Shift	= 0;
static const uint32 kTxDescDataRate_Mask	= 0x0000007F;
static const uint32 kTxDescDataBW_Shift		= 5;
static const uint32 kTxDescDataBW_Mask		= 0x00000060;	// FIXME: verify
static const uint32 kTxDescRTSEn			= (1 << 12);
static const uint32 kTxDescCTSEn			= (1 << 13);
static const uint32 kTxDescRetryLimit_Shift	= 16;
static const uint32 kTxDescRetryLimitEn		= (1 << 17);
static const uint32 kTxDescDataShort		= (1 << 4);	// Short preamble

// TX descriptor DWORD 5 (offset 0x14)
static const uint32 kTxDescTxPwrOffset_Shift = 0;

// TX descriptor DWORD 9 (offset 0x24)
static const uint32 kTxDescSWDefine_Shift	= 0;


// ---------------------------------------------------------------------------
// RX descriptor format — 24 bytes prepended to every received frame
//
// The hardware writes this descriptor when a frame is received. The driver
// parses it in the RX path to extract frame metadata (length, rate, RSSI).
// ---------------------------------------------------------------------------

static const uint32 kRxDescSize = 24;

// RX descriptor DWORD 0 (offset 0x00)
static const uint32 kRxDescPktLen_Shift		= 0;
static const uint32 kRxDescPktLen_Mask		= 0x00003FFF;
static const uint32 kRxDescCRC32_Err		= (1 << 14);
static const uint32 kRxDescICV_Err			= (1 << 15);
static const uint32 kRxDescDrvInfoSize_Shift = 16;
static const uint32 kRxDescDrvInfoSize_Mask	= 0x000F0000;
static const uint32 kRxDescShift_Shift		= 24;
static const uint32 kRxDescShift_Mask		= 0x03000000;
static const uint32 kRxDescPHYStatus		= (1 << 26);
static const uint32 kRxDescSWDec			= (1 << 27);
static const uint32 kRxDescLS				= (1 << 28);
static const uint32 kRxDescFS				= (1 << 29);
static const uint32 kRxDescEOR				= (1 << 30);
static const uint32 kRxDescOWN				= (1 << 31);

// RX descriptor DWORD 1 (offset 0x04)
static const uint32 kRxDescMACID_Shift		= 0;
static const uint32 kRxDescMACID_Mask		= 0x0000007F;
static const uint32 kRxDescTID_Shift		= 8;
static const uint32 kRxDescTID_Mask			= 0x00000F00;
static const uint32 kRxDescSecType_Shift	= 20;
static const uint32 kRxDescSecType_Mask		= 0x00700000;

// RX descriptor DWORD 2 (offset 0x08)
static const uint32 kRxDescSeq_Shift		= 0;
static const uint32 kRxDescSeq_Mask			= 0x00000FFF;
static const uint32 kRxDescFrag_Shift		= 12;
static const uint32 kRxDescFrag_Mask		= 0x0000F000;

// RX descriptor DWORD 3 (offset 0x0C) — rate and bandwidth
static const uint32 kRxDescRxRate_Shift		= 0;
static const uint32 kRxDescRxRate_Mask		= 0x0000007F;
static const uint32 kRxDescBW_Shift			= 4;
static const uint32 kRxDescBW_Mask			= 0x00000030;


// ---------------------------------------------------------------------------
// Data rate indices
//
// Used in both TX and RX descriptors. CCK rates for 2.4 GHz, OFDM for both
// bands, HT (802.11n) MCS indices, VHT (802.11ac) MCS indices.
// ---------------------------------------------------------------------------

enum DataRateIndex {
	// CCK rates (2.4 GHz only)
	kRateCCK1		= 0x00,
	kRateCCK2		= 0x01,
	kRateCCK5_5		= 0x02,
	kRateCCK11		= 0x03,

	// OFDM rates
	kRateOFDM6		= 0x04,
	kRateOFDM9		= 0x05,
	kRateOFDM12		= 0x06,
	kRateOFDM18		= 0x07,
	kRateOFDM24		= 0x08,
	kRateOFDM36		= 0x09,
	kRateOFDM48		= 0x0A,
	kRateOFDM54		= 0x0B,

	// HT MCS indices (802.11n)
	kRateHT_MCS0	= 0x0C,
	kRateHT_MCS1	= 0x0D,
	kRateHT_MCS2	= 0x0E,
	kRateHT_MCS3	= 0x0F,
	kRateHT_MCS4	= 0x10,
	kRateHT_MCS5	= 0x11,
	kRateHT_MCS6	= 0x12,
	kRateHT_MCS7	= 0x13,
	kRateHT_MCS8	= 0x14,
	kRateHT_MCS9	= 0x15,
	kRateHT_MCS10	= 0x16,
	kRateHT_MCS11	= 0x17,
	kRateHT_MCS12	= 0x18,
	kRateHT_MCS13	= 0x19,
	kRateHT_MCS14	= 0x1A,
	kRateHT_MCS15	= 0x1B,

	// VHT MCS indices (802.11ac) — per spatial stream
	kRateVHT_1SS_MCS0	= 0x2C,
	kRateVHT_1SS_MCS1	= 0x2D,
	kRateVHT_1SS_MCS2	= 0x2E,
	kRateVHT_1SS_MCS3	= 0x2F,
	kRateVHT_1SS_MCS4	= 0x30,
	kRateVHT_1SS_MCS5	= 0x31,
	kRateVHT_1SS_MCS6	= 0x32,
	kRateVHT_1SS_MCS7	= 0x33,
	kRateVHT_1SS_MCS8	= 0x34,
	kRateVHT_1SS_MCS9	= 0x35,

	kRateVHT_2SS_MCS0	= 0x36,
	kRateVHT_2SS_MCS9	= 0x3F,

	kRateVHT_3SS_MCS0	= 0x40,
	kRateVHT_3SS_MCS9	= 0x49,
};


// ---------------------------------------------------------------------------
// Channel and bandwidth definitions
// ---------------------------------------------------------------------------

enum ChannelBandwidth {
	kBandwidth20MHz		= 0,
	kBandwidth40MHz		= 1,
	kBandwidth80MHz		= 2,
};

enum ChannelBand {
	kBand2_4GHz			= 0,
	kBand5GHz			= 1,
};

// 2.4 GHz channels (1–14)
static const uint8 kChannelList2G[] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14
};

// 5 GHz channels — UNII-1, UNII-2, UNII-2 Extended, UNII-3
static const uint8 kChannelList5G[] = {
	36, 40, 44, 48,							// UNII-1
	52, 56, 60, 64,							// UNII-2
	100, 104, 108, 112, 116, 120, 124, 128,	// UNII-2 Extended
	132, 136, 140, 144,						// UNII-2 Extended (cont.)
	149, 153, 157, 161, 165, 169, 173, 177	// UNII-3
};


// ---------------------------------------------------------------------------
// H2C command IDs — sent from host to firmware via the mailbox system
//
// The firmware running on the Lexra 3081 processes these commands to perform
// operations the driver cannot do directly (scan, authenticate, manage
// power states, etc.).
// ---------------------------------------------------------------------------

enum H2CCommandID {
	kH2C_RsvdPage			= 0x00,
	kH2C_MediaStatusRpt	= 0x01,
	kH2C_ScanEn			= 0x02,
	kH2C_KeepAlive			= 0x03,
	kH2C_SetPwrMode		= 0x05,
	kH2C_PSTunePar			= 0x06,
	kH2C_MacIDCfg			= 0x40,
	kH2C_RSSI_Setting		= 0x42,
	kH2C_APReqTxRpt			= 0x43,
	kH2C_RaInfo				= 0x44,
	kH2C_BcnRsvdPage		= 0x09,
	kH2C_WoWLAN				= 0x80,
	kH2C_RemoteWakeCtrl		= 0x81,
};


// ---------------------------------------------------------------------------
// C2H event IDs — sent from firmware to host via interrupt IN endpoint
// ---------------------------------------------------------------------------

enum C2HEventID {
	kC2H_Debug				= 0x01,
	kC2H_ScanComplete		= 0x07,
	kC2H_BtInfo			= 0x09,
	kC2H_RateAdaptive		= 0x0C,
	kC2H_ConnectionStatus	= 0x10,
	kC2H_TxReport			= 0x14,
};


// ---------------------------------------------------------------------------
// EFUSE map field offsets — factory-programmed calibration data
//
// The EFUSE stores per-device calibration including MAC address, TX power
// tables, antenna configuration, and regulatory domain. These offsets are
// for the logical EFUSE map (after decoding the physical EFUSE).
// ---------------------------------------------------------------------------

static const uint16 kEfuseMacAddr			= 0x107;	// 6 bytes (USB variant)
static const uint16 kEfuseAntennaConfig		= 0x00E;	// TX + RX path config
static const uint16 kEfuseRfeType			= 0x010;	// RF front-end type (0-6)
static const uint16 kEfuseTxPwr2G			= 0x020;	// 2.4 GHz power table
static const uint16 kEfuseTxPwr5G			= 0x060;	// 5 GHz power table
static const uint16 kEfuseTxPwrByRate		= 0x0B0;	// Power-by-rate diffs
static const uint16 kEfuseThermalMeter		= 0x100;	// Thermal calibration
static const uint16 kEfuseCrystalCal		= 0x120;	// Crystal calibration
static const uint16 kEfuseChannelPlan		= 0x130;	// Regulatory domain


// ---------------------------------------------------------------------------
// Power-on sequence step types — used by the hardware init state machine
//
// The power-on sequence is a series of register writes, delays, and polls
// that bring the chip from reset to an operational state. Derived from
// Hal8814PwrSeq.c in the reference driver.
// ---------------------------------------------------------------------------

enum PowerSeqCmdType {
	kPwrCmdWrite		= 0,	// Write value to register
	kPwrCmdPolling		= 1,	// Poll register until condition met
	kPwrCmdDelay		= 2,	// Delay in microseconds
	kPwrCmdEnd			= 3,	// End of sequence
};

struct PowerSeqCommand {
	uint16	offset;				// Register address
	uint8	cutMask;			// Chip revision mask (0xFF = all)
	uint8	cmdType;			// PowerSeqCmdType
	uint8	mask;				// Bit mask
	uint8	value;				// Value to write or expect
};


#endif	// RTL8814AU_H
