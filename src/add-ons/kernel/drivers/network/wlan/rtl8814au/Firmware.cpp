/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Firmware.cpp — RTL8814AU firmware loading implementation.
 *
 * The loading sequence for the Lexra 3081 MCU:
 *   1. Read firmware binary from filesystem
 *   2. Validate the 32-byte header (signature 0x8814)
 *   3. Enable firmware download mode + reset MCU
 *   4. Write the payload in 4 KB pages via register writes to 0x1000
 *   5. Disable download mode
 *   6. Poll until MCU reports checksum OK, then signal ready, then
 *      wait for WINTINI_RDY (firmware init complete)
 *
 * The page-based write mechanism:
 *   - Select a page (0–7) by writing to bits [18:16] of REG_MCUFWDL
 *   - Write up to 4 KB of data to addresses 0x1000–0x1FFF
 *   - Advance to the next page and repeat
 *
 * Reference: rtl8814a_hal_init.c — FirmwareDownload8814A(),
 *            _FWDownloadEnable_8814A(), _WriteFW_8814A(),
 *            _PageWrite_8814A(), _FWFreeToGo8814A()
 *            in ulli-kroll/rtl8814au.
 */

#include "Firmware.h"

#include <fcntl.h>
#include <new>
#include <string.h>
#include <unistd.h>

#include <ByteOrder.h>
#include <KernelExport.h>
#include <OS.h>

#include "RegisterIO.h"


RTL8814AUFirmware::RTL8814AUFirmware(RTL8814AURegisterIO* registerIO)
	:
	fRegisterIO(registerIO),
	fData(NULL),
	fDataSize(0),
	fLoaded(false),
	fVersion(0),
	fPayloadOffset(0),
	fPayloadSize(0)
{
}


RTL8814AUFirmware::~RTL8814AUFirmware()
{
	delete[] fData;
}


// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------


/*! Load firmware from the given path, validate, and transfer to chip.
    \param firmwarePath  Absolute path to the firmware binary
    \return B_OK on success, or an error code describing what failed.
*/
status_t
RTL8814AUFirmware::Load(const char* firmwarePath)
{
	if (fLoaded)
		return B_OK;

	dprintf(RTL8814AU_DRIVER_NAME ": loading firmware from %s\n",
		firmwarePath);

	// Step 1: Read the firmware file into memory
	status_t status = _ReadFile(firmwarePath);
	if (status != B_OK)
		return status;

	// Step 2: Validate the header
	status = _ValidateHeader();
	if (status != B_OK) {
		delete[] fData;
		fData = NULL;
		return status;
	}

	dprintf(RTL8814AU_DRIVER_NAME ": firmware version %u, "
		"payload %" B_PRIu32 " bytes\n", fVersion, fPayloadSize);

	// Step 3: Enable download mode (sets MCUFWDL_EN + resets MCU)
	status = _EnableDownloadMode();
	if (status != B_OK)
		goto cleanup;

	// Step 4: Write the firmware payload via page-based register writes
	dprintf(RTL8814AU_DRIVER_NAME ": writing firmware "
		"(%" B_PRIu32 " bytes, %" B_PRIu32 " pages)\n",
		fPayloadSize, (fPayloadSize + kFwPageSize - 1) / kFwPageSize);

	status = _PageWriteFirmware(fData + fPayloadOffset, fPayloadSize);
	if (status != B_OK)
		goto cleanup;

	// Step 5: Disable download mode
	status = _DisableDownloadMode();
	if (status != B_OK)
		goto cleanup;

	// Step 6: Poll until firmware signals ready
	status = _PollForReady();
	if (status != B_OK)
		goto cleanup;

	fLoaded = true;
	dprintf(RTL8814AU_DRIVER_NAME ": firmware loaded successfully\n");

	// Free the file data — it's now in chip memory
	delete[] fData;
	fData = NULL;
	fDataSize = 0;

	return B_OK;

cleanup:
	dprintf(RTL8814AU_DRIVER_NAME ": firmware load failed: %s\n",
		strerror(status));
	_DisableDownloadMode();
	delete[] fData;
	fData = NULL;
	fDataSize = 0;
	return status;
}


// ---------------------------------------------------------------------------
// Internal implementation
// ---------------------------------------------------------------------------


