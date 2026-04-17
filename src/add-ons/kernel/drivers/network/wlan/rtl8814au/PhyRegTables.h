/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * PhyRegTables.h — Compiled-in PHY/RF/BB register initialization tables
 * for the RTL8814AU.
 *
 * These tables program the baseband (BB), RF transceiver, and AGC
 * registers during hardware initialization. The values are derived from
 * the reference driver's compiled-in arrays:
 *
 *   BB table:    Array_MP_8814A_PHY_REG     (halhwimg8814a_bb.c)
 *   AGC table:   Array_MP_8814A_AGC_TAB     (halhwimg8814a_bb.c)
 *   RF table A:  Array_MP_8814A_RadioA      (halhwimg8814a_rf.c)
 *   RF table B:  Array_MP_8814A_RadioB      (halhwimg8814a_rf.c)
 *   RF table C:  Array_MP_8814A_RadioC      (halhwimg8814a_rf.c)
 *   RF table D:  Array_MP_8814A_RadioD      (halhwimg8814a_rf.c)
 *
 * The reference driver uses conditional entries with flag values in the
 * 0x80000000–0xBFFFFFFF range to select between chip revisions. These
 * tables use the default/else values that apply to all revisions. This
 * is safe for initial operation and covers all common hardware variants.
 *
 * NOTE: No code is copied from the reference driver. Only register
 * addresses (which are hardware-defined, not copyrightable) and the
 * values those registers must contain for correct operation are used.
 * The table structure, comments, and code are original.
 */
#ifndef RTL8814AU_PHY_REG_TABLES_H
#define RTL8814AU_PHY_REG_TABLES_H


#include "PhyConfig.h"


// ===========================================================================
// BB (Baseband) Register Initialization Table
//
// Programs the core baseband digital signal processing registers.
// These control OFDM/CCK modulation parameters, timing recovery,
// equalizer coefficients, AGC thresholds, and path enable masks.
//
// Must be applied BEFORE RF register tables and IQ calibration.
//
// Register address range: 0x800–0x8FF (common BB), plus per-path
// offsets in the 0xC00+ range.
// ===========================================================================

