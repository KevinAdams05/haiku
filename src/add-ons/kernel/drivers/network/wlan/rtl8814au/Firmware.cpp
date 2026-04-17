/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Firmware.cpp — RTL8814AU firmware loading implementation.
 *
 * The loading sequence for the Lexra 3081 MCU:
 *   1. Read firmware binary from filesystem
 *   2. Validate header (signature 0x8814, size ≤ 96 KB)
 *   3. Enable firmware download mode
 *   4. Halt the MCU
 *   5. DMA the DMEM section to chip data memory
 *   6. DMA the IRAM section to chip instruction memory
 *   7. Resume the MCU
 *   8. Disable download mode
 *   9. Poll until firmware signals CPU_DL_READY
 *
 * The internal DMA engine (DDMA) is used for the actual data transfer.
 * This is fundamentally different from older Realtek chips (RTL8192/8812)
 * that use 8051-style page writes through the firmware download register.
 *
 * Reference: rtl8814a_hal_init.c:FirmwareDownload8814A() in the
 * ulli-kroll/rtl8814au driver.
 */

#include "Firmware.h"

#include <fcntl.h>
#include <new>
#include <string.h>
#include <unistd.h>

#include <ByteOrder.h>
#include <KernelExport.h>

#include "RegisterIO.h"


RTL8814AUFirmware::RTL8814AUFirmware(RTL8814AURegisterIO* registerIO)
	:
	fRegisterIO(registerIO),
	fData(NULL),
	fDataSize(0),
	fLoaded(false),
	fVersion(0),
	fDmemOffset(0),
	fDmemSize(0),
	fIramOffset(0),
	fIramSize(0)
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
		"DMEM %"  B_PRIu32 " bytes, IRAM %" B_PRIu32 " bytes\n",
		fVersion, fDmemSize, fIramSize);

	// Step 3: Enable download mode
	status = _EnableDownloadMode();
	if (status != B_OK)
		goto cleanup;

	// Step 4: Halt the MCU so we can safely write to its memory
	status = _HaltMCU();
	if (status != B_OK)
		goto cleanup;

	// Step 5: DMA the DMEM section to chip data memory
	if (fDmemSize > 0) {
		dprintf(RTL8814AU_DRIVER_NAME ": transferring DMEM "
			"(%" B_PRIu32 " bytes)\n", fDmemSize);
		status = _DmaTransferSection(fData + fDmemOffset, fDmemSize,
			kFirmwareDmemStartAddr);
		if (status != B_OK)
			goto cleanup;
	}

	// Step 6: DMA the IRAM section to chip instruction memory
	if (fIramSize > 0) {
		dprintf(RTL8814AU_DRIVER_NAME ": transferring IRAM "
			"(%" B_PRIu32 " bytes)\n", fIramSize);
		status = _DmaTransferSection(fData + fIramOffset, fIramSize,
			kFirmwareImemStartAddr);
		if (status != B_OK)
			goto cleanup;
	}

	// Step 7: Resume the MCU
	status = _ResumeMCU();
	if (status != B_OK)
		goto cleanup;

	// Step 8: Disable download mode
	status = _DisableDownloadMode();
	if (status != B_OK)
		goto cleanup;

	// Step 9: Poll until firmware signals ready
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
	dprintf(RTL8814AU_DRIVER_NAME ": firmware load failed at step: %s\n",
		strerror(status));
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


