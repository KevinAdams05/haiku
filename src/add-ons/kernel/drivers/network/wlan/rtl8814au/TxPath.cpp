/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * TxPath.cpp — TX data path implementation for RTL8814AU.
 *
 * Outbound frame flow:
 *   1. Network stack or WiFi management calls Transmit() with raw frame data
 *   2. Transmit() acquires a free transfer buffer from the pre-allocated pool
 *   3. _BuildDescriptor() writes the 40-byte TX descriptor at the buffer start
 *   4. Frame data is copied immediately after the descriptor
 *   5. The combined buffer is submitted via queue_bulk() on the appropriate
 *      bulk OUT endpoint (selected by WMM priority)
 *   6. _TxCallback() fires when the USB transfer completes, releasing the
 *      buffer back to the pool and updating statistics
 *
 * Buffer management:
 *   Each bulk OUT pipe has kTxTransfersPerQueue pre-allocated transfer buffers
 *   (currently 4 each, 12 total). This bounds memory usage while allowing
 *   enough USB pipelining for good throughput. If all buffers for a pipe are
 *   in-flight, Transmit() blocks briefly on the completion semaphore.
 *
 * Reference: rtl8814a_xmit.c in ulli-kroll/rtl8814au.
 */

#include "TxPath.h"

#include <new>
#include <string.h>

#include <ByteOrder.h>
#include <KernelExport.h>
#include <OS.h>
#include <util/AutoLock.h>

#include "RegisterIO.h"


// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------


RTL8814AUTxPath::RTL8814AUTxPath(RTL8814AURegisterIO* registerIO,
	usb_module_info* usbModule, usb_device usbDevice,
	usb_pipe bulkOut[kBulkOutEndpointCount])
	:
	fRegisterIO(registerIO),
	fUSBModule(usbModule),
	fUSBDevice(usbDevice),
	fSequenceNumber(0),
	fFramesSent(0),
	fFramesFailed(0),
	fInitStatus(B_NO_INIT)
{
	memcpy(fBulkOut, bulkOut, sizeof(fBulkOut));
	mutex_init(&fLock, "rtl8814au:tx");

	// Allocate transfer buffers and semaphores for each slot
	for (uint32 i = 0; i < kTxTotalTransfers; i++) {
		fTransfers[i].buffer = new(std::nothrow) uint8[kUsbTxBufferSize];
		if (fTransfers[i].buffer == NULL) {
			fInitStatus = B_NO_MEMORY;
			return;
		}
		fTransfers[i].bufferSize = kUsbTxBufferSize;
		fTransfers[i].inUse = false;

		fTransfers[i].completionSem = create_sem(0, "rtl8814au:tx_done");
		if (fTransfers[i].completionSem < 0) {
			fInitStatus = fTransfers[i].completionSem;
			return;
		}
	}

	fInitStatus = B_OK;
	dprintf(RTL8814AU_DRIVER_NAME ": TX path initialized "
		"(%" B_PRIu32 " transfer buffers)\n", kTxTotalTransfers);
}


RTL8814AUTxPath::~RTL8814AUTxPath()
{
	CancelAll();

	for (uint32 i = 0; i < kTxTotalTransfers; i++) {
		delete[] fTransfers[i].buffer;
		fTransfers[i].buffer = NULL;
		if (fTransfers[i].completionSem >= 0)
			delete_sem(fTransfers[i].completionSem);
	}

	mutex_destroy(&fLock);
}


// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------