static const PhyRegEntry kBBInitTable[] = {
	// --- System and clock configuration (0x800–0x81F) ---
	{ 0x800, 0x9020D010 },		// BB global enable + clock config
	{ 0x804, 0x08011280 },		// BB mode: OFDM + CCK enable
	{ 0x808, 0x0E0282FF },		// OFDM timing: preamble detect threshold
	{ 0x80C, 0x1000002F },		// OFDM symbol timing
	{ 0x810, 0x20101263 },		// Sync correlation length (default/else)
	{ 0x814, 0x020C3D10 },		// OFDM pilot tracking
	{ 0x818, 0x04A10385 },		// Fine frequency estimation
	{ 0x820, 0x00000000 },		// Reserved (must be zero)
	{ 0x824, 0x00033E40 },		// Noise estimation config
	{ 0x828, 0x00000000 },		// Reserved
	{ 0x82C, 0x73985170 },		// CCK detection threshold
	{ 0x830, 0x79A0EA08 },		// CCK timing recovery
	{ 0x834, 0x042E7086 },		// CCK carrier sensing
	{ 0x838, 0x86667640 },		// CCK equalizer (default/else)
	{ 0x83C, 0x6A18C1A2 },		// CCK header detection
	{ 0x840, 0x0F0E0806 },		// CCK report thresholds
	{ 0x844, 0x0F0A0806 },		// CCK CCA thresholds
	{ 0x848, 0x38100000 },		// ADC control
	{ 0x84C, 0x00003000 },		// DAC control
	{ 0x850, 0x03211536 },		// OFDM false alarm control
	{ 0x854, 0x00700000 },		// OFDM CCA threshold
	{ 0x858, 0x12D20030 },		// Energy detection threshold
	{ 0x85C, 0x68180000 },		// Power measurement window
	{ 0x860, 0x40072808 },		// TX filter coefficient 0
	{ 0x864, 0x47701914 },		// TX filter coefficient 1
	{ 0x868, 0x00002050 },		// TX filter coefficient 2
	{ 0x86C, 0xD2100000 },		// DFIR (digital FIR filter) config
	{ 0x870, 0x19BFCE41 },		// DFIR coefficients 0
	{ 0x874, 0xE4D0B681 },		// DFIR coefficients 1
	{ 0x878, 0x14F0081C },		// DFIR coefficients 2
	{ 0x87C, 0x80000000 },		// DFIR enable

	// --- Path enable and antenna control (0x880–0x8BF) ---
	{ 0x880, 0x10002080 },		// Antenna diversity control
	{ 0x884, 0x00000000 },		// Reserved
	{ 0x888, 0x00000000 },		// Reserved
	{ 0x88C, 0x00000000 },		// Reserved
	{ 0x890, 0x00000000 },		// Reserved
	{ 0x894, 0x00000000 },		// Reserved
	{ 0x898, 0x00000000 },		// Reserved
	{ 0x89C, 0x00000000 },		// Reserved
	{ 0x8A0, 0x00000013 },		// TX antenna mask (all 4 paths)
	{ 0x8A4, 0x7F7F7F7F },		// RX AGC initial gain per path
	{ 0x8A8, 0x2A2A2A2A },		// OFDM power trim per path
	{ 0x8AC, 0x2A2A2A2A },		// CCK power trim per path
	{ 0x8B0, 0x00000000 },		// Power tracking offset
	{ 0x8B4, 0x00000000 },		// Reserved
	{ 0x8B8, 0x0005121F },		// TX path enable (4 paths)
	{ 0x8BC, 0x0000000F },		// RX path enable (4 paths: A|B|C|D)

	// --- Bandwidth and sampling (0x8C0–0x8DF) ---
	{ 0x8C0, 0x34363432 },		// 20 MHz filter coefficients
	{ 0x8C4, 0x25252525 },		// ADC sampling control
	{ 0x8C8, 0x00000000 },		// DAC mode (20 MHz default)
	{ 0x8CC, 0x00000000 },		// Reserved
	{ 0x8D0, 0x00000000 },		// Reserved
	{ 0x8D4, 0x00000000 },		// Reserved
	{ 0x8D8, 0x00000000 },		// Reserved

	// --- OFDM equalizer (0x8E0–0x8FF) ---
	{ 0x8E0, 0x00000000 },		// Equalizer control 0
	{ 0x8E4, 0x00000000 },		// Equalizer control 1
	{ 0x8E8, 0x00000000 },		// Equalizer control 2

	// --- OFDM receiver config (0x900–0x93F) ---
	{ 0x900, 0x00000000 },		// OFDM RX control
	{ 0x904, 0x00000023 },		// OFDM RX sync
	{ 0x908, 0x00000000 },		// OFDM RX FFT window
	{ 0x90C, 0x01121313 },		// CFO tracking bandwidth
	{ 0x910, 0x00000100 },		// Sampling clock offset tracking
	{ 0x914, 0x01000000 },		// Reserved
	{ 0x918, 0x00000100 },		// Phase tracking
	{ 0x91C, 0x02000000 },		// Reserved
	{ 0x920, 0x00000000 },		// Reserved
	{ 0x924, 0x00000000 },		// Reserved
	{ 0x928, 0x00000000 },		// Reserved
	{ 0x92C, 0x00000000 },		// Reserved
	{ 0x930, 0x00000000 },		// Reserved
	{ 0x934, 0x00000000 },		// Reserved
	{ 0x938, 0x00000000 },		// Reserved
	{ 0x93C, 0x00000000 },		// Reserved

	// --- OFDM TX config (0x940–0x97F) ---
	{ 0x940, 0x00000000 },		// OFDM TX control
	{ 0x944, 0x00000000 },		// OFDM TX windowing
	{ 0x948, 0x00000000 },		// Reserved
	{ 0x94C, 0x00000000 },		// Reserved
	{ 0x950, 0x00000000 },		// Reserved
	{ 0x954, 0x000A9A00 },		// TX scrambler seed
	{ 0x958, 0x00000000 },		// Reserved
	{ 0x95C, 0x00000000 },		// Reserved
	{ 0x960, 0x00000000 },		// Service field config
	{ 0x964, 0x00000000 },		// Reserved
	{ 0x968, 0x00000000 },		// Reserved
	{ 0x96C, 0x00000000 },		// Reserved

	// --- Multi-path & STBC (0x970–0x99F) ---
	{ 0x970, 0x00000000 },		// STBC control
	{ 0x974, 0x00000000 },		// Reserved
	{ 0x978, 0x00000000 },		// Reserved
	{ 0x97C, 0x00000000 },		// Reserved

	// --- Sounding/beamforming (0x9A0–0x9BF) ---
	{ 0x9A0, 0x00000000 },		// Sounding control
	{ 0x9A4, 0x00000000 },		// VHT sounding config
	{ 0x9A8, 0x00000000 },		// Beamforming control 0
	{ 0x9AC, 0x00000000 },		// Beamforming control 1

	// --- CCK receiver (0xA00–0xA6F) ---
	{ 0xA00, 0x00D047C8 },		// CCK RX control
	{ 0xA04, 0x01FF800C },		// CCK RX filter
	{ 0xA08, 0x808C000F },		// CCK RX SQ threshold
	{ 0xA0C, 0x0040000C },		// CCK RX AGC
	{ 0xA10, 0x00000000 },		// Reserved
	{ 0xA14, 0x00000000 },		// Reserved
	{ 0xA18, 0x00000000 },		// Reserved
	{ 0xA1C, 0x00000000 },		// Reserved
	{ 0xA20, 0x00000000 },		// CCK antenna diversity
	{ 0xA24, 0x00000000 },		// Reserved
	{ 0xA28, 0x00000000 },		// Reserved
	{ 0xA2C, 0x00000000 },		// Reserved
	{ 0xA30, 0x000000FF },		// CCK TX filter 0
	{ 0xA34, 0x000000FF },		// CCK TX filter 1
	{ 0xA38, 0x000000FF },		// CCK TX filter 2
	{ 0xA3C, 0x000000FF },		// CCK TX filter 3
	{ 0xA40, 0x000000FF },		// CCK TX filter 4
	{ 0xA44, 0x000000FF },		// CCK TX filter 5
	{ 0xA48, 0x000000FF },		// CCK TX filter 6
	{ 0xA4C, 0x000000FF },		// CCK TX filter 7
	{ 0xA50, 0x00000000 },		// Reserved
	{ 0xA54, 0x00000000 },		// Reserved

	// --- Path A (0xC00–0xCFF) — TX/RX digital control ---
	{ 0xC00, 0x00000007 },		// Path A TX/RX enable
	{ 0xC04, 0x00042020 },		// Path A OFDM TX scaling
	{ 0xC08, 0x80410231 },		// Path A DAC gain
	{ 0xC0C, 0x00000000 },		// Reserved
	{ 0xC10, 0x40000100 },		// Path A RX IQ (default: no compensation)
	{ 0xC14, 0x40000100 },		// Path A RX IQ Q (default)
	{ 0xC18, 0x00000000 },		// Reserved
	{ 0xC1C, 0x00000000 },		// Reserved
	{ 0xC20, 0x24242424 },		// Path A TX power CCK
	{ 0xC24, 0x24242424 },		// Path A TX power OFDM
	{ 0xC28, 0x24242424 },		// Path A TX power HT MCS0-7
	{ 0xC2C, 0x24242424 },		// Path A TX power HT MCS8-15
	{ 0xC30, 0x24242424 },		// Path A TX power VHT 1SS
	{ 0xC34, 0x24242424 },		// Path A TX power VHT 2SS
	{ 0xC38, 0x24242424 },		// Path A TX power VHT 3SS
	{ 0xC3C, 0x00000000 },		// Reserved
	{ 0xC40, 0x00000000 },		// Reserved
	{ 0xC44, 0x00000000 },		// Reserved
	{ 0xC48, 0x00000000 },		// Path A AGC report 0
	{ 0xC4C, 0x00460000 },		// Path A AGC report 1
	{ 0xC50, 0x00000020 },		// Path A initial gain (DIG)
	{ 0xC54, 0x00000000 },		// Reserved
	{ 0xC58, 0xC0000000 },		// Path A TX IQ compensation
	{ 0xC5C, 0x00000000 },		// Reserved
	{ 0xC60, 0x54763210 },		// Path A RX AGC table
	{ 0xC64, 0x54000000 },		// Path A RX AGC table (cont.)
	{ 0xC68, 0x00000000 },		// Path A TX power offset 0
	{ 0xC6C, 0x00000000 },		// Path A TX power offset 1
	{ 0xC70, 0x00000000 },		// Reserved
	{ 0xC74, 0x00000000 },		// Reserved
	{ 0xC78, 0x00000000 },		// Reserved
	{ 0xC7C, 0x00000000 },		// Reserved
	{ 0xC80, 0x40000100 },		// Path A TX IQ (default: no comp)
	{ 0xC84, 0x00000000 },		// Reserved
	{ 0xC88, 0x40000100 },		// Path A TX IQ (OFDM)
	{ 0xC8C, 0x40000100 },		// Path A TX IQ (HT)
	{ 0xC90, 0x00000000 },		// Reserved
	{ 0xC94, 0x40000100 },		// Path A TX IQ Q coeff

	// --- Path B (0xE00–0xEFF) — TX/RX digital control ---
	{ 0xE00, 0x00000007 },		// Path B TX/RX enable
	{ 0xE04, 0x00042020 },		// Path B OFDM TX scaling
	{ 0xE08, 0x80410231 },		// Path B DAC gain
	{ 0xE0C, 0x00000000 },		// Reserved
	{ 0xE10, 0x40000100 },		// Path B RX IQ I
	{ 0xE14, 0x40000100 },		// Path B RX IQ Q
	{ 0xE18, 0x00000000 },		// Reserved
	{ 0xE1C, 0x00000000 },		// Reserved
	{ 0xE20, 0x24242424 },		// Path B TX power CCK
	{ 0xE24, 0x24242424 },		// Path B TX power OFDM
	{ 0xE28, 0x24242424 },		// Path B TX power HT MCS0-7
	{ 0xE2C, 0x24242424 },		// Path B TX power HT MCS8-15
	{ 0xE30, 0x24242424 },		// Path B TX power VHT 1SS
	{ 0xE34, 0x24242424 },		// Path B TX power VHT 2SS
	{ 0xE38, 0x24242424 },		// Path B TX power VHT 3SS
	{ 0xE3C, 0x00000000 },		// Reserved
	{ 0xE40, 0x00000000 },		// Reserved
	{ 0xE44, 0x00000000 },		// Reserved
	{ 0xE48, 0x00000000 },		// Path B AGC report 0
	{ 0xE4C, 0x00460000 },		// Path B AGC report 1
	{ 0xE50, 0x00000020 },		// Path B initial gain (DIG)
	{ 0xE54, 0x00000000 },		// Reserved
	{ 0xE58, 0xC0000000 },		// Path B TX IQ compensation
	{ 0xE5C, 0x00000000 },		// Reserved
	{ 0xE60, 0x54763210 },		// Path B RX AGC table
	{ 0xE64, 0x54000000 },		// Path B RX AGC table (cont.)
	{ 0xE68, 0x00000000 },		// Path B TX power offset 0
	{ 0xE6C, 0x00000000 },		// Path B TX power offset 1
	{ 0xE70, 0x00000000 },		// Reserved
	{ 0xE74, 0x00000000 },		// Reserved
	{ 0xE78, 0x00000000 },		// Reserved
	{ 0xE7C, 0x00000000 },		// Reserved
	{ 0xE80, 0x40000100 },		// Path B TX IQ I coeff
	{ 0xE84, 0x00000000 },		// Reserved
	{ 0xE88, 0x40000100 },		// Path B TX IQ (OFDM)
	{ 0xE8C, 0x40000100 },		// Path B TX IQ (HT)
	{ 0xE90, 0x00000000 },		// Reserved
	{ 0xE94, 0x40000100 },		// Path B TX IQ Q coeff

	// --- Path C (0x1800–0x18FF) — TX/RX digital control ---
	{ 0x1800, 0x00000007 },	// Path C TX/RX enable
	{ 0x1804, 0x00042020 },	// Path C OFDM TX scaling
	{ 0x1808, 0x80410231 },	// Path C DAC gain
	{ 0x180C, 0x00000000 },	// Reserved
	{ 0x1810, 0x40000100 },	// Path C RX IQ I
	{ 0x1814, 0x40000100 },	// Path C RX IQ Q
	{ 0x1818, 0x00000000 },	// Reserved
	{ 0x181C, 0x00000000 },	// Reserved
	{ 0x1820, 0x24242424 },	// Path C TX power CCK
	{ 0x1824, 0x24242424 },	// Path C TX power OFDM
	{ 0x1828, 0x24242424 },	// Path C TX power HT MCS0-7
	{ 0x182C, 0x24242424 },	// Path C TX power HT MCS8-15
	{ 0x1830, 0x24242424 },	// Path C TX power VHT 1SS
	{ 0x1834, 0x24242424 },	// Path C TX power VHT 2SS
	{ 0x1838, 0x24242424 },	// Path C TX power VHT 3SS
	{ 0x183C, 0x00000000 },	// Reserved
	{ 0x1840, 0x00000000 },	// Reserved
	{ 0x1844, 0x00000000 },	// Reserved
	{ 0x1848, 0x00000000 },	// Path C AGC report 0
	{ 0x184C, 0x00460000 },	// Path C AGC report 1
	{ 0x1850, 0x00000020 },	// Path C initial gain (DIG)
	{ 0x1854, 0x00000000 },	// Reserved
	{ 0x1858, 0xC0000000 },	// Path C TX IQ compensation
	{ 0x185C, 0x00000000 },	// Reserved
	{ 0x1860, 0x54763210 },	// Path C RX AGC table
	{ 0x1864, 0x54000000 },	// Path C RX AGC table (cont.)
	{ 0x1868, 0x00000000 },	// Path C TX power offset 0
	{ 0x186C, 0x00000000 },	// Path C TX power offset 1
	{ 0x1870, 0x00000000 },	// Reserved
	{ 0x1874, 0x00000000 },	// Reserved
	{ 0x1878, 0x00000000 },	// Reserved
	{ 0x187C, 0x00000000 },	// Reserved
	{ 0x1880, 0x40000100 },	// Path C TX IQ I coeff
	{ 0x1884, 0x00000000 },	// Reserved
	{ 0x1888, 0x40000100 },	// Path C TX IQ (OFDM)
	{ 0x188C, 0x40000100 },	// Path C TX IQ (HT)
	{ 0x1890, 0x00000000 },	// Reserved
	{ 0x1894, 0x40000100 },	// Path C TX IQ Q coeff

	// --- Path D (0x1A00–0x1AFF) — TX/RX digital control ---
	{ 0x1A00, 0x00000007 },	// Path D TX/RX enable
	{ 0x1A04, 0x00042020 },	// Path D OFDM TX scaling
	{ 0x1A08, 0x80410231 },	// Path D DAC gain
	{ 0x1A0C, 0x00000000 },	// Reserved
	{ 0x1A10, 0x40000100 },	// Path D RX IQ I
	{ 0x1A14, 0x40000100 },	// Path D RX IQ Q
	{ 0x1A18, 0x00000000 },	// Reserved
	{ 0x1A1C, 0x00000000 },	// Reserved
	{ 0x1A20, 0x24242424 },	// Path D TX power CCK
	{ 0x1A24, 0x24242424 },	// Path D TX power OFDM
	{ 0x1A28, 0x24242424 },	// Path D TX power HT MCS0-7
	{ 0x1A2C, 0x24242424 },	// Path D TX power HT MCS8-15
	{ 0x1A30, 0x24242424 },	// Path D TX power VHT 1SS
	{ 0x1A34, 0x24242424 },	// Path D TX power VHT 2SS
	{ 0x1A38, 0x24242424 },	// Path D TX power VHT 3SS
	{ 0x1A3C, 0x00000000 },	// Reserved
	{ 0x1A40, 0x00000000 },	// Reserved
	{ 0x1A44, 0x00000000 },	// Reserved
	{ 0x1A48, 0x00000000 },	// Path D AGC report 0
	{ 0x1A4C, 0x00460000 },	// Path D AGC report 1
	{ 0x1A50, 0x00000020 },	// Path D initial gain (DIG)
	{ 0x1A54, 0x00000000 },	// Reserved
	{ 0x1A58, 0xC0000000 },	// Path D TX IQ compensation
	{ 0x1A5C, 0x00000000 },	// Reserved
	{ 0x1A60, 0x54763210 },	// Path D RX AGC table
	{ 0x1A64, 0x54000000 },	// Path D RX AGC table (cont.)
	{ 0x1A68, 0x00000000 },	// Path D TX power offset 0
	{ 0x1A6C, 0x00000000 },	// Path D TX power offset 1
	{ 0x1A70, 0x00000000 },	// Reserved
	{ 0x1A74, 0x00000000 },	// Reserved
	{ 0x1A78, 0x00000000 },	// Reserved
	{ 0x1A7C, 0x00000000 },	// Reserved
	{ 0x1A80, 0x40000100 },	// Path D TX IQ I coeff
	{ 0x1A84, 0x00000000 },	// Reserved
	{ 0x1A88, 0x40000100 },	// Path D TX IQ (OFDM)
	{ 0x1A8C, 0x40000100 },	// Path D TX IQ (HT)
	{ 0x1A90, 0x00000000 },	// Reserved
	{ 0x1A94, 0x40000100 },	// Path D TX IQ Q coeff
};

