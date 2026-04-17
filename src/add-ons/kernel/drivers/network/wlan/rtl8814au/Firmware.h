/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Firmware.h — RTL8814AU firmware loading.
 *
 * The RTL8814AU contains a Lexra 3081 MIPS-derived CPU that runs firmware
 * for MLME management, rate adaptation, and power control. The firmware
 * binary has a 32-byte header followed by a single contiguous payload blob.
 *
 * Loading sequence:
 *   1. Enable download mode (set MCUFWDL_EN, reset MCU)
 *   2. Write payload in 4 KB pages via register writes to 0x1000
 *   3. Disable download mode
 *   4. Poll until MCU signals checksum OK and init complete
 *
 * Reference: rtl8814a_hal_init.c — FirmwareDownload8814A() in
 *            ulli-kroll/rtl8814au.
 */
#ifndef RTL8814AU_FIRMWARE_H
#define RTL8814AU_FIRMWARE_H


#include <SupportDefs.h>

#include "RTL8814AU.h"


class RTL8814AURegisterIO;


// Firmware binary header — 32 bytes at the start of the .bin file.
// Fields are little-endian. Matches RT_8814A_FIRMWARE_HDR in the
// reference driver.
struct RTL8814AUFirmwareHeader {
	uint16	signature;		// Expected: 0x8814
	uint8	category;		// Firmware category
	uint8	function;		// Function version
	uint16	version;		// Firmware version
	uint8	subversion;		// Sub-version
	uint8	reserved1;
	uint8	month;			// Build month
	uint8	day;			// Build day
	uint8	hour;			// Build hour
	uint8	minute;			// Build minute
	uint16	ramCodeSize;	// RAM code size (informational)
	uint16	reserved2;
	uint32	svnIndex;		// SVN revision
	uint32	reserved3;
	uint32	reserved4;
	uint32	reserved5;
};	// 32 bytes total


class RTL8814AUFirmware {
public:
								RTL8814AUFirmware(
									RTL8814AURegisterIO* registerIO);
								~RTL8814AUFirmware();

	// Load firmware from the given filesystem path. Reads the file,
	// validates the header, and writes the payload to the chip via
	// page-based register writes. Returns B_OK on success.
	status_t					Load(const char* firmwarePath);

	// Query firmware status after loading
	bool						IsLoaded() const { return fLoaded; }
	uint16						Version() const { return fVersion; }

private:
	// Internal steps of the load sequence
	status_t					_ReadFile(const char* path);
	status_t					_ValidateHeader();
	status_t					_EnableDownloadMode();
	status_t					_DisableDownloadMode();
	status_t					_PageWriteFirmware(const uint8* data,
									uint32 size);
	status_t					_WritePage(uint32 page, const uint8* data,
									uint32 size);
	status_t					_PollForReady();

	RTL8814AURegisterIO*		fRegisterIO;

	// Firmware file contents (allocated during _ReadFile, freed after
	// successful load since the data is now in chip memory)
	uint8*						fData;
	uint32						fDataSize;

	// Parsed header info
	bool						fLoaded;
	uint16						fVersion;
	uint32						fPayloadOffset;
	uint32						fPayloadSize;
};


#endif	// RTL8814AU_FIRMWARE_H
