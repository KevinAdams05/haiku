# RTL8814AU Register Reference

This document lists the key hardware registers used by the driver. All registers
are accessed via USB vendor control transfers (no memory-mapped I/O on USB
devices).

Register addresses and bit definitions are derived from `include/rtl8814a_spec.h`
in the reference driver (ulli-kroll/rtl8814au). Only registers actively used by
this driver are documented here.

---

## Notation

- **RW** = Read/Write, **RO** = Read Only, **WO** = Write Only, **W1C** = Write 1 to Clear
- Bit ranges: `[31:24]` means bits 31 through 24 inclusive
- Size suffixes: `(8)` = 8-bit, `(16)` = 16-bit, `(32)` = 32-bit

---

## 1. System Configuration (0x0000–0x00FF)

### REG_SYS_FUNC_EN (0x0002, 16-bit, RW)

System function enable register. Controls power to major chip blocks.

| Bit | Name | Description |
|-----|------|-------------|
| 0 | FEN_BBRSTB | BB reset (active low) |
| 1 | FEN_BB_GLB_RST | BB global reset |
| 2 | FEN_USBA | USB analog enable |
| 3 | FEN_UPLL | USB PLL enable |
| 4 | FEN_USBD | USB digital enable |
| 8 | FEN_DIO_PCIE | PCIe digital I/O |
| 10 | FEN_PCIEA | PCIe analog enable |
| 11 | FEN_PPLL | PCIe PLL enable |
| 12 | FEN_CPUEN | CPU (Lexra 3081) enable — **critical for firmware load** |
| 13 | FEN_DCORE | Digital core power |
| 14 | FEN_ELDR | EEPROM loader enable |
| 15 | FEN_HWPDN | Hardware power down |

**Driver usage:** Bit 12 (offset byte +1, bit 2) is cleared before firmware DMA
and set after to start the MCU.

### REG_AFE_CTRL (0x0028, 32-bit, RW)

Analog front-end control. PLL and crystal oscillator configuration.

### REG_GPIO_MUXCFG (0x0040, 32-bit, RW)

GPIO multiplexer configuration. Controls pin function selection for LEDs,
antenna switching, and external PA/LNA control.

### REG_HIMR0 / REG_HISR0 (0x00B0 / 0x00B4, 32-bit, RW / W1C)

Host interrupt mask and status register (set 0).

| Bit | Name | Description |
|-----|------|-------------|
| 0 | RXOK | RX data available |
| 1 | RXERR | RX error |
| 4 | TXOK_VO | TX complete (voice queue) |
| 5 | TXOK_VI | TX complete (video queue) |
| 6 | TXOK_BE | TX complete (best effort) |
| 7 | TXOK_BK | TX complete (background) |
| 8 | TXBCNOK | Beacon TX complete |
| 9 | TXBCNERR | Beacon TX error |
| 16 | C2HCMD | Card-to-host command ready |
| 20 | CPWM | CPU power mode change |

### REG_HIMR1 / REG_HISR1 (0x00B8 / 0x00BC, 32-bit, RW / W1C)

Host interrupt mask and status register (set 1). Extended interrupt sources.

---

## 2. MAC General Configuration (0x0100–0x01FF)

### REG_8051FW_CTRL_8814A (0x0080, 32-bit, RW)

Firmware download control register.

| Bit | Name | Description |
|-----|------|-------------|
| 0 | FWDL_EN | Enable firmware download mode |
| 3 | MCUFWDL_RDY | Firmware download ready (set by MCU) |
| 6 | WINTINI_RDY | Firmware init complete |
| 12 | FWDL_CHKSUM_RPT | Checksum report enable |
| 13 | FWDL_DISABLE_SIM | Disable simulation mode |
| 15 | CPU_DL_READY | **MCU ready after FW load — poll this** |

### REG_HMEBOX_0..3 (0x01D0–0x01DC, 32-bit each, WO)

H2C command mailboxes. Each holds 4 bytes of an H2C command.