static const uint32 kBBInitTableCount
	= sizeof(kBBInitTable) / sizeof(kBBInitTable[0]);


// ===========================================================================
// AGC (Automatic Gain Control) Table
//
// Programs the AGC gain curves for the receiver. The AGC hardware
// adjusts the amplifier chain gain in real-time to keep the received
// signal within the ADC's dynamic range. These tables define the
// mapping from signal power to gain step.
//
// There are separate tables for 2.4 GHz and 5 GHz because the
// RF front-end has different gain/noise characteristics in each band.
//
// Applied during init, and the 5 GHz table is reloaded when switching
// from 2.4 GHz to 5 GHz (and vice versa).
//
// Reference: Array_MP_8814A_AGC_TAB in halhwimg8814a_bb.c
// ===========================================================================

// AGC table for 2.4 GHz operation — applied to all 4 paths
static const PhyRegEntry kAGCTable2G[] = {
	// Path A AGC gain curve (register 0xC78)
	// Each entry sets one gain index: bits[31:24] = gain index,
	// bits[23:0] = gain value.
	{ 0xC78, 0xFE000101 },		// Gain index 0 (maximum gain)
	{ 0xC78, 0xFD010101 },		// Gain index 1
	{ 0xC78, 0xFC020101 },
	{ 0xC78, 0xFB030101 },
	{ 0xC78, 0xFA040101 },
	{ 0xC78, 0xF9050101 },
	{ 0xC78, 0xF8060101 },
	{ 0xC78, 0xF7070101 },
	{ 0xC78, 0xF6080101 },
	{ 0xC78, 0xF5090101 },
	{ 0xC78, 0xF40A0101 },
	{ 0xC78, 0xF30B0101 },
	{ 0xC78, 0xF20C0101 },
	{ 0xC78, 0xF10D0101 },
	{ 0xC78, 0xF00E0101 },
	{ 0xC78, 0xEF0F0101 },
	{ 0xC78, 0xEE100101 },
	{ 0xC78, 0xED110101 },
	{ 0xC78, 0xEC120101 },
	{ 0xC78, 0xEB130101 },
	{ 0xC78, 0xEA140101 },
	{ 0xC78, 0xE9150101 },
	{ 0xC78, 0xE8160101 },
	{ 0xC78, 0xE7170101 },
	{ 0xC78, 0xE6180101 },
	{ 0xC78, 0xE5190101 },
	{ 0xC78, 0xE41A0101 },
	{ 0xC78, 0xE31B0101 },
	{ 0xC78, 0xA81C0101 },
	{ 0xC78, 0xA71D0101 },
	{ 0xC78, 0xA61E0101 },
	{ 0xC78, 0xA51F0101 },
	{ 0xC78, 0xA4200101 },
	{ 0xC78, 0xA3210101 },
	{ 0xC78, 0xA2220101 },
	{ 0xC78, 0xA1230101 },
	{ 0xC78, 0x68240101 },
	{ 0xC78, 0x67250101 },
	{ 0xC78, 0x66260101 },
	{ 0xC78, 0x65270101 },
	{ 0xC78, 0x64280101 },
	{ 0xC78, 0x63290101 },
	{ 0xC78, 0x622A0101 },
	{ 0xC78, 0x612B0101 },
	{ 0xC78, 0x282C0101 },
	{ 0xC78, 0x272D0101 },
	{ 0xC78, 0x262E0101 },
	{ 0xC78, 0x252F0101 },
	{ 0xC78, 0x24300101 },
	{ 0xC78, 0x09310101 },
	{ 0xC78, 0x08320101 },
	{ 0xC78, 0x07330101 },
	{ 0xC78, 0x06340101 },
	{ 0xC78, 0x05350101 },
	{ 0xC78, 0x04360101 },
	{ 0xC78, 0x03370101 },
	{ 0xC78, 0x02380101 },
	{ 0xC78, 0x01390101 },
	{ 0xC78, 0x013A0101 },
	{ 0xC78, 0x013B0101 },
	{ 0xC78, 0x013C0101 },
	{ 0xC78, 0x013D0101 },
	{ 0xC78, 0x013E0101 },
	{ 0xC78, 0x013F0101 },		// Gain index 63 (minimum gain)

	// Path B AGC gain curve (register 0xE78)
	{ 0xE78, 0xFE000101 },
	{ 0xE78, 0xFD010101 },
	{ 0xE78, 0xFC020101 },
	{ 0xE78, 0xFB030101 },
	{ 0xE78, 0xFA040101 },
	{ 0xE78, 0xF9050101 },
	{ 0xE78, 0xF8060101 },
	{ 0xE78, 0xF7070101 },
	{ 0xE78, 0xF6080101 },
	{ 0xE78, 0xF5090101 },
	{ 0xE78, 0xF40A0101 },
	{ 0xE78, 0xF30B0101 },
	{ 0xE78, 0xF20C0101 },
	{ 0xE78, 0xF10D0101 },
	{ 0xE78, 0xF00E0101 },
	{ 0xE78, 0xEF0F0101 },
	{ 0xE78, 0xEE100101 },
	{ 0xE78, 0xED110101 },
	{ 0xE78, 0xEC120101 },
	{ 0xE78, 0xEB130101 },
	{ 0xE78, 0xEA140101 },
	{ 0xE78, 0xE9150101 },
	{ 0xE78, 0xE8160101 },
	{ 0xE78, 0xE7170101 },
	{ 0xE78, 0xE6180101 },
	{ 0xE78, 0xE5190101 },
	{ 0xE78, 0xE41A0101 },
	{ 0xE78, 0xE31B0101 },
	{ 0xE78, 0xA81C0101 },
	{ 0xE78, 0xA71D0101 },
	{ 0xE78, 0xA61E0101 },
	{ 0xE78, 0xA51F0101 },
	{ 0xE78, 0xA4200101 },
	{ 0xE78, 0xA3210101 },
	{ 0xE78, 0xA2220101 },
	{ 0xE78, 0xA1230101 },
	{ 0xE78, 0x68240101 },
	{ 0xE78, 0x67250101 },
	{ 0xE78, 0x66260101 },
	{ 0xE78, 0x65270101 },
	{ 0xE78, 0x64280101 },
	{ 0xE78, 0x63290101 },
	{ 0xE78, 0x622A0101 },
	{ 0xE78, 0x612B0101 },
	{ 0xE78, 0x282C0101 },
	{ 0xE78, 0x272D0101 },
	{ 0xE78, 0x262E0101 },
	{ 0xE78, 0x252F0101 },
	{ 0xE78, 0x24300101 },
	{ 0xE78, 0x09310101 },
	{ 0xE78, 0x08320101 },
	{ 0xE78, 0x07330101 },
	{ 0xE78, 0x06340101 },
	{ 0xE78, 0x05350101 },
	{ 0xE78, 0x04360101 },
	{ 0xE78, 0x03370101 },
	{ 0xE78, 0x02380101 },
	{ 0xE78, 0x01390101 },
	{ 0xE78, 0x013A0101 },
	{ 0xE78, 0x013B0101 },
	{ 0xE78, 0x013C0101 },
	{ 0xE78, 0x013D0101 },
	{ 0xE78, 0x013E0101 },
	{ 0xE78, 0x013F0101 },

	// Path C AGC gain curve (register 0x1878)
	{ 0x1878, 0xFE000101 },
	{ 0x1878, 0xFD010101 },
	{ 0x1878, 0xFC020101 },
	{ 0x1878, 0xFB030101 },
	{ 0x1878, 0xFA040101 },
	{ 0x1878, 0xF9050101 },
	{ 0x1878, 0xF8060101 },
	{ 0x1878, 0xF7070101 },
	{ 0x1878, 0xF6080101 },
	{ 0x1878, 0xF5090101 },
	{ 0x1878, 0xF40A0101 },
	{ 0x1878, 0xF30B0101 },
	{ 0x1878, 0xF20C0101 },
	{ 0x1878, 0xF10D0101 },
	{ 0x1878, 0xF00E0101 },
	{ 0x1878, 0xEF0F0101 },
	{ 0x1878, 0xEE100101 },
	{ 0x1878, 0xED110101 },
	{ 0x1878, 0xEC120101 },
	{ 0x1878, 0xEB130101 },
	{ 0x1878, 0xEA140101 },
	{ 0x1878, 0xE9150101 },
	{ 0x1878, 0xE8160101 },
	{ 0x1878, 0xE7170101 },
	{ 0x1878, 0xE6180101 },
	{ 0x1878, 0xE5190101 },
	{ 0x1878, 0xE41A0101 },
	{ 0x1878, 0xE31B0101 },
	{ 0x1878, 0xA81C0101 },
	{ 0x1878, 0xA71D0101 },
	{ 0x1878, 0xA61E0101 },
	{ 0x1878, 0xA51F0101 },
	{ 0x1878, 0xA4200101 },
	{ 0x1878, 0xA3210101 },
	{ 0x1878, 0xA2220101 },
	{ 0x1878, 0xA1230101 },
	{ 0x1878, 0x68240101 },
	{ 0x1878, 0x67250101 },
	{ 0x1878, 0x66260101 },
	{ 0x1878, 0x65270101 },
	{ 0x1878, 0x64280101 },
	{ 0x1878, 0x63290101 },
	{ 0x1878, 0x622A0101 },
	{ 0x1878, 0x612B0101 },
	{ 0x1878, 0x282C0101 },
	{ 0x1878, 0x272D0101 },
	{ 0x1878, 0x262E0101 },
	{ 0x1878, 0x252F0101 },
	{ 0x1878, 0x24300101 },
	{ 0x1878, 0x09310101 },
	{ 0x1878, 0x08320101 },
	{ 0x1878, 0x07330101 },
	{ 0x1878, 0x06340101 },
	{ 0x1878, 0x05350101 },
	{ 0x1878, 0x04360101 },
	{ 0x1878, 0x03370101 },
	{ 0x1878, 0x02380101 },
	{ 0x1878, 0x01390101 },
	{ 0x1878, 0x013A0101 },
	{ 0x1878, 0x013B0101 },
	{ 0x1878, 0x013C0101 },
	{ 0x1878, 0x013D0101 },
	{ 0x1878, 0x013E0101 },
	{ 0x1878, 0x013F0101 },

	// Path D AGC gain curve (register 0x1A78)
	{ 0x1A78, 0xFE000101 },
	{ 0x1A78, 0xFD010101 },
	{ 0x1A78, 0xFC020101 },
	{ 0x1A78, 0xFB030101 },
	{ 0x1A78, 0xFA040101 },
	{ 0x1A78, 0xF9050101 },
	{ 0x1A78, 0xF8060101 },
	{ 0x1A78, 0xF7070101 },
	{ 0x1A78, 0xF6080101 },
	{ 0x1A78, 0xF5090101 },
	{ 0x1A78, 0xF40A0101 },
	{ 0x1A78, 0xF30B0101 },
	{ 0x1A78, 0xF20C0101 },
	{ 0x1A78, 0xF10D0101 },
	{ 0x1A78, 0xF00E0101 },
	{ 0x1A78, 0xEF0F0101 },
	{ 0x1A78, 0xEE100101 },
	{ 0x1A78, 0xED110101 },
	{ 0x1A78, 0xEC120101 },
	{ 0x1A78, 0xEB130101 },
	{ 0x1A78, 0xEA140101 },
	{ 0x1A78, 0xE9150101 },
	{ 0x1A78, 0xE8160101 },
	{ 0x1A78, 0xE7170101 },
	{ 0x1A78, 0xE6180101 },
	{ 0x1A78, 0xE5190101 },
	{ 0x1A78, 0xE41A0101 },
	{ 0x1A78, 0xE31B0101 },
	{ 0x1A78, 0xA81C0101 },
	{ 0x1A78, 0xA71D0101 },
	{ 0x1A78, 0xA61E0101 },
	{ 0x1A78, 0xA51F0101 },
	{ 0x1A78, 0xA4200101 },
	{ 0x1A78, 0xA3210101 },
	{ 0x1A78, 0xA2220101 },
	{ 0x1A78, 0xA1230101 },
	{ 0x1A78, 0x68240101 },
	{ 0x1A78, 0x67250101 },
	{ 0x1A78, 0x66260101 },
	{ 0x1A78, 0x65270101 },
	{ 0x1A78, 0x64280101 },
	{ 0x1A78, 0x63290101 },
	{ 0x1A78, 0x622A0101 },
	{ 0x1A78, 0x612B0101 },
	{ 0x1A78, 0x282C0101 },
	{ 0x1A78, 0x272D0101 },
	{ 0x1A78, 0x262E0101 },
	{ 0x1A78, 0x252F0101 },
	{ 0x1A78, 0x24300101 },
	{ 0x1A78, 0x09310101 },
	{ 0x1A78, 0x08320101 },
	{ 0x1A78, 0x07330101 },
	{ 0x1A78, 0x06340101 },
	{ 0x1A78, 0x05350101 },
	{ 0x1A78, 0x04360101 },
	{ 0x1A78, 0x03370101 },
	{ 0x1A78, 0x02380101 },
	{ 0x1A78, 0x01390101 },
	{ 0x1A78, 0x013A0101 },
	{ 0x1A78, 0x013B0101 },
	{ 0x1A78, 0x013C0101 },
	{ 0x1A78, 0x013D0101 },
	{ 0x1A78, 0x013E0101 },
	{ 0x1A78, 0x013F0101 },
};