/*! Validate the firmware header at the start of fData. Parses the section
    offsets and sizes for DMEM and IRAM.
*/
status_t
RTL8814AUFirmware::_ValidateHeader()
{
	if (fDataSize < sizeof(RTL8814AUFirmwareHeader)) {
		dprintf(RTL8814AU_DRIVER_NAME ": firmware too small for header\n");
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

	// Parse section sizes from the header
	fIramSize = B_LENDIAN_TO_HOST_INT16(header->ramCodeSize);
	fDmemSize = B_LENDIAN_TO_HOST_INT32(header->dmemSize);

	// Sections follow immediately after the header
	uint32 headerSize = sizeof(RTL8814AUFirmwareHeader);
	fDmemOffset = headerSize;
	fIramOffset = headerSize + fDmemSize;

	// Bounds check
	if (fDmemOffset + fDmemSize > fDataSize) {
		dprintf(RTL8814AU_DRIVER_NAME ": DMEM section extends beyond "
			"file (offset %" B_PRIu32 ", size %" B_PRIu32
			", file %" B_PRIu32 ")\n",
			fDmemOffset, fDmemSize, fDataSize);
		return B_BAD_DATA;
	}

	if (fIramOffset + fIramSize > fDataSize) {
		dprintf(RTL8814AU_DRIVER_NAME ": IRAM section extends beyond "
			"file (offset %" B_PRIu32 ", size %" B_PRIu32
			", file %" B_PRIu32 ")\n",
			fIramOffset, fIramSize, fDataSize);
		return B_BAD_DATA;
	}

	if (fDmemSize > kFirmwareDmemMaxSize) {
		dprintf(RTL8814AU_DRIVER_NAME ": DMEM too large: %" B_PRIu32
			" (max %" B_PRIu32 ")\n", fDmemSize, kFirmwareDmemMaxSize);
		return B_BAD_DATA;
	}

	if (fIramSize > kFirmwareIramMaxSize) {
		dprintf(RTL8814AU_DRIVER_NAME ": IRAM too large: %" B_PRIu32
			" (max %" B_PRIu32 ")\n", fIramSize, kFirmwareIramMaxSize);
		return B_BAD_DATA;
	}

	return B_OK;
}


/*! Enable firmware download mode by setting the appropriate bits in
    the firmware control register.
*/
status_t
RTL8814AUFirmware::_EnableDownloadMode()
{
	uint32 ctrl = fRegisterIO->Read32(kRegMcuFwDl);
	ctrl |= kMcuFwDlEn | kMcuFwDlChksumRpt | kMcuFwDlDisableSim;
	return fRegisterIO->Write32(kRegMcuFwDl, ctrl);
}


/*! Disable firmware download mode after transfer is complete. */
status_t
RTL8814AUFirmware::_DisableDownloadMode()
{
	uint32 ctrl = fRegisterIO->Read32(kRegMcuFwDl);
	ctrl &= ~(kMcuFwDlEn | kMcuFwDlChksumRpt | kMcuFwDlDisableSim);
	return fRegisterIO->Write32(kRegMcuFwDl, ctrl);
}


/*! Halt the Lexra 3081 MCU by clearing the CPU enable bit. This must
    be done before writing to DMEM/IRAM to avoid corrupting the running
    firmware.
*/
status_t
RTL8814AUFirmware::_HaltMCU()
{
	// The CPU enable bit is bit 2 in the upper byte of kRegSysFuncEn
	// (i.e., bit 12 of the 16-bit register, which is kSysFuncEnCpuEn).
	uint16 funcEn = fRegisterIO->Read16(kRegSysFuncEn);
	funcEn &= ~kSysFuncEnCpuEn;
	return fRegisterIO->Write16(kRegSysFuncEn, funcEn);
}


/*! Resume the Lexra 3081 MCU by setting the CPU enable bit. The firmware
    begins executing from the start of IRAM.
*/
status_t
RTL8814AUFirmware::_ResumeMCU()
{
	uint16 funcEn = fRegisterIO->Read16(kRegSysFuncEn);
	funcEn |= kSysFuncEnCpuEn;
	return fRegisterIO->Write16(kRegSysFuncEn, funcEn);
}


/*! Transfer a firmware section to the chip using the internal DMA engine
    (DDMA). The data is written to a staging buffer in the chip's memory
    space, then an internal DMA operation copies it to the final destination.

    \param data         Pointer to section data (host memory)
    \param size         Section size in bytes
    \param destAddress  Destination address in chip address space
    \return B_OK on success, B_TIMED_OUT if DMA doesn't complete.
*/
status_t
RTL8814AUFirmware::_DmaTransferSection(const uint8* data, uint32 size,
	uint32 destAddress)
{
	// The internal DMA engine transfers data in chunks. Each chunk
	// involves:
	//   1. Write the data to the chip's staging buffer via register writes
	//   2. Configure DDMA source, destination, and size
	//   3. Trigger DMA and wait for completion

	static const uint32 kChunkSize = 4096;

	for (uint32 offset = 0; offset < size; offset += kChunkSize) {
		uint32 chunkLen = size - offset;
		if (chunkLen > kChunkSize)
			chunkLen = kChunkSize;

		// Write chunk data to staging area via sequential register writes.
		// The staging buffer at kFirmwareDmaBufferAddr is accessible
		// through the indirect memory interface.
		for (uint32 i = 0; i < chunkLen; i += 4) {
			uint32 word = 0;
			uint32 remaining = chunkLen - i;
			if (remaining >= 4) {
				memcpy(&word, data + offset + i, 4);
			} else {
				// Partial last word — pad with zeros
				memcpy(&word, data + offset + i, remaining);
			}
			status_t status = fRegisterIO->Write32(
				kFirmwareDmaBufferAddr + i, word);
			if (status != B_OK)
				return status;
		}

		// Configure the internal DMA: source = staging buffer,
		// destination = target memory, length = chunk size
		fRegisterIO->Write32(kRegDdmaCh0SA, kFirmwareDmaBufferAddr);
		fRegisterIO->Write32(kRegDdmaCh0DA, destAddress + offset);

		// Start DMA transfer with checksum enabled
		uint32 dmaCtrl = chunkLen | kDdmaChOwn | kDdmaChksmEn;
		fRegisterIO->Write32(kRegDdmaCh0Ctrl, dmaCtrl);

		// Poll until DMA completes (OWN bit clears)
		status_t status = fRegisterIO->PollFor32(kRegDdmaCh0Ctrl,
			kDdmaChOwn, 0, 1000, 10);
		if (status != B_OK) {
			dprintf(RTL8814AU_DRIVER_NAME ": DMA transfer timed out "
				"at offset %" B_PRIu32 "\n", offset);
			return B_TIMED_OUT;
		}
	}

	return B_OK;
}


/*! Poll the firmware control register until the MCU signals that it
    has finished initialization (CPU_DL_READY bit set).
    Polls up to 100 times with 50 ms between attempts (5 seconds total).
*/
status_t
RTL8814AUFirmware::_PollForReady()
{
	dprintf(RTL8814AU_DRIVER_NAME ": waiting for firmware ready signal\n");

	return fRegisterIO->PollFor32(kRegMcuFwDl,
		kMcuCpuDlReady, kMcuCpuDlReady,
		kFirmwarePollAttempts, kFirmwarePollDelay);
}