/*! Transmit a single frame. Builds the TX descriptor, copies the frame
    into a transfer buffer, and submits it on the appropriate bulk OUT pipe.

    If all transfer buffers for the target pipe are in-flight, this will
    briefly block until one completes.

    \return B_OK on successful submission (NOT delivery confirmation).
*/
status_t
RTL8814AUTxPath::Transmit(const uint8* frameData, uint32 frameLength,
	TxQueueSelect queueSelect, uint8 dataRate, uint8 macID,
	SecurityType secType, bool isBroadcast)
{
	if (fInitStatus != B_OK)
		return fInitStatus;

	// Validate frame size — descriptor + frame must fit in the USB buffer
	uint32 totalLength = kTxDescSize + frameLength;
	if (totalLength > kUsbTxBufferSize) {
		dprintf(RTL8814AU_DRIVER_NAME ": TX frame too large: %" B_PRIu32
			" bytes (max %" B_PRIu32 ")\n",
			totalLength, kUsbTxBufferSize);
		return B_BAD_VALUE;
	}

	uint32 pipeIndex = _QueueToPipeIndex(queueSelect);

	// Find a free transfer buffer for this pipe
	MutexLocker locker(fLock);

	int32 transferIndex = _FindFreeTransfer(pipeIndex);
	if (transferIndex < 0) {
		// All buffers in use — release the lock and wait for one to complete.
		// We wait on the first transfer slot for this pipe.
		uint32 firstSlot = pipeIndex * kTxTransfersPerQueue;
		locker.Unlock();

		dprintf(RTL8814AU_DRIVER_NAME ": TX queue %u full, waiting\n",
			pipeIndex);
		status_t status = acquire_sem_etc(fTransfers[firstSlot].completionSem,
			1, B_RELATIVE_TIMEOUT, 1000000);	// 1 second timeout
		if (status != B_OK) {
			dprintf(RTL8814AU_DRIVER_NAME ": TX wait timed out\n");
			fFramesFailed++;
			return B_TIMED_OUT;
		}

		locker.Lock();
		transferIndex = _FindFreeTransfer(pipeIndex);
		if (transferIndex < 0) {
			fFramesFailed++;
			return B_BUSY;
		}
	}

	TxTransfer* transfer = &fTransfers[transferIndex];
	transfer->inUse = true;

	// Build the TX descriptor at the start of the buffer
	_BuildDescriptor(transfer->buffer, frameLength, queueSelect,
		dataRate, macID, secType, isBroadcast);

	// Copy the frame data after the descriptor
	memcpy(transfer->buffer + kTxDescSize, frameData, frameLength);

	locker.Unlock();

	// Submit the USB bulk OUT transfer
	status_t status = fUSBModule->queue_bulk(fBulkOut[pipeIndex],
		transfer->buffer, totalLength, _TxCallback, transfer);
	if (status != B_OK) {
		dprintf(RTL8814AU_DRIVER_NAME ": queue_bulk failed for TX: %s\n",
			strerror(status));
		MutexLocker relock(fLock);
		transfer->inUse = false;
		fFramesFailed++;
		return status;
	}

	return B_OK;
}


/*! Send a single firmware download chunk synchronously on the beacon
    bulk OUT endpoint.

    Builds a minimal beacon-queue TX descriptor (QSEL = kQslBeacon = 0x10,
    FS | LS | OWN, Offset = 40, BMC set) and submits it via queue_bulk()
    on pipe 0, then blocks on the completion semaphore until the USB
    transfer finishes.

    Pipe 0 is the first enumerated bulk OUT endpoint — in the reference
    driver's 3EP non-WMM mapping (_ThreeOutPipeMapping), BCN, MGT, HIGH,
    and TXCMD queues all route to RtOutPipe[0] (QUEUE_HIGH priority,
    typically EP2).  Data queues use pipes 1–2.

    The chip writes the packet into its TX packet buffer at a location
    determined by the beacon-queue page boundary, then sets BcnValid
    (bit 7 of REG_FIFOPAGE_CTRL_2+1) to acknowledge.  The firmware
    loader waits on that bit before triggering IDDMA.

    Reference: SetDownLoadFwRsvdPagePkt_8814A() and _ThreeOutPipeMapping()
    in zebulon2/rtl8814au (hal/hal_com.c).
*/
status_t
RTL8814AUTxPath::SendFirmwareChunk(const uint8* data, uint32 length)
{
	if (fInitStatus != B_OK)
		return fInitStatus;

	if (length == 0 || length > kFwPageSize)
		return B_BAD_VALUE;

	uint32 totalLength = kTxDescSize + length;
	if (totalLength > kUsbTxBufferSize)
		return B_BAD_VALUE;

	// Use pipe 0 (first enumerated bulk OUT) — matches the reference
	// 3EP mapping where BCN and MGT go to RtOutPipe[0].  Slot 0 of
	// that pipe's transfer pool is used; firmware load is single-
	// threaded so no contention is expected.
	const uint32 pipeIndex = 0;
	const uint32 slotIndex = pipeIndex * kTxTransfersPerQueue;

	MutexLocker locker(fLock);

	TxTransfer* transfer = &fTransfers[slotIndex];
	if (transfer->inUse) {
		dprintf(RTL8814AU_DRIVER_NAME ": firmware TX slot busy\n");
		return B_BUSY;
	}
	transfer->inUse = true;

	// Build minimal beacon-queue TX descriptor.
	uint8* desc = transfer->buffer;
	memset(desc, 0, kTxDescSize);

	// DWORD 0: packet length, descriptor offset = 40, BMC, FS, LS, OWN
	uint32 dword0 = (length & kTxDescPktLen_Mask)
		| ((kTxDescSize << kTxDescOffset_Shift) & kTxDescOffset_Mask)
		| kTxDescBMC | kTxDescFS | kTxDescLS | kTxDescOWN;

	// DWORD 1: MACID=0, QSEL = 0x10 (QSLT_BEACON)
	uint32 dword1 = ((uint32)kQslBeacon << kTxDescQueueSel_Shift)
		& kTxDescQueueSel_Mask;

	uint32* desc32 = reinterpret_cast<uint32*>(desc);
	desc32[0] = B_HOST_TO_LENDIAN_INT32(dword0);
	desc32[1] = B_HOST_TO_LENDIAN_INT32(dword1);
	// DWORDs 2-9 remain zero — no aggregation, sequence, rate, or power

	memcpy(desc + kTxDescSize, data, length);

	// Clear the completion semaphore of any stale signals from prior TX.
	sem_info info;
	if (get_sem_info(transfer->completionSem, &info) == B_OK
		&& info.count > 0) {
		acquire_sem_etc(transfer->completionSem, info.count,
			B_RELATIVE_TIMEOUT, 0);
	}

	locker.Unlock();

	status_t status = fUSBModule->queue_bulk(fBulkOut[pipeIndex],
		transfer->buffer, totalLength, _TxCallback, transfer);
	if (status != B_OK) {
		dprintf(RTL8814AU_DRIVER_NAME ": firmware queue_bulk failed: %s\n",
			strerror(status));
		MutexLocker relock(fLock);
		transfer->inUse = false;
		return status;
	}

	// Wait for the USB transfer to complete (1 second is generous for a
	// 4 KB bulk transfer at USB 2.0).  The callback clears inUse and
	// releases the semaphore.
	status = acquire_sem_etc(transfer->completionSem, 1,
		B_RELATIVE_TIMEOUT, 1000000);
	if (status != B_OK) {
		dprintf(RTL8814AU_DRIVER_NAME ": firmware TX completion wait "
			"timed out (%" B_PRIu32 " bytes)\n", length);
		fUSBModule->cancel_queued_transfers(fBulkOut[pipeIndex]);
		return B_TIMED_OUT;
	}

	return B_OK;
}