static const uint32 kAGCTable2GCount
	= sizeof(kAGCTable2G) / sizeof(kAGCTable2G[0]);


// AGC table for 5 GHz operation — loaded when switching to 5 GHz band.
// The 5 GHz paths have different LNA gain steps because the RF front-end
// uses different external PA/LNA components for the higher frequency band.
static const PhyRegEntry kAGCTable5G[] = {
	// Path A (0xC78) — 5 GHz gain curve
	{ 0xC78, 0xFE000101 },
	{ 0xC78, 0xFD010101 },
	{ 0xC78, 0xFC020101 },
	{ 0xC78, 0xFB030101 },
	{ 0xC78, 0xFA040101 },
	{ 0xC78, 0xF9050101 },
	{ 0xC78, 0xF8060101 },
	{ 0xC78, 0xF7070101 },
	{ 0xC78, 0xF6080101 },
	{ 0xC78, 0xF5090101 },
	{ 0xC78, 0xF40A0101 },
	{ 0xC78, 0xF30B0101 },
	{ 0xC78, 0xF20C0101 },
	{ 0xC78, 0xF10D0101 },
	{ 0xC78, 0xF00E0101 },
	{ 0xC78, 0xEF0F0101 },
	{ 0xC78, 0xEE100101 },
	{ 0xC78, 0xED110101 },
	{ 0xC78, 0xEC120101 },
	{ 0xC78, 0xEB130101 },
	{ 0xC78, 0xEA140101 },
	{ 0xC78, 0xE9150101 },
	{ 0xC78, 0xE8160101 },
	{ 0xC78, 0xE7170101 },
	{ 0xC78, 0xE6180101 },
	{ 0xC78, 0xE5190101 },
	{ 0xC78, 0xE41A0101 },
	{ 0xC78, 0xE31B0101 },
	{ 0xC78, 0xA81C0101 },
	{ 0xC78, 0xA71D0101 },
	{ 0xC78, 0xA61E0101 },
	{ 0xC78, 0xA51F0101 },
	{ 0xC78, 0xA4200101 },
	{ 0xC78, 0xA3210101 },
	{ 0xC78, 0xA2220101 },
	{ 0xC78, 0xA1230101 },
	{ 0xC78, 0x68240101 },
	{ 0xC78, 0x67250101 },
	{ 0xC78, 0x66260101 },
	{ 0xC78, 0x65270101 },
	{ 0xC78, 0x64280101 },
	{ 0xC78, 0x63290101 },
	{ 0xC78, 0x622A0101 },
	{ 0xC78, 0x612B0101 },
	{ 0xC78, 0x472C0101 },
	{ 0xC78, 0x462D0101 },
	{ 0xC78, 0x452E0101 },
	{ 0xC78, 0x442F0101 },
	{ 0xC78, 0x43300101 },
	{ 0xC78, 0x28310101 },
	{ 0xC78, 0x27320101 },
	{ 0xC78, 0x26330101 },
	{ 0xC78, 0x25340101 },
	{ 0xC78, 0x24350101 },
	{ 0xC78, 0x09360101 },
	{ 0xC78, 0x08370101 },
	{ 0xC78, 0x07380101 },
	{ 0xC78, 0x06390101 },
	{ 0xC78, 0x053A0101 },
	{ 0xC78, 0x043B0101 },
	{ 0xC78, 0x033C0101 },
	{ 0xC78, 0x023D0101 },
	{ 0xC78, 0x013E0101 },
	{ 0xC78, 0x013F0101 },

	// Path B (0xE78) — 5 GHz gain curve
	{ 0xE78, 0xFE000101 },
	{ 0xE78, 0xFD010101 },
	{ 0xE78, 0xFC020101 },
	{ 0xE78, 0xFB030101 },
	{ 0xE78, 0xFA040101 },
	{ 0xE78, 0xF9050101 },
	{ 0xE78, 0xF8060101 },
	{ 0xE78, 0xF7070101 },
	{ 0xE78, 0xF6080101 },
	{ 0xE78, 0xF5090101 },
	{ 0xE78, 0xF40A0101 },
	{ 0xE78, 0xF30B0101 },
	{ 0xE78, 0xF20C0101 },
	{ 0xE78, 0xF10D0101 },
	{ 0xE78, 0xF00E0101 },
	{ 0xE78, 0xEF0F0101 },
	{ 0xE78, 0xEE100101 },
	{ 0xE78, 0xED110101 },
	{ 0xE78, 0xEC120101 },
	{ 0xE78, 0xEB130101 },
	{ 0xE78, 0xEA140101 },
	{ 0xE78, 0xE9150101 },
	{ 0xE78, 0xE8160101 },
	{ 0xE78, 0xE7170101 },
	{ 0xE78, 0xE6180101 },
	{ 0xE78, 0xE5190101 },
	{ 0xE78, 0xE41A0101 },
	{ 0xE78, 0xE31B0101 },
	{ 0xE78, 0xA81C0101 },
	{ 0xE78, 0xA71D0101 },
	{ 0xE78, 0xA61E0101 },
	{ 0xE78, 0xA51F0101 },
	{ 0xE78, 0xA4200101 },
	{ 0xE78, 0xA3210101 },
	{ 0xE78, 0xA2220101 },
	{ 0xE78, 0xA1230101 },
	{ 0xE78, 0x68240101 },
	{ 0xE78, 0x67250101 },
	{ 0xE78, 0x66260101 },
	{ 0xE78, 0x65270101 },
	{ 0xE78, 0x64280101 },
	{ 0xE78, 0x63290101 },
	{ 0xE78, 0x622A0101 },
	{ 0xE78, 0x612B0101 },
	{ 0xE78, 0x472C0101 },
	{ 0xE78, 0x462D0101 },
	{ 0xE78, 0x452E0101 },
	{ 0xE78, 0x442F0101 },
	{ 0xE78, 0x43300101 },
	{ 0xE78, 0x28310101 },
	{ 0xE78, 0x27320101 },
	{ 0xE78, 0x26330101 },
	{ 0xE78, 0x25340101 },
	{ 0xE78, 0x24350101 },
	{ 0xE78, 0x09360101 },
	{ 0xE78, 0x08370101 },
	{ 0xE78, 0x07380101 },
	{ 0xE78, 0x06390101 },
	{ 0xE78, 0x053A0101 },
	{ 0xE78, 0x043B0101 },
	{ 0xE78, 0x033C0101 },
	{ 0xE78, 0x023D0101 },
	{ 0xE78, 0x013E0101 },
	{ 0xE78, 0x013F0101 },

	// Path C (0x1878) — 5 GHz gain curve
	{ 0x1878, 0xFE000101 },
	{ 0x1878, 0xFD010101 },
	{ 0x1878, 0xFC020101 },
	{ 0x1878, 0xFB030101 },
	{ 0x1878, 0xFA040101 },
	{ 0x1878, 0xF9050101 },
	{ 0x1878, 0xF8060101 },
	{ 0x1878, 0xF7070101 },
	{ 0x1878, 0xF6080101 },
	{ 0x1878, 0xF5090101 },
	{ 0x1878, 0xF40A0101 },
	{ 0x1878, 0xF30B0101 },
	{ 0x1878, 0xF20C0101 },
	{ 0x1878, 0xF10D0101 },
	{ 0x1878, 0xF00E0101 },
	{ 0x1878, 0xEF0F0101 },
	{ 0x1878, 0xEE100101 },
	{ 0x1878, 0xED110101 },
	{ 0x1878, 0xEC120101 },
	{ 0x1878, 0xEB130101 },
	{ 0x1878, 0xEA140101 },
	{ 0x1878, 0xE9150101 },
	{ 0x1878, 0xE8160101 },
	{ 0x1878, 0xE7170101 },
	{ 0x1878, 0xE6180101 },
	{ 0x1878, 0xE5190101 },
	{ 0x1878, 0xE41A0101 },
	{ 0x1878, 0xE31B0101 },
	{ 0x1878, 0xA81C0101 },
	{ 0x1878, 0xA71D0101 },
	{ 0x1878, 0xA61E0101 },
	{ 0x1878, 0xA51F0101 },
	{ 0x1878, 0xA4200101 },
	{ 0x1878, 0xA3210101 },
	{ 0x1878, 0xA2220101 },
	{ 0x1878, 0xA1230101 },
	{ 0x1878, 0x68240101 },
	{ 0x1878, 0x67250101 },
	{ 0x1878, 0x66260101 },
	{ 0x1878, 0x65270101 },
	{ 0x1878, 0x64280101 },
	{ 0x1878, 0x63290101 },
	{ 0x1878, 0x622A0101 },
	{ 0x1878, 0x612B0101 },
	{ 0x1878, 0x472C0101 },
	{ 0x1878, 0x462D0101 },
	{ 0x1878, 0x452E0101 },
	{ 0x1878, 0x442F0101 },
	{ 0x1878, 0x43300101 },
	{ 0x1878, 0x28310101 },
	{ 0x1878, 0x27320101 },
	{ 0x1878, 0x26330101 },
	{ 0x1878, 0x25340101 },
	{ 0x1878, 0x24350101 },
	{ 0x1878, 0x09360101 },
	{ 0x1878, 0x08370101 },
	{ 0x1878, 0x07380101 },
	{ 0x1878, 0x06390101 },
	{ 0x1878, 0x053A0101 },
	{ 0x1878, 0x043B0101 },
	{ 0x1878, 0x033C0101 },
	{ 0x1878, 0x023D0101 },
	{ 0x1878, 0x013E0101 },
	{ 0x1878, 0x013F0101 },

	// Path D (0x1A78) — 5 GHz gain curve
	{ 0x1A78, 0xFE000101 },
	{ 0x1A78, 0xFD010101 },
	{ 0x1A78, 0xFC020101 },
	{ 0x1A78, 0xFB030101 },
	{ 0x1A78, 0xFA040101 },
	{ 0x1A78, 0xF9050101 },
	{ 0x1A78, 0xF8060101 },
	{ 0x1A78, 0xF7070101 },
	{ 0x1A78, 0xF6080101 },
	{ 0x1A78, 0xF5090101 },
	{ 0x1A78, 0xF40A0101 },
	{ 0x1A78, 0xF30B0101 },
	{ 0x1A78, 0xF20C0101 },
	{ 0x1A78, 0xF10D0101 },
	{ 0x1A78, 0xF00E0101 },
	{ 0x1A78, 0xEF0F0101 },
	{ 0x1A78, 0xEE100101 },
	{ 0x1A78, 0xED110101 },
	{ 0x1A78, 0xEC120101 },
	{ 0x1A78, 0xEB130101 },
	{ 0x1A78, 0xEA140101 },
	{ 0x1A78, 0xE9150101 },
	{ 0x1A78, 0xE8160101 },
	{ 0x1A78, 0xE7170101 },
	{ 0x1A78, 0xE6180101 },
	{ 0x1A78, 0xE5190101 },
	{ 0x1A78, 0xE41A0101 },
	{ 0x1A78, 0xE31B0101 },
	{ 0x1A78, 0xA81C0101 },
	{ 0x1A78, 0xA71D0101 },
	{ 0x1A78, 0xA61E0101 },
	{ 0x1A78, 0xA51F0101 },
	{ 0x1A78, 0xA4200101 },
	{ 0x1A78, 0xA3210101 },
	{ 0x1A78, 0xA2220101 },
	{ 0x1A78, 0xA1230101 },
	{ 0x1A78, 0x68240101 },
	{ 0x1A78, 0x67250101 },
	{ 0x1A78, 0x66260101 },
	{ 0x1A78, 0x65270101 },
	{ 0x1A78, 0x64280101 },
	{ 0x1A78, 0x63290101 },
	{ 0x1A78, 0x622A0101 },
	{ 0x1A78, 0x612B0101 },
	{ 0x1A78, 0x472C0101 },
	{ 0x1A78, 0x462D0101 },
	{ 0x1A78, 0x452E0101 },
	{ 0x1A78, 0x442F0101 },
	{ 0x1A78, 0x43300101 },
	{ 0x1A78, 0x28310101 },
	{ 0x1A78, 0x27320101 },
	{ 0x1A78, 0x26330101 },
	{ 0x1A78, 0x25340101 },
	{ 0x1A78, 0x24350101 },
	{ 0x1A78, 0x09360101 },
	{ 0x1A78, 0x08370101 },
	{ 0x1A78, 0x07380101 },
	{ 0x1A78, 0x06390101 },
	{ 0x1A78, 0x053A0101 },
	{ 0x1A78, 0x043B0101 },
	{ 0x1A78, 0x033C0101 },
	{ 0x1A78, 0x023D0101 },
	{ 0x1A78, 0x013E0101 },
	{ 0x1A78, 0x013F0101 },
};

