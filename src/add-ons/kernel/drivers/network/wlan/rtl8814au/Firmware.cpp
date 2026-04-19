/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Firmware.cpp — RTL8814AU firmware loading implementation.
 *
 * The RTL8814AU contains a Lexra 3081 MIPS-derived MCU (not the 8051 found
 * in older Realtek chips). This requires a fundamentally different firmware
 * loading mechanism: IDDMA (Internal Data DMA).
 *
 * Loading sequence:
 *   1. Read firmware binary from filesystem
 *   2. Validate the 64-byte header (signature 0x8814, DMEM/IRAM sizes)
 *   3. Enable firmware download mode (set MCUFWDL_EN)
 *   4. Halt the MCU via REG_SYS_FUNC_EN bit 12 (kSysFuncEnCpuEn)
 *   5. Reset the DDMA engine
 *   6. Transfer DMEM section: write to TX buffer, IDDMA to OCP 0x00200000
 *   7. Transfer IRAM section: write to TX buffer, IDDMA to OCP 0x00000000
 *   8. Resume the MCU via REG_SYS_FUNC_EN bit 12
 *   9. Disable download mode
 *  10. Poll CPU_DL_READY (bit 15 of REG_MCUFWDL) — 5 second timeout
 *
 * The IDDMA mechanism:
 *   - Host writes firmware data to the TX packet buffer via page-based
 *     register writes to 0x1000 (same as 8051 method)
 *   - DDMA channel 0 (registers 0x1200–0x1208) copies from the TX buffer
 *     (OCP address 0x18780000) to the MCU's DMEM or IRAM region
 *   - The DDMA engine computes and verifies a running checksum
 *
 * Reference: rtl8814a_hal_init.c — FirmwareDownload8814A(),
 *            IDDMADownLoadFW_3081(), _3081Disable8814A(), _3081Enable8814A(),
 *            _FWFreeToGo8814A() in morrownr/8814au.
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

	// Step 2: Validate the 64-byte header and parse section sizes
	status = _ValidateHeader();
	if (status != B_OK) {
		delete[] fData;
		fData = NULL;
		return status;
	}

	dprintf(RTL8814AU_DRIVER_NAME ": firmware version %u, "
		"DMEM %" B_PRIu32 " bytes, IRAM %" B_PRIu32 " bytes\n",
		fVersion, fDmemSize, fIramSize);

	// Step 3: Enable download mode (sets MCUFWDL_EN)
	status = _EnableDownloadMode();
	if (status != B_OK)
		goto cleanup;

	// Step 4: Halt the Lexra 3081 MCU so we can write to its memory.
	// Uses REG_SYS_FUNC_EN bit 12, NOT the 8051 reset bit in REG_MCUFWDL.
	_HaltMCU();

	// Step 5: Reset the DDMA engine to clear any stale state
	_ResetDDMA();

	// Step 6: Transfer DMEM section — write to TX buffer, then IDDMA to
	// the MCU's data memory region at OCP address 0x00200000.
	dprintf(RTL8814AU_DRIVER_NAME ": transferring DMEM "
		"(%" B_PRIu32 " bytes)\n", fDmemSize);

	status = _TransferSection(fData + fDmemOffset, fDmemSize,
		kOcpBaseDMem, true);
	if (status != B_OK)
		goto cleanup_resume;

	// Step 7: Transfer IRAM section — write to TX buffer, then IDDMA to
	// the MCU's instruction memory at OCP address 0x00000000.
	dprintf(RTL8814AU_DRIVER_NAME ": transferring IRAM "
		"(%" B_PRIu32 " bytes)\n", fIramSize);

	status = _TransferSection(fData + fIramOffset, fIramSize,
		kOcpBaseIMem, true);
	if (status != B_OK)
		goto cleanup_resume;

	// Step 8: Resume the MCU — firmware will begin executing
	_ResumeMCU();

	// Step 9: Disable download mode
	status = _DisableDownloadMode();
	if (status != B_OK)
		goto cleanup;

	// Step 10: Poll until MCU signals CPU_DL_READY (bit 15)
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

cleanup_resume:
	// If we failed during transfer, resume the MCU before cleaning up
	// to avoid leaving it in a halted state
	_ResumeMCU();

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