/*! Clear ENDPOINT_HALT on the beacon-queue bulk OUT pipe.

    If the chip's EP2 is in a STALL state (possibly a residue from a
    previous session or triggered by a malformed control transfer
    during probe), no bulk OUT transfer will ever be drained.  The
    standard USB recovery is a CLEAR_FEATURE(ENDPOINT_HALT) request,
    which resets the endpoint's halt condition and data toggle.
*/
status_t
RTL8814AUTxPath::ClearBeaconPipeHalt()
{
	status_t status = fUSBModule->clear_feature(fBulkOut[0],
		USB_FEATURE_ENDPOINT_HALT);
	dprintf(RTL8814AU_DRIVER_NAME ": clear_feature(ENDPOINT_HALT) on "
		"bulk OUT pipe 0 returned %s\n", strerror(status));
	return status;
}


/*! Cancel all pending TX transfers. Called during device shutdown or
    removal. After this returns, no callbacks will fire.
*/
void
RTL8814AUTxPath::CancelAll()
{
	MutexLocker locker(fLock);

	// Cancel all in-flight USB transfers on each bulk OUT pipe
	for (uint32 pipe = 0; pipe < kBulkOutEndpointCount; pipe++) {
		if (fBulkOut[pipe] != 0)
			fUSBModule->cancel_queued_transfers(fBulkOut[pipe]);
	}

	// Mark all transfer slots as free
	for (uint32 i = 0; i < kTxTotalTransfers; i++)
		fTransfers[i].inUse = false;
}


// ---------------------------------------------------------------------------
// Private implementation
// ---------------------------------------------------------------------------