static const uint32 kAGCTable5GCount
	= sizeof(kAGCTable5G) / sizeof(kAGCTable5G[0]);


// ===========================================================================
// RF Transceiver Register Tables
//
// Each of the 4 RF paths has its own transceiver configuration. The
// transceivers are accessed indirectly via the BB register interface
// (see _WriteRF() / _ReadRF() in PhyConfig.cpp).
//
// The tables configure:
//   - PLL and synthesizer (frequency generation)
//   - PA (power amplifier) bias and gain
//   - LNA (low noise amplifier) settings
//   - Mixer and filter bandwidth
//   - AGC interface
//   - Calibration coefficients
//
// Paths A–D share most settings but have small path-specific
// differences in calibration trim values (PA/LNA offsets vary per
// physical path due to PCB trace length, component tolerance, etc.).
//
// Reference: Array_MP_8814A_Radio{A,B,C,D} in halhwimg8814a_rf.c
// ===========================================================================

// Common RF initialization sequence shared by all 4 paths.
// These registers configure the synthesizer, PLL, and basic RF mode.
static const PhyRegEntry kRFInitCommon[] = {
	// Synthesizer and PLL configuration
	{ 0x00, 0x00030 },			// RF mode: standby → active
	{ 0x18, 0x13124 },			// Channel synthesizer: ch 1, 20 MHz, 2.4 GHz
	{ 0x55, 0x03F78 },			// TX LO leakage calibration
	{ 0x56, 0x50000 },			// TX gain default
	{ 0x57, 0xD8000 },			// TX DC offset calibration
	{ 0x58, 0xBE180 },			// RX filter bandwidth
	{ 0x5A, 0x04800 },			// RX DC offset
	{ 0x5B, 0x00000 },			// Reserved (must clear)
	{ 0x5C, 0x00000 },			// Reserved

	// PA (power amplifier) configuration
	{ 0x60, 0x00000 },			// PA bias control
	{ 0x61, 0x00000 },			// PA dynamic bias
	{ 0x62, 0x00000 },			// PA ramp control
	{ 0x63, 0x00000 },			// Reserved
	{ 0x64, 0x00000 },			// Reserved
	{ 0x65, 0x20000 },			// PA enable
	{ 0x66, 0x00000 },			// Reserved

	// LNA and mixer configuration
	{ 0x70, 0x49022 },			// LNA input matching
	{ 0x71, 0x18C80 },			// LNA bias current
	{ 0x72, 0x00100 },			// LNA gain control
	{ 0x73, 0x20E00 },			// Mixer bias current
	{ 0x74, 0x00000 },			// Reserved
	{ 0x75, 0x00000 },			// Reserved
	{ 0x76, 0x00000 },			// Reserved

	// AGC and RSSI configuration
	{ 0x7F, 0x00068 },			// AGC timing control
	{ 0x80, 0x00000 },			// Reserved
	{ 0x81, 0x00000 },			// Reserved
	{ 0x82, 0x00000 },			// RSSI indicator

	// PLL lock and settling
	{ 0x83, 0x00000 },			// PLL CP current
	{ 0x84, 0x00000 },			// PLL loop bandwidth
	{ 0x85, 0x00000 },			// PLL lock detect threshold
	{ 0x86, 0x00000 },			// PLL settling time

	// Baseband interface
	{ 0x8F, 0xADB00 },			// BB-RF interface timing
	{ 0xB0, 0xFFFFE },			// AGC look-up table enable
	{ 0xB1, 0x3FF48 },			// AGC target power
	{ 0xB2, 0x6AA3F },			// AGC high threshold
	{ 0xB3, 0xFFC9A },			// AGC low threshold
	{ 0xB4, 0x0A78F },			// AGC mid threshold
	{ 0xB5, 0x00A3F },			// AGC settling window
	{ 0xB6, 0x0C09C },			// AGC mode select
	{ 0xB7, 0x30008 },			// AGC control 2 (default/else)

	// Enable RF and go to normal operation
	{ 0x00, 0x00033 },			// RF mode: active TX+RX
};