/*! Read the firmware binary file into fData/fDataSize. */
status_t
RTL8814AUFirmware::_ReadFile(const char* path)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		dprintf(RTL8814AU_DRIVER_NAME ": cannot open firmware file: %s\n",
			path);
		return B_ENTRY_NOT_FOUND;
	}

	// Get file size
	off_t fileSize = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);

	if (fileSize <= 0 || (uint32)fileSize > kMaxFirmwareSize) {
		dprintf(RTL8814AU_DRIVER_NAME ": firmware file size invalid: "
			"%" B_PRIdOFF " bytes (max %" B_PRIu32 ")\n",
			fileSize, kMaxFirmwareSize);
		close(fd);
		return B_BAD_DATA;
	}

	fData = new(std::nothrow) uint8[fileSize];
	if (fData == NULL) {
		close(fd);
		return B_NO_MEMORY;
	}

	ssize_t bytesRead = read(fd, fData, fileSize);
	close(fd);

	if (bytesRead != fileSize) {
		dprintf(RTL8814AU_DRIVER_NAME ": short read: expected "
			"%" B_PRIdOFF ", got %zd\n", fileSize, bytesRead);
		delete[] fData;
		fData = NULL;
		return B_IO_ERROR;
	}

	fDataSize = (uint32)fileSize;
	return B_OK;
}


/*! Validate the 32-byte firmware header and compute payload offset/size. */
status_t
RTL8814AUFirmware::_ValidateHeader()
{
	if (fDataSize < kFwHeaderSize) {
		dprintf(RTL8814AU_DRIVER_NAME ": firmware too small for header "
			"(%" B_PRIu32 " < %u)\n", fDataSize, kFwHeaderSize);
		return B_BAD_DATA;
	}

	const RTL8814AUFirmwareHeader* header
		= reinterpret_cast<const RTL8814AUFirmwareHeader*>(fData);

	// Check signature — expect 0x8814 for RTL8814AU firmware
	uint16 signature = B_LENDIAN_TO_HOST_INT16(header->signature);
	if (signature != kRTL8814AU_ChipID) {
		dprintf(RTL8814AU_DRIVER_NAME ": bad firmware signature: 0x%04x "
			"(expected 0x%04x)\n", signature, kRTL8814AU_ChipID);
		return B_BAD_DATA;
	}

	fVersion = B_LENDIAN_TO_HOST_INT16(header->version);

	// Everything after the 32-byte header is the firmware payload
	fPayloadOffset = kFwHeaderSize;
	fPayloadSize = fDataSize - kFwHeaderSize;

	if (fPayloadSize == 0) {
		dprintf(RTL8814AU_DRIVER_NAME ": firmware has no payload\n");
		return B_BAD_DATA;
	}

	return B_OK;
}


/*! Enable firmware download mode: set the download enable bit and
    reset the MCU so we can safely write to its memory.
    Matches _FWDownloadEnable_8814A(enable=true) in the reference driver.
*/
status_t
RTL8814AUFirmware::_EnableDownloadMode()
{
	// Set firmware download enable (bit 0 of REG_MCUFWDL)
	uint32 ctrl = fRegisterIO->Read32(kRegMcuFwDl);
	ctrl |= kMcuFwDlEn;
	status_t status = fRegisterIO->Write32(kRegMcuFwDl, ctrl);
	if (status != B_OK)
		return status;

	// Reset MCU (clear bit 19 — MCU running flag)
	ctrl = fRegisterIO->Read32(kRegMcuFwDl);
	ctrl &= ~kMcuRst8051;
	return fRegisterIO->Write32(kRegMcuFwDl, ctrl);
}


/*! Disable firmware download mode after transfer is complete.
    Matches _FWDownloadEnable_8814A(enable=false) in the reference driver.
*/
status_t
RTL8814AUFirmware::_DisableDownloadMode()
{
	uint32 ctrl = fRegisterIO->Read32(kRegMcuFwDl);
	ctrl &= ~kMcuFwDlEn;
	return fRegisterIO->Write32(kRegMcuFwDl, ctrl);
}


