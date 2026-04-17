/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Firmware.h — RTL8814AU firmware loading.
 *
 * The RTL8814AU contains a Lexra 3081 MIPS-derived CPU that runs firmware
 * for MLME management, rate adaptation, and power control. The firmware
 * binary contains two sections:
 *   - DMEM (Data Memory, ≤ 32 KB)
 *   - IRAM (Instruction Memory, ≤ 64 KB)
 *
 * Unlike older Realtek chips that use an 8051 with page-based firmware
 * loading, the 3081 core uses the chip's internal DMA engine to transfer
 * each section independently. See docs/diagrams/firmware_load.svg for the
 * full sequence diagram.
 */
#ifndef RTL8814AU_FIRMWARE_H
#define RTL8814AU_FIRMWARE_H


#include <SupportDefs.h>

#include "RTL8814AU.h"


class RTL8814AURegisterIO;


// Firmware binary header — stored at the beginning of the .bin file.
// Fields are little-endian.
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
	uint16	ramCodeSize;	// IRAM section size in bytes
	uint16	reserved2;
	uint32	svnIndex;		// SVN revision
	uint32	reserved3;
	uint32	reserved4;
	uint32	reserved5;
	uint32	dmemSize;		// DMEM section size in bytes
};


class RTL8814AUFirmware {
public:
								RTL8814AUFirmware(
									RTL8814AURegisterIO* registerIO);
								~RTL8814AUFirmware();

	// Load firmware from the given filesystem path. Reads the file,
	// validates the header, and transfers DMEM + IRAM to the chip.
	// Returns B_OK on success.
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
	status_t					_HaltMCU();
	status_t					_ResumeMCU();
	status_t					_DmaTransferSection(const uint8* data,
									uint32 size, uint32 destAddress);
	status_t					_PollForReady();

	RTL8814AURegisterIO*		fRegisterIO;

	// Firmware file contents (allocated during _ReadFile, freed after
	// successful load since the data is now in chip memory)
	uint8*						fData;
	uint32						fDataSize;

	// Parsed header info
	bool						fLoaded;
	uint16						fVersion;
	uint32						fDmemOffset;
	uint32						fDmemSize;
	uint32						fIramOffset;
	uint32						fIramSize;
};


#endif	// RTL8814AU_FIRMWARE_H