### REG_HMEBOX_EXT_0..3 (0x01F0–0x01FC, 32-bit each, WO)

Extended H2C mailbox data. Each holds 4 additional bytes (total 7 usable
bytes per command with standard + extended).

### REG_MCUFWDL (0x0080, 8-bit, RW)

MCU firmware download status. Overlaps with REG_8051FW_CTRL_8814A byte 0.

---

## 3. TX DMA (0x0200–0x027F)

### REG_RQPN (0x0200, 32-bit, RW)

Request queue page number. Configures TX buffer allocation across queues.

### REG_FIFOPAGE (0x0204, 32-bit, RO)

Available FIFO pages. Shows remaining TX buffer space per queue.

### REG_TXDMA_OFFSET_CHK (0x020C, 32-bit, RW)

TX DMA offset check. Validates descriptor alignment.

### REG_TXDMA_STATUS (0x0210, 32-bit, RO)

TX DMA status. Per-queue transfer status and error flags.

---

## 4. RX DMA (0x0280–0x02FF)

### REG_RXDMA_AGG_PG_TH (0x0280, 32-bit, RW)

RX DMA aggregation page threshold. Controls how many pages are aggregated
into a single USB bulk IN transfer.

| Bits | Name | Description |
|------|------|-------------|
| [7:0] | AGG_PG_TH | Aggregation page threshold |
| [14:8] | AGG_TO | Aggregation timeout (in units of 32 µs) |

### REG_RXPKT_NUM (0x0284, 32-bit, RO)

Number of received packets waiting in hardware buffer.

---

## 5. Protocol Engine (0x0400–0x047F)

### REG_ARFR0..5 (0x0444–0x046C, 32-bit each, RW)

Auto rate fallback register sets. Configure which rates the hardware can
fall back to when transmission fails at the current rate.

### REG_AMPDU_MAX_LENGTH (0x0458, 32-bit, RW)

Maximum A-MPDU aggregation length. RTL8814AU default: 0x1FFFF (128 KB).

### REG_AMPDU_MAX_TIME (0x0456, 8-bit, RW)

Maximum A-MPDU duration. RTL8814AU default: 0x70.

---

## 6. EDCA / Timing (0x0500–0x05FF)

### REG_EDCA_VO_PARAM..BK_PARAM (0x0500–0x050C, 32-bit each, RW)

WMM EDCA parameters per access category (VO, VI, BE, BK).

| Bits | Name | Description |
|------|------|-------------|
| [3:0] | AIFS | Arbitration Inter-Frame Spacing |
| [7:4] | ECWmin | Exponent of CWmin |
| [11:8] | ECWmax | Exponent of CWmax |
| [31:16] | TXOP | TX opportunity limit |

### REG_BCN_INTERVAL (0x0554, 16-bit, RW)

Beacon interval in time units (TU, 1 TU = 1024 µs). Default: 100 TU.

### REG_TSF_TIMER (0x0560, 64-bit, RO)

64-bit TSF (Timing Synchronization Function) timer. Increments at 1 MHz.

---

## 7. Wireless MAC (0x0600–0x07FF)

### REG_MAC_ADDR (0x0610, 48-bit, RW)

MAC address of the device. Loaded from EFUSE during initialization.

### REG_BSSID (0x0618, 48-bit, RW)

BSSID of the associated AP.

### REG_RCR (0x0608, 32-bit, RW)

Receive Configuration Register. Controls frame filtering.

| Bit | Name | Description |
|-----|------|-------------|
| 0 | AAP | Accept all packets (promiscuous) |
| 1 | APM | Accept PM-bit-set frames |
| 2 | AM | Accept multicast |
| 3 | AB | Accept broadcast |
| 4 | ACRC32 | Accept CRC32 error frames |
| 5 | AICV | Accept ICV error frames |
| 7 | ADF | Accept data frames |
| 8 | ACF | Accept control frames |
| 9 | AMF | Accept management frames |
| 12 | CBSSID_BCN | Check BSSID for beacons |
| 13 | CBSSID_DATA | Check BSSID for data frames |