/*! Validate the 64-byte firmware header and parse DMEM/IRAM section info.

    The firmware binary layout:
      [64-byte header] [DMEM section] [IRAM section]
    DMEM starts immediately after the header. IRAM starts at the end of
    the file minus iramSize. Each section includes 4 extra bytes for
    checksum purposes appended by the build tools.
*/
status_t
RTL8814AUFirmware::_ValidateHeader()
{
	if (fDataSize < kFwHeaderSize) {
		dprintf(RTL8814AU_DRIVER_NAME ": firmware too small for header "
			"(%" B_PRIu32 " < %" B_PRIu32 ")\n", fDataSize, kFwHeaderSize);
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

	// Extract section sizes from the header.
	// The reference driver reads these via GET_FIRMWARE_HDR_TOTAL_DMEM_SZ_3081
	// (offset 36) and GET_FIRMWARE_HDR_IRAM_SZ_3081 (offset 48).
	uint32 dmemSize = B_LENDIAN_TO_HOST_INT32(header->totalDmemSize);
	uint32 iramSize = B_LENDIAN_TO_HOST_INT32(header->iramSize);

	dprintf(RTL8814AU_DRIVER_NAME ": header: sig=0x%04x ver=%u "
		"dmem=%" B_PRIu32 " iram=%" B_PRIu32 "\n",
		signature, fVersion, dmemSize, iramSize);

	// Sanity check: sections must fit within the file
	if (dmemSize == 0 && iramSize == 0) {
		dprintf(RTL8814AU_DRIVER_NAME ": firmware has no payload sections\n");
		return B_BAD_DATA;
	}

	if (kFwHeaderSize + dmemSize + iramSize > fDataSize) {
		dprintf(RTL8814AU_DRIVER_NAME ": firmware sections exceed file size "
			"(header %" B_PRIu32 " + dmem %" B_PRIu32 " + iram %" B_PRIu32
			" > file %" B_PRIu32 ")\n",
			kFwHeaderSize, dmemSize, iramSize, fDataSize);
		return B_BAD_DATA;
	}

	// DMEM immediately follows the 64-byte header.
	// IRAM is at the end of the file (file size - iramSize).
	// This matches the reference driver's extraction:
	//   dmem_ptr = pbuffer + FWHeaderSize;
	//   iram_ptr = pbuffer + Len - iram_pkt_size;
	fDmemOffset = kFwHeaderSize;
	fDmemSize = dmemSize;
	fIramOffset = fDataSize - iramSize;
	fIramSize = iramSize;

	return B_OK;
}


/*! Enable firmware download mode: set the download enable bit and configure
    the control register for firmware transfer.

    Uses 8-bit register writes to avoid accidentally clobbering adjacent
    bytes in the MCU control register (some bits are write-1-to-clear).

    Matches _FWDownloadEnable_8814A(enable=true) in the reference driver.
*/
status_t
RTL8814AUFirmware::_EnableDownloadMode()
{
	// The reference driver's _FWDownloadEnable_8814A(enable=TRUE) operates
	// on byte 2 (0x0082) of REG_8051FW_CTRL_V1, not byte 1.
	// Step 1: Clear bit 4 and set bit 5 of byte 2 (bits 20/21 overall)
	uint8 byte2 = fRegisterIO->Read8(kRegMcuFwDl + 2);
	byte2 &= ~(uint8)0x10;		// Clear bit 4 of byte 2 (= bit 20 overall)
	byte2 |= (uint8)0x20;		// Set bit 5 of byte 2 (= bit 21 overall)
	status_t status = fRegisterIO->Write8(kRegMcuFwDl + 2, byte2);
	if (status != B_OK)
		return status;

	// Step 2: Set firmware download enable (bit 0 of byte 0)
	uint8 byte0 = fRegisterIO->Read8(kRegMcuFwDl);
	byte0 |= (uint8)kMcuFwDlEn;
	status = fRegisterIO->Write8(kRegMcuFwDl, byte0);
	if (status != B_OK)
		return status;

	// Step 3: Set ROM_DL_EN (bit 3 of byte 2 = bit 19 overall).
	// This enables the DDMA engine to access MCU memory regions.
	// Without this, DDMA register reads return garbage (0xeaeaeaea).
	byte2 = fRegisterIO->Read8(kRegMcuFwDl + 2);
	byte2 |= (uint8)0x08;		// Set bit 3 of byte 2 (= bit 19, ROM_DL_EN)
	return fRegisterIO->Write8(kRegMcuFwDl + 2, byte2);
}


/*! Disable firmware download mode after transfer is complete.

    Matches _FWDownloadEnable_8814A(enable=false) in the reference driver.
*/
status_t
RTL8814AUFirmware::_DisableDownloadMode()
{
	// Clear firmware download enable (bit 0 of byte 0)
	uint8 ctrl = fRegisterIO->Read8(kRegMcuFwDl);
	ctrl &= ~(uint8)kMcuFwDlEn;
	return fRegisterIO->Write8(kRegMcuFwDl, ctrl);
}


/*! Halt the Lexra 3081 MCU by clearing bit 12 (FEN_CPUEN) of
    REG_SYS_FUNC_EN. Uses an 8-bit write to byte 1 of the 16-bit register
    to avoid disturbing other enable bits.

    Matches _3081Disable8814A() in the reference driver.
*/
void
RTL8814AUFirmware::_HaltMCU()
{
	// Bit 10 of REG_SYS_FUNC_EN = BIT2 of byte at REG_SYS_FUNC_EN + 1.
	// Matches _3081Disable8814A(): u1bTmp & (~BIT2)
	uint8 byte1 = fRegisterIO->Read8(kRegSysFuncEn + 1);
	byte1 &= ~(uint8)0x04;		// Clear BIT2 (= bit 10 of 16-bit reg)
	fRegisterIO->Write8(kRegSysFuncEn + 1, byte1);

	dprintf(RTL8814AU_DRIVER_NAME ": MCU halted (SYS_FUNC_EN bit 10 cleared)\n");
}


/*! Resume the Lexra 3081 MCU by setting bit 12 (FEN_CPUEN) of
    REG_SYS_FUNC_EN. The MCU begins executing the firmware loaded into
    DMEM and IRAM.

    Matches _3081Enable8814A() in the reference driver.
*/
void
RTL8814AUFirmware::_ResumeMCU()
{
	// Bit 10 of REG_SYS_FUNC_EN = BIT2 of byte at REG_SYS_FUNC_EN + 1.
	// Matches _3081Enable8814A(): u1bTmp | BIT2
	uint8 byte1 = fRegisterIO->Read8(kRegSysFuncEn + 1);
	byte1 |= (uint8)0x04;		// Set BIT2 (= bit 10 of 16-bit reg)
	fRegisterIO->Write8(kRegSysFuncEn + 1, byte1);

	dprintf(RTL8814AU_DRIVER_NAME ": MCU resumed (SYS_FUNC_EN bit 10 set)\n");
}


/*! Reset the DDMA engine by toggling bit 16 of REG_CPU_DMEM_CON_8814A.
    This clears any stale DMA state from a previous firmware load attempt.
*/
void
RTL8814AUFirmware::_ResetDDMA()
{
	uint32 val = fRegisterIO->Read32(kRegCpuDmemCon);
	fRegisterIO->Write32(kRegCpuDmemCon, val | (1 << 16));
	snooze(100);
	fRegisterIO->Write32(kRegCpuDmemCon, val & ~(1 << 16));

	dprintf(RTL8814AU_DRIVER_NAME ": DDMA engine reset\n");
}


// Maximum bytes per USB vendor control transfer for firmware data.
// Matches MAX_REG_BOLCK_SIZE (254) used by _BlockWrite_8814A() in the
// reference driver's USB path.
static const uint32 kFwUsbBlockSize = 254;


/*! Transfer a firmware section (DMEM or IRAM) to the chip.

    The section data is first written to the TX packet buffer via page-based
    register writes to 0x1000, then the DDMA engine copies it from the TX
    buffer to the target OCP address (DMEM or IRAM memory region).

    For large sections, the data is split into 4 KB chunks. Each chunk is
    written to page 0 of the TX buffer, then IDDMA'd to the destination
    at an increasing offset.

    \param data           Pointer to the section data
    \param size           Section size in bytes
    \param ocpDestAddr    OCP base address (kOcpBaseDMem or kOcpBaseIMem)
    \param resetChecksum  True to reset the DDMA checksum accumulator
*/
status_t
RTL8814AUFirmware::_TransferSection(const uint8* data, uint32 size,
	uint32 ocpDestAddr, bool resetChecksum)
{
	if (size == 0)
		return B_OK;

	uint32 offset = 0;

	while (offset < size) {
		// Determine chunk size — up to one page (4 KB) at a time
		uint32 chunkSize = size - offset;
		if (chunkSize > kFwPageSize)
			chunkSize = kFwPageSize;

		// Write this chunk to page 0 of the TX packet buffer (0x1000)
		status_t status = _PageWriteToTxBuffer(data + offset, chunkSize);
		if (status != B_OK) {
			dprintf(RTL8814AU_DRIVER_NAME ": TX buffer write failed at "
				"offset %" B_PRIu32 "\n", offset);
			return status;
		}

		// IDDMA from TX buffer to the MCU memory destination.
		// Source = TX buffer base (OCP 0x18780000)
		// Dest = target OCP address + current offset
		bool isFirstChunk = (offset == 0) && resetChecksum;
		status = _IDDMATransfer(kOcpBaseTxBuf, ocpDestAddr + offset,
			chunkSize, isFirstChunk);
		if (status != B_OK) {
			dprintf(RTL8814AU_DRIVER_NAME ": IDDMA transfer failed at "
				"offset %" B_PRIu32 "\n", offset);
			return status;
		}

		offset += chunkSize;
	}

	// Verify section download status via REG_CPU_DMEM_CON.
	// After DMEM transfer, DMEM_DL_RDY (bit 5) should be set.
	// After IRAM transfer, IMEM_DL_RDY (bit 3) should be set.
	// These bits are set by the last IDDMA transfer's checksum validation.
	uint32 dmemCon = fRegisterIO->Read32(kRegCpuDmemCon);
	if (ocpDestAddr == kOcpBaseDMem) {
		if (!(dmemCon & kDmemDlRdy)) {
			dprintf(RTL8814AU_DRIVER_NAME ": DMEM download ready not set "
				"(REG_CPU_DMEM_CON=0x%08" B_PRIx32 ")\n", dmemCon);
		}
	} else if (ocpDestAddr == kOcpBaseIMem) {
		if (!(dmemCon & kImemDlRdy)) {
			dprintf(RTL8814AU_DRIVER_NAME ": IRAM download ready not set "
				"(REG_CPU_DMEM_CON=0x%08" B_PRIx32 ")\n", dmemCon);
		}
	}

	return B_OK;
}


/*! Write firmware data to page 0 of the TX packet buffer via register
    writes to 0x1000. The data is written in 254-byte USB control transfer
    blocks, with 4-byte and 1-byte fallback for the remainder.

    This puts the data into the TX buffer where the DDMA engine can access
    it at OCP address kOcpBaseTxBuf (0x18780000).
*/
status_t
RTL8814AUFirmware::_PageWriteToTxBuffer(const uint8* data, uint32 size)
{
	// Always write to page 0 of the TX buffer
	return _WritePage(0, data, size);
}


/*! Write a single page of firmware data. Sets the page number in
    REG_MCUFWDL, then writes data in 254-byte blocks to kFwStartAddress
    (0x1000) using USB vendor control transfers.

    Matches _PageWrite_8814A() + _BlockWrite_8814A() in the reference driver.
*/
status_t
RTL8814AUFirmware::_WritePage(uint32 page, const uint8* data, uint32 size)
{
	// Select page number in bits [2:0] of byte 2 at REG_MCUFWDL (= bits
	// [18:16] of the 32-bit register). Use 8-bit write to byte 2 only,
	// matching the reference driver's _PageWrite_8814A() which avoids
	// disturbing byte 0's write-1-to-clear bits.
	uint8 byte2 = fRegisterIO->Read8(kRegMcuFwDl + 2);
	byte2 = (byte2 & 0xF8) | (uint8)(page & 0x07);
	status_t status = fRegisterIO->Write8(kRegMcuFwDl + 2, byte2);
	if (status != B_OK)
		return status;

	// Phase 1: Write 254-byte blocks via WriteN (bulk USB control transfers).
	uint32 offset = 0;
	uint32 blockCount = size / kFwUsbBlockSize;

	for (uint32 i = 0; i < blockCount; i++) {
		status = fRegisterIO->WriteN(
			kFwStartAddress + offset,
			data + offset,
			(uint16)kFwUsbBlockSize);
		if (status != B_OK) {
			dprintf(RTL8814AU_DRIVER_NAME ": page %" B_PRIu32 " block write "
				"failed at offset %" B_PRIu32 "\n", page, offset);
			return status;
		}
		offset += kFwUsbBlockSize;
	}

	// Phase 2: Write remaining 4-byte words
	uint32 remainSize = size - offset;
	uint32 wordCount = remainSize / 4;

	for (uint32 i = 0; i < wordCount; i++) {
		uint32 word;
		memcpy(&word, data + offset, 4);
		status = fRegisterIO->Write32(kFwStartAddress + offset, word);
		if (status != B_OK) {
			dprintf(RTL8814AU_DRIVER_NAME ": page %" B_PRIu32 " word write "
				"failed at offset %" B_PRIu32 "\n", page, offset);
			return status;
		}
		offset += 4;
	}

	// Phase 3: Write remaining 1-byte tail
	remainSize = size - offset;
	for (uint32 i = 0; i < remainSize; i++) {
		status = fRegisterIO->Write8(kFwStartAddress + offset, data[offset]);
		if (status != B_OK) {
			dprintf(RTL8814AU_DRIVER_NAME ": page %" B_PRIu32 " byte write "
				"failed at offset %" B_PRIu32 "\n", page, offset);
			return status;
		}
		offset++;
	}

	return B_OK;
}


/*! Trigger an IDDMA transfer from the TX packet buffer to MCU memory.

    Programs DDMA channel 0 registers with source/destination addresses and
    transfer length, then polls until the hardware clears the ownership bit.
    The DDMA engine also computes a running checksum over the transferred data.

    \param srcAddr        OCP source address (typically kOcpBaseTxBuf)
    \param destAddr       OCP destination address (DMEM or IRAM base + offset)
    \param length         Number of bytes to transfer
    \param resetChecksum  True to reset checksum accumulator (first chunk)

    Matches IDDMADownLoadFW_3081() in the reference driver.
*/
status_t
RTL8814AUFirmware::_IDDMATransfer(uint32 srcAddr, uint32 destAddr,
	uint32 length, bool resetChecksum)
{
	// Program source and destination addresses
	fRegisterIO->Write32(kRegDDMACh0SA, srcAddr);
	fRegisterIO->Write32(kRegDDMACh0DA, destAddr);

	// Build control word: OWN + CHKSUM_EN + length
	// For the first chunk, reset the checksum accumulator.
	// For subsequent chunks, set CHKSUM_CONT to continue accumulating.
	uint32 ctrl = kDDMAChOwn | kDDMAChksumEn | (length & kDDMALenMask);
	if (resetChecksum)
		ctrl |= kDDMAChksumRst;
	else
		ctrl |= kDDMAChksumCont;

	fRegisterIO->Write32(kRegDDMACh0Ctrl, ctrl);

	// Poll until hardware clears the OWN bit (transfer complete)
	uint32 attempts = 0;
	while (attempts < kDDMAPollAttempts) {
		uint32 status = fRegisterIO->Read32(kRegDDMACh0Ctrl);
		if (!(status & kDDMAChOwn)) {
			// Transfer done — check for checksum failure
			if (status & kDDMAChksumFail) {
				dprintf(RTL8814AU_DRIVER_NAME ": IDDMA checksum failed "
					"(ctrl=0x%08" B_PRIx32 ")\n", status);
				return B_IO_ERROR;
			}
			return B_OK;
		}
		snooze(kDDMAPollDelay);
		attempts++;
	}

	dprintf(RTL8814AU_DRIVER_NAME ": IDDMA transfer timed out "
		"(ctrl=0x%08" B_PRIx32 ")\n",
		fRegisterIO->Read32(kRegDDMACh0Ctrl));
	return B_TIMED_OUT;
}


/*! Poll for CPU_DL_READY (bit 15) in REG_MCUFWDL / REG_8051FW_CTRL_8814A.
    The Lexra 3081 MCU sets this bit when firmware initialization is complete.

    Timeout: 100 iterations x 50 ms = 5 seconds.

    Matches _FWFreeToGo8814A() in the reference driver.
*/
status_t
RTL8814AUFirmware::_PollForReady()
{
	dprintf(RTL8814AU_DRIVER_NAME ": waiting for CPU_DL_READY "
		"(REG_MCUFWDL=0x%08" B_PRIx32 ")\n",
		fRegisterIO->Read32(kRegMcuFwDl));

	uint32 attempts = 0;
	while (attempts < kFirmwareReadyAttempts) {
		snooze(kFirmwareReadyDelay);
		uint32 ctrl = fRegisterIO->Read32(kRegMcuFwDl);
		if (ctrl & kMcuCpuDlReady) {
			dprintf(RTL8814AU_DRIVER_NAME ": firmware init complete "
				"(CPU_DL_READY set after %" B_PRIu32 " polls, "
				"REG_MCUFWDL=0x%08" B_PRIx32 ")\n",
				attempts + 1, ctrl);
			return B_OK;
		}
		attempts++;
	}

	uint32 finalCtrl = fRegisterIO->Read32(kRegMcuFwDl);
	dprintf(RTL8814AU_DRIVER_NAME ": firmware init ready timed out after "
		"%" B_PRIu32 " polls (REG_MCUFWDL=0x%08" B_PRIx32 ")\n",
		attempts, finalCtrl);

	// Log additional diagnostic registers
	uint32 dmemCon = fRegisterIO->Read32(kRegCpuDmemCon);
	uint16 sysFuncEn = fRegisterIO->Read16(kRegSysFuncEn);
	dprintf(RTL8814AU_DRIVER_NAME ": REG_CPU_DMEM_CON=0x%08" B_PRIx32
		" REG_SYS_FUNC_EN=0x%04x\n", dmemCon, sysFuncEn);

	return B_TIMED_OUT;
}