/*! Build the 40-byte TX descriptor that the RTL8814AU hardware expects
    prepended to every transmitted frame.

    The descriptor is written in little-endian format into the provided
    buffer, which must be at least kTxDescSize (40) bytes.

    Descriptor layout (10 DWORDs):
      DWORD 0: packet length, descriptor offset, BMC, first/last segment, OWN
      DWORD 1: MACID, queue select, security type, packet offset
      DWORD 2: aggregation flags
      DWORD 3: sequence number
      DWORD 4: data rate, bandwidth, RTS/CTS, short preamble
      DWORD 5: TX power offset
      DWORD 6–9: reserved / future use
*/
void
RTL8814AUTxPath::_BuildDescriptor(uint8* descriptor, uint32 frameLength,
	TxQueueSelect queueSelect, uint8 dataRate, uint8 macID,
	SecurityType secType, bool isBroadcast)
{
	// Zero the entire descriptor first — unused fields must be 0
	memset(descriptor, 0, kTxDescSize);

	// Assign and advance the sequence number
	uint16 seqNum = fSequenceNumber++;
	if (fSequenceNumber > 0x0FFF)
		fSequenceNumber = 0;

	// DWORD 0: packet length, descriptor offset (40 bytes = 0x28),
	// broadcast/multicast flag, first segment, last segment, OWN
	uint32 dword0 = (frameLength & kTxDescPktLen_Mask)
		| ((kTxDescSize << kTxDescOffset_Shift) & kTxDescOffset_Mask)
		| kTxDescFS | kTxDescLS | kTxDescOWN;
	if (isBroadcast)
		dword0 |= kTxDescBMC;

	// DWORD 1: MACID, queue select, security type
	uint32 dword1 = (macID & kTxDescMACID_Mask)
		| (((uint32)queueSelect << kTxDescQueueSel_Shift)
			& kTxDescQueueSel_Mask)
		| (((uint32)secType << kTxDescSecType_Shift)
			& kTxDescSecType_Mask);

	// DWORD 2: aggregation enable for data frames (not management)
	uint32 dword2 = 0;
	if (queueSelect != kTxQueueMGT && queueSelect != kTxQueueCMD)
		dword2 |= kTxDescAGGEn;

	// DWORD 3: sequence number
	uint32 dword3 = ((uint32)seqNum << kTxDescSeq_Shift)
		& kTxDescSeq_Mask;

	// DWORD 4: data rate, short preamble for CCK rates
	uint32 dword4 = (dataRate & kTxDescDataRate_Mask);
	if (dataRate <= kRateCCK11)
		dword4 |= kTxDescDataShort;

	// Write all DWORDs in little-endian format
	uint32* desc32 = reinterpret_cast<uint32*>(descriptor);
	desc32[0] = B_HOST_TO_LENDIAN_INT32(dword0);
	desc32[1] = B_HOST_TO_LENDIAN_INT32(dword1);
	desc32[2] = B_HOST_TO_LENDIAN_INT32(dword2);
	desc32[3] = B_HOST_TO_LENDIAN_INT32(dword3);
	desc32[4] = B_HOST_TO_LENDIAN_INT32(dword4);
	// DWORDs 5–9 remain zero (TX power offset = 0, no SW define)
}


/*! Map a TX queue selection to the bulk OUT pipe index.

    The TxQueueSelect enum values are defined to equal the pipe index directly:
      VO/VI/HIGH = 0 → pipe 0 (high priority)
      BE/BK      = 1 → pipe 1 (normal priority)
      MGT/CMD/BCN = 2 → pipe 2 (management)
*/
uint32
RTL8814AUTxPath::_QueueToPipeIndex(TxQueueSelect queue)
{
	uint32 index = (uint32)queue;
	if (index >= kBulkOutEndpointCount)
		return 1;	// Default to best-effort
	return index;
}


/*! Search for a free transfer slot among the slots allocated to the
    given pipe index. Each pipe has kTxTransfersPerQueue dedicated slots.

    \param pipeIndex  Bulk OUT pipe index (0–2)
    \return Transfer slot index (0..kTxTotalTransfers-1), or -1 if none free.
*/
int32
RTL8814AUTxPath::_FindFreeTransfer(uint32 pipeIndex)
{
	uint32 base = pipeIndex * kTxTransfersPerQueue;
	for (uint32 i = 0; i < kTxTransfersPerQueue; i++) {
		if (!fTransfers[base + i].inUse)
			return (int32)(base + i);
	}
	return -1;
}


/*! USB bulk OUT completion callback. Called by the USB bus manager when
    a TX transfer finishes (successfully or with an error).

    \param cookie       Pointer to the TxTransfer that completed
    \param status       B_OK on success, error code on failure
    \param data         Transfer buffer pointer (same as TxTransfer::buffer)
    \param actualLength Number of bytes actually transferred
*/
void
RTL8814AUTxPath::_TxCallback(void* cookie, status_t status, void* data,
	size_t actualLength)
{
	TxTransfer* transfer = static_cast<TxTransfer*>(cookie);
	if (transfer == NULL)
		return;

	if (status != B_OK) {
		dprintf(RTL8814AU_DRIVER_NAME ": TX callback error: %s\n",
			strerror(status));
	}

	// Release the transfer slot so it can be reused
	transfer->inUse = false;

	// Signal any thread waiting for a free buffer
	release_sem_etc(transfer->completionSem, 1, B_DO_NOT_RESCHEDULE);
}