### REG_SECCFG (0x0680, 16-bit, RW)

Security configuration. Controls hardware encryption/decryption behavior.

### REG_CAMCMD (0x0670, 32-bit, RW)

CAM (Content Addressable Memory) command register. Used to read/write the
64 hardware security key entries.

### REG_CAMWRITE (0x0674, 32-bit, WO)

CAM write data register. Write key material here before issuing CAM command.

---

## 8. Baseband Registers (per-path)

### Path Base Addresses

| Path | Base Address |
|------|-------------|
| A | 0x2800 |
| B | 0x2C00 |
| C | 0x3800 |
| D | 0x3C00 |

Each path has identical register offsets from its base. Key per-path registers
include AGC tables, IQ calibration coefficients, TX power indices, and RSSI
measurements.

### Key Offsets (relative to path base)

| Offset | Name | Description |
|--------|------|-------------|
| +0x00 | rFPGA0_RFMOD | RF mode (11a/b/g/n/ac) |
| +0x18 | rFPGA0_AnalogParameter4 | Analog parameter set 4 |
| +0x1C | rFPGA0_PSDFunction | PSD (Power Spectral Density) |
| +0x40 | rOFDM0_TRxPathEnable | OFDM TX/RX path enable mask |
| +0x44 | rOFDM0_TRMuxPar | TX/RX mux parameters |
| +0xC0 | rOFDM0_RxIQExtAnta | RX IQ calibration for antenna A |

---

## 9. EFUSE Memory Map

EFUSE contains factory-programmed calibration data. Total: 1024 bytes across
2 banks. Key fields:

| Offset | Length | Content |
|--------|--------|---------|
| 0x0D8 | 6 | MAC address (USB variant) |
| 0x00E | 1 | Antenna TX path config |
| 0x00F | 1 | Antenna RX path config |
| 0x010 | 1 | RFE type (0–6) |
| 0x020 | varies | 2.4 GHz TX power table (per-path, per-channel group) |
| 0x060 | varies | 5 GHz TX power table (per-path, per-channel group) |
| 0x0B0 | varies | TX power by rate differential |
| 0x100 | varies | Thermal meter calibration value |
| 0x120 | varies | Crystal calibration |
| 0x130 | 2 | Channel plan (regulatory domain) |

**Note:** Exact EFUSE offsets vary between chip revisions. The driver reads the
EFUSE map sequentially and parses it using the reference driver's known field
layout.

---

## 10. H2C Command Format

Each H2C command uses one of 4 rotating mailboxes:

```
HMEBOX_N    (4 bytes): [CMD_ID:7][CMD_SEQ:1] [PARAM0] [PARAM1] [PARAM2]
HMEBOX_EXT_N(4 bytes): [PARAM3] [PARAM4] [PARAM5] [PARAM6]
```

- **CMD_ID** (7 bits): Command identifier
- **CMD_SEQ** (1 bit): Toggles with each command to signal new data
- **PARAM0–6** (7 bytes): Command-specific parameters

The driver maintains a write index (0–3) and increments it modulo 4 after
each command. Before writing, it checks that the target mailbox has been
consumed by the firmware (read-back confirm).

---

## 11. C2H Event Format

C2H events arrive on the interrupt IN endpoint:

```
[EVT_ID:8] [EVT_SEQ:8] [PAYLOAD: variable length]
```

Key C2H event IDs:

| ID | Name | Description |
|----|------|-------------|
| 0x01 | DBG | Debug message from firmware |
| 0x07 | ScanComplete | Scan finished, results available |
| 0x09 | BtInfo | Bluetooth coexistence info |
| 0x0C | RateAdaptive | Rate adaptation update |
| 0x10 | ConnectionStatus | Association state change |
| 0x14 | TxReport | Per-station TX statistics |