/*! Write the firmware payload to chip memory using page-based register
    writes. The payload is split into 4 KB pages; for each page we set
    the page number in REG_MCUFWDL and write data to 0x1000.

    Matches _WriteFW_8814A() in the reference driver.
*/
status_t
RTL8814AUFirmware::_PageWriteFirmware(const uint8* data, uint32 size)
{
	uint32 pageCount = size / kFwPageSize;
	uint32 remainSize = size % kFwPageSize;

	for (uint32 page = 0; page < pageCount; page++) {
		status_t status = _WritePage(page,
			data + page * kFwPageSize, kFwPageSize);
		if (status != B_OK)
			return status;
	}

	if (remainSize > 0) {
		status_t status = _WritePage(pageCount,
			data + pageCount * kFwPageSize, remainSize);
		if (status != B_OK)
			return status;
	}

	return B_OK;
}


/*! Write a single 4 KB page of firmware data. Sets the page number in
    REG_MCUFWDL, then writes data word-by-word to kFwStartAddress (0x1000).

    Matches _PageWrite_8814A() + _BlockWrite_8814A() in the reference driver.
*/
status_t
RTL8814AUFirmware::_WritePage(uint32 page, const uint8* data, uint32 size)
{
	// Select page number in bits [18:16] of REG_MCUFWDL
	uint32 ctrl = fRegisterIO->Read32(kRegMcuFwDl);
	ctrl = (ctrl & ~kMcuFwDlPageMask)
		| ((page & 0x07) << kMcuFwDlPageShift);
	status_t status = fRegisterIO->Write32(kRegMcuFwDl, ctrl);
	if (status != B_OK)
		return status;

	// Write data to kFwStartAddress in 4-byte words
	for (uint32 i = 0; i < size; i += 4) {
		uint32 word = 0;
		uint32 remaining = size - i;
		if (remaining >= 4) {
			memcpy(&word, data + i, 4);
		} else {
			// Partial last word — zero-pad
			memcpy(&word, data + i, remaining);
		}
		status = fRegisterIO->Write32(kFwStartAddress + i, word);
		if (status != B_OK) {
			dprintf(RTL8814AU_DRIVER_NAME ": page write failed at "
				"page %" B_PRIu32 " offset %" B_PRIu32 "\n", page, i);
			return status;
		}
	}

	return B_OK;
}


/*! Poll the firmware control register through three stages:
    1. Wait for checksum report (MCU has verified the downloaded firmware)
    2. Set MCUFWDL_RDY and clear WINTINI_RDY to signal MCU to start init
    3. Wait for WINTINI_RDY (firmware has finished initialization)

    Matches _FWFreeToGo8814A() in the reference driver.
*/
status_t
RTL8814AUFirmware::_PollForReady()
{
	dprintf(RTL8814AU_DRIVER_NAME ": waiting for firmware checksum\n");

	// Stage 1: Wait for checksum report from MCU
	uint32 attempts = 0;
	while (attempts < kFirmwarePollAttempts) {
		uint32 ctrl = fRegisterIO->Read32(kRegMcuFwDl);
		if (ctrl & kMcuFwDlChksumRpt)
			break;
		snooze(kFirmwarePollDelay);
		attempts++;
	}
	if (attempts >= kFirmwarePollAttempts) {
		dprintf(RTL8814AU_DRIVER_NAME ": firmware checksum timed out\n");
		return B_TIMED_OUT;
	}

	dprintf(RTL8814AU_DRIVER_NAME ": firmware checksum OK, "
		"signaling MCU to start\n");

	// Stage 2: Tell MCU to begin firmware initialization
	uint32 ctrl = fRegisterIO->Read32(kRegMcuFwDl);
	ctrl |= kMcuFwDlRdy;
	ctrl &= ~kMcuWintiniRdy;
	fRegisterIO->Write32(kRegMcuFwDl, ctrl);

	// Stage 3: Wait for firmware to signal init complete
	attempts = 0;
	while (attempts < kFirmwarePollAttempts) {
		ctrl = fRegisterIO->Read32(kRegMcuFwDl);
		if (ctrl & kMcuWintiniRdy)
			break;
		snooze(kFirmwarePollDelay);
		attempts++;
	}
	if (attempts >= kFirmwarePollAttempts) {
		dprintf(RTL8814AU_DRIVER_NAME ": firmware init ready timed out "
			"(reg=0x%08" B_PRIx32 ")\n",
			fRegisterIO->Read32(kRegMcuFwDl));
		return B_TIMED_OUT;
	}

	dprintf(RTL8814AU_DRIVER_NAME ": firmware init complete\n");
	return B_OK;
}