static const uint32 kRFInitCommonCount
	= sizeof(kRFInitCommon) / sizeof(kRFInitCommon[0]);


// Path-specific RF trim tables. These adjust calibration values that
// differ between paths due to PCB trace length, component tolerance,
// and antenna placement. Applied after the common table.

// Path A — typically the "primary" or reference path
static const PhyRegEntry kRFInitPathA[] = {
	{ 0x08, 0x01000 },			// Path A TX DC offset trim
	{ 0x09, 0x0C4FF },			// Path A TX IQ trim
	{ 0x0A, 0x80510 },			// Path A RX DC offset trim
	{ 0x0B, 0xE2020 },			// Path A RX IQ trim
	{ 0x0C, 0x01C1C },			// Path A filter calibration
	{ 0x0E, 0x02000 },			// Path A PA bias trim
	{ 0x1E, 0x00034 },			// Path A LNA gain offset
	{ 0xDF, 0x00000 },			// Path A: LNA for 2.4 GHz (default)
};

static const uint32 kRFInitPathACount
	= sizeof(kRFInitPathA) / sizeof(kRFInitPathA[0]);


// Path B
static const PhyRegEntry kRFInitPathB[] = {
	{ 0x08, 0x01000 },			// Path B TX DC offset trim
	{ 0x09, 0x0C4FF },			// Path B TX IQ trim
	{ 0x0A, 0x80510 },			// Path B RX DC offset trim
	{ 0x0B, 0xE2020 },			// Path B RX IQ trim
	{ 0x0C, 0x01C1C },			// Path B filter calibration
	{ 0x0E, 0x02000 },			// Path B PA bias trim
	{ 0x1E, 0x00034 },			// Path B LNA gain offset
	{ 0xDF, 0x00000 },			// Path B: LNA for 2.4 GHz (default)
};

static const uint32 kRFInitPathBCount
	= sizeof(kRFInitPathB) / sizeof(kRFInitPathB[0]);


// Path C
static const PhyRegEntry kRFInitPathC[] = {
	{ 0x08, 0x01000 },			// Path C TX DC offset trim
	{ 0x09, 0x0C4FF },			// Path C TX IQ trim
	{ 0x0A, 0x80510 },			// Path C RX DC offset trim
	{ 0x0B, 0xE2020 },			// Path C RX IQ trim
	{ 0x0C, 0x01C1C },			// Path C filter calibration
	{ 0x0E, 0x02000 },			// Path C PA bias trim
	{ 0x1E, 0x00034 },			// Path C LNA gain offset
	{ 0xDF, 0x00000 },			// Path C: LNA for 2.4 GHz (default)
};

static const uint32 kRFInitPathCCount
	= sizeof(kRFInitPathC) / sizeof(kRFInitPathC[0]);


// Path D
static const PhyRegEntry kRFInitPathD[] = {
	{ 0x08, 0x01000 },			// Path D TX DC offset trim
	{ 0x09, 0x0C4FF },			// Path D TX IQ trim
	{ 0x0A, 0x80510 },			// Path D RX DC offset trim
	{ 0x0B, 0xE2020 },			// Path D RX IQ trim
	{ 0x0C, 0x01C1C },			// Path D filter calibration
	{ 0x0E, 0x02000 },			// Path D PA bias trim
	{ 0x1E, 0x00034 },			// Path D LNA gain offset
	{ 0xDF, 0x00000 },			// Path D: LNA for 2.4 GHz (default)
};

static const uint32 kRFInitPathDCount
	= sizeof(kRFInitPathD) / sizeof(kRFInitPathD[0]);


// Convenience arrays for indexed path access
static const PhyRegEntry* kRFInitPerPath[kRfPathCount] = {
	kRFInitPathA, kRFInitPathB, kRFInitPathC, kRFInitPathD
};

static const uint32 kRFInitPerPathCount[kRfPathCount] = {
	kRFInitPathACount, kRFInitPathBCount,
	kRFInitPathCCount, kRFInitPathDCount
};


#endif	// RTL8814AU_PHY_REG_TABLES_H
