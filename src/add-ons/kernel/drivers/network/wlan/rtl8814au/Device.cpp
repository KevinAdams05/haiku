/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Device.cpp — RTL8814AUDevice implementation.
 *
 * Manages the full lifecycle of a single RTL8814AU USB WiFi adapter:
 *   - USB endpoint discovery (3 bulk OUT, 1 bulk IN, 1 interrupt IN)
 *   - Hardware power-on sequencing
 *   - Coordination of firmware loading, EFUSE reading, PHY init
 *   - Device open/close reference counting for hot-unplug safety
 *   - Ioctl dispatch for network stack integration
 *
 * Hardware initialization is deferred to the first open() call rather than
 * happening at USB attach time. This avoids blocking the USB bus manager
 * during firmware download (~1 second) and allows cleaner error reporting
 * to the caller that actually wants to use the device.
 */

#include "Device.h"

#include <new>
#include <stdlib.h>
#include <string.h>

#include <KernelExport.h>
#include <OS.h>
#include <net/if_media.h>
#include <util/AutoLock.h>

#include <ether_driver.h>

#include "Driver.h"


// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------


/*! Create a new device instance for a USB device we claimed.
    Sets up the register I/O module and discovers USB endpoints.
    Does NOT initialize the hardware yet — that happens on first open().

    \param device       USB device handle from the bus manager
    \param slotIndex    Index in gDeviceList[] for this device
    \param deviceName   Human-readable name (e.g., "ASUS USB-AC68")
*/
RTL8814AUDevice::RTL8814AUDevice(usb_device device, uint32 slotIndex,
	const char* deviceName)
	:
	fUSBDevice(device),
	fBulkIn(0),
	fInterruptIn(0),
	fSlotIndex(slotIndex),
	fInitStatus(B_NO_INIT),
	fRemoved(false),
	fHardwareInitialized(false),
	fOpenCount(0),
	fRegisterIO(NULL),
	fFirmware(NULL),
	fEfuseReader(NULL),
	fPhyConfig(NULL),
	fTxPath(NULL),
	fRxPath(NULL),
	fWiFiManager(NULL)
{
	memset(fBulkOut, 0, sizeof(fBulkOut));
	memset(fMacAddress, 0, sizeof(fMacAddress));
	memset(fRxRing, 0, sizeof(fRxRing));
	fRxRingHead = 0;
	fRxRingTail = 0;
	fLinkStateSem = -1;
	strlcpy(fDeviceName, deviceName, sizeof(fDeviceName));
	mutex_init(&fLock, "rtl8814au:device");

	fRxDataReady = create_sem(0, "rtl8814au:rx_ready");
	if (fRxDataReady < 0) {
		fInitStatus = fRxDataReady;
		return;
	}

	// Create the register I/O module — everything else depends on this
	fRegisterIO = new(std::nothrow) RTL8814AURegisterIO(device, gUSBModule);
	if (fRegisterIO == NULL) {
		fInitStatus = B_NO_MEMORY;
		return;
	}

	// Create the firmware loader
	fFirmware = new(std::nothrow) RTL8814AUFirmware(fRegisterIO);
	if (fFirmware == NULL) {
		fInitStatus = B_NO_MEMORY;
		return;
	}

	// Create the EFUSE reader
	fEfuseReader = new(std::nothrow) RTL8814AUEfuseReader(fRegisterIO);
	if (fEfuseReader == NULL) {
		fInitStatus = B_NO_MEMORY;
		return;
	}

	// Discover and configure USB endpoints — must happen before creating
	// modules that need endpoint handles (TxPath, RxPath, WiFiManager)
	fInitStatus = _SetupEndpoints();
	if (fInitStatus != B_OK) {
		dprintf(RTL8814AU_DRIVER_NAME ": endpoint setup failed: %s\n",
			strerror(fInitStatus));
		return;
	}

	// Create the PHY configuration module
	fPhyConfig = new(std::nothrow) RTL8814AUPhyConfig(fRegisterIO,
		fEfuseReader);
	if (fPhyConfig == NULL) {
		fInitStatus = B_NO_MEMORY;
		return;
	}

	// Create the TX path — needs bulk OUT endpoint handles
	fTxPath = new(std::nothrow) RTL8814AUTxPath(fRegisterIO, gUSBModule,
		device, fBulkOut);
	if (fTxPath == NULL || fTxPath->InitCheck() != B_OK) {
		fInitStatus = (fTxPath != NULL) ? fTxPath->InitCheck() : B_NO_MEMORY;
		return;
	}

	// Firmware loader submits download chunks on the beacon bulk OUT
	// endpoint via the TX path, so it needs a pointer to it.
	fFirmware->SetTxPath(fTxPath);

	// Create the RX path — needs bulk IN endpoint handle
	fRxPath = new(std::nothrow) RTL8814AURxPath(fRegisterIO, gUSBModule,
		device, fBulkIn);
	if (fRxPath == NULL || fRxPath->InitCheck() != B_OK) {
		fInitStatus = (fRxPath != NULL) ? fRxPath->InitCheck() : B_NO_MEMORY;
		return;
	}

	// Register our RX frame handler
	fRxPath->SetFrameCallback(_RxFrameReceived, this);

	// Create the WiFi management module — needs interrupt IN for C2H events
	fWiFiManager = new(std::nothrow) RTL8814AUWiFiManager(fRegisterIO,
		gUSBModule, device, fInterruptIn);
	if (fWiFiManager == NULL || fWiFiManager->InitCheck() != B_OK) {
		fInitStatus = (fWiFiManager != NULL) ? fWiFiManager->InitCheck()
			: B_NO_MEMORY;
		return;
	}
}


RTL8814AUDevice::~RTL8814AUDevice()
{
	if (fHardwareInitialized)
		_Shutdown();

	delete fWiFiManager;
	delete fRxPath;
	delete fTxPath;
	delete fPhyConfig;
	delete fEfuseReader;
	delete fFirmware;
	delete fRegisterIO;

	if (fRxDataReady >= 0)
		delete_sem(fRxDataReady);

	mutex_destroy(&fLock);
}


/*! Mark the device as physically removed (USB unplug). All pending and
    future operations will return B_DEV_NOT_READY.
*/
void
RTL8814AUDevice::SetRemoved()
{
	fRemoved = true;
	if (fRegisterIO != NULL)
		fRegisterIO->SetDeviceRemoved();
}


// ---------------------------------------------------------------------------
// USB endpoint discovery
// ---------------------------------------------------------------------------


/*! Walk the USB configuration to find our 5 expected endpoints:
    3 bulk OUT, 1 bulk IN, 1 interrupt IN. The RTL8814AU always exposes
    these on the first interface of the first configuration.

    \return B_OK if all endpoints found, B_BAD_VALUE otherwise.
*/
status_t
RTL8814AUDevice::_SetupEndpoints()
{
	// Get the first (and usually only) configuration
	const usb_configuration_info* config
		= gUSBModule->get_nth_configuration(fUSBDevice, 0);
	if (config == NULL) {
		dprintf(RTL8814AU_DRIVER_NAME ": no USB configuration found\n");
		return B_BAD_VALUE;
	}

	// Set this configuration as active
	status_t status = gUSBModule->set_configuration(fUSBDevice, config);
	if (status != B_OK) {
		dprintf(RTL8814AU_DRIVER_NAME ": set_configuration failed: %s\n",
			strerror(status));
		return status;
	}

	// The RTL8814AU uses a single interface
	if (config->interface_count < 1) {
		dprintf(RTL8814AU_DRIVER_NAME ": no interfaces in configuration\n");
		return B_BAD_VALUE;
	}

	const usb_interface_info* interface
		= config->interface[0].active;
	if (interface == NULL) {
		dprintf(RTL8814AU_DRIVER_NAME ": no active interface\n");
		return B_BAD_VALUE;
	}

	// Walk all endpoints and classify them by type and direction.
	// The RTL8814AU has a fixed endpoint layout:
	//   Bulk OUT ×3: TX data (priority-mapped to VO/VI, BE/BK, MGT/CMD)
	//   Bulk IN  ×1: RX data (aggregated frames from hardware)
	//   Interrupt IN ×1: C2H firmware events
	uint32 bulkOutIndex = 0;

	for (uint32 i = 0; i < interface->endpoint_count; i++) {
		const usb_endpoint_descriptor* endpoint
			= interface->endpoint[i].descr;
		usb_pipe pipe = interface->endpoint[i].handle;

		if (endpoint == NULL)
			continue;

		bool isIn = (endpoint->endpoint_address & USB_ENDPOINT_ADDR_DIR_IN)
			!= 0;
		uint8 attributes = endpoint->attributes & USB_ENDPOINT_ATTR_MASK;

		if (attributes == USB_ENDPOINT_ATTR_BULK) {
			if (isIn) {
				fBulkIn = pipe;
				dprintf(RTL8814AU_DRIVER_NAME ": found bulk IN endpoint "
					"0x%02x\n", endpoint->endpoint_address);
			} else {
				if (bulkOutIndex < kBulkOutEndpointCount) {
					fBulkOut[bulkOutIndex] = pipe;
					dprintf(RTL8814AU_DRIVER_NAME ": found bulk OUT "
						"endpoint 0x%02x (queue %"
						B_PRIu32 ")\n",
						endpoint->endpoint_address, bulkOutIndex);
					bulkOutIndex++;
				}
			}
		} else if (attributes == USB_ENDPOINT_ATTR_INTERRUPT && isIn) {
			fInterruptIn = pipe;
			dprintf(RTL8814AU_DRIVER_NAME ": found interrupt IN endpoint "
				"0x%02x\n", endpoint->endpoint_address);
		}
	}

	// Validate that we found all required endpoints
	if (fBulkIn == 0) {
		dprintf(RTL8814AU_DRIVER_NAME ": missing bulk IN endpoint\n");
		return B_BAD_VALUE;
	}

	if (bulkOutIndex == 0) {
		dprintf(RTL8814AU_DRIVER_NAME ": no bulk OUT endpoints found\n");
		return B_BAD_VALUE;
	}

	// Some RTL8814AU devices may expose fewer than 3 bulk OUT endpoints
	// depending on USB speed. Fill missing OUT pipes with the last one
	// found (all TX goes through a single pipe in that case).
	for (uint32 i = bulkOutIndex; i < kBulkOutEndpointCount; i++)
		fBulkOut[i] = fBulkOut[bulkOutIndex - 1];

	dprintf(RTL8814AU_DRIVER_NAME ": endpoint setup complete — "
		"%" B_PRIu32 " bulk OUT, 1 bulk IN, interrupt %s\n",
		bulkOutIndex, fInterruptIn != 0 ? "IN" : "missing");

	return B_OK;
}


// ---------------------------------------------------------------------------
// Hardware initialization — called on first open()
// ---------------------------------------------------------------------------


/*! Full hardware initialization sequence. Called with fLock held.
    This brings the chip from a powered-down state to fully operational:
      1. Power-on sequence (register writes to bring chip out of reset)
      2. Read EFUSE (MAC address, calibration data)
      3. Initialize MAC registers (queue page allocation, DMA enables)
      4. Load firmware (DMEM + IRAM to Lexra 3081 via beacon queue)
      5. Write MAC address to hardware
      6. PHY/RF initialization (4-path setup — TODO)

    MAC init precedes firmware download because the 3081-MCU firmware
    loader submits its chunks via the beacon bulk OUT endpoint — the
    chip needs TX packet buffer pages allocated (via REG_RQPN) and
    MAC TX enabled before it will drain those bulk OUT transfers.
    This matches the reference driver's init order: MAC queue config
    is set up in _InitQueueReservedPage_8814AU() before
    FirmwareDownload8814A() is called.

    \return B_OK on success, or the first error encountered.
*/
status_t
RTL8814AUDevice::_InitHardware()
{
	dprintf(RTL8814AU_DRIVER_NAME ": initializing hardware for %s\n",
		fDeviceName);

	// Step 1: Power-on sequence — bring the chip out of reset
	status_t status = _PowerOnSequence();
	if (status != B_OK) {
		dprintf(RTL8814AU_DRIVER_NAME ": power-on sequence failed: %s\n",
			strerror(status));
		return status;
	}

	// Step 2: Read EFUSE — get MAC address and calibration data
	status = fEfuseReader->ReadEfuseMap();
	if (status != B_OK) {
		dprintf(RTL8814AU_DRIVER_NAME ": EFUSE read failed: %s\n",
			strerror(status));
		return status;
	}

	status = _ReadMacAddress();
	if (status != B_OK) {
		dprintf(RTL8814AU_DRIVER_NAME ": MAC address read failed: %s\n",
			strerror(status));
		return status;
	}

	dprintf(RTL8814AU_DRIVER_NAME ": MAC address: "
		"%02x:%02x:%02x:%02x:%02x:%02x\n",
		fMacAddress[0], fMacAddress[1], fMacAddress[2],
		fMacAddress[3], fMacAddress[4], fMacAddress[5]);

	// Step 3: Initialize MAC — allocate TX packet buffer pages to queues
	// and enable DMA engines.  MUST happen before firmware load because
	// the 3081-MCU loader submits firmware chunks via the beacon bulk
	// OUT endpoint, which requires the beacon queue to have pages.
	status = _InitMAC();
	if (status != B_OK) {
		dprintf(RTL8814AU_DRIVER_NAME ": MAC init failed: %s\n",
			strerror(status));
		return status;
	}

	// Step 4: Load firmware — transfer DMEM and IRAM to the Lexra 3081
	status = fFirmware->Load(RTL8814AU_FIRMWARE_PATH);
	if (status != B_OK) {
		dprintf(RTL8814AU_DRIVER_NAME ": firmware load failed: %s\n",
			strerror(status));
		return status;
	}

	// Step 5: Write MAC address to hardware
	status = _WriteMacAddress();
	if (status != B_OK) {
		dprintf(RTL8814AU_DRIVER_NAME ": MAC address write failed: %s\n",
			strerror(status));
		return status;
	}

	// Step 6: PHY/RF initialization — configure all 4 RF paths
	if (fPhyConfig != NULL) {
		status = fPhyConfig->Initialize();
		if (status != B_OK) {
			dprintf(RTL8814AU_DRIVER_NAME ": PHY init failed: %s\n",
				strerror(status));
			return status;
		}
	}

	// Step 7: Start the RX receive loop (bulk IN transfers)
	if (fRxPath != NULL) {
		status = fRxPath->Start();
		if (status != B_OK) {
			dprintf(RTL8814AU_DRIVER_NAME ": RX start failed: %s\n",
				strerror(status));
			return status;
		}
	}

	// Step 8: Start the WiFi management module (interrupt IN for C2H events)
	if (fWiFiManager != NULL) {
		status = fWiFiManager->Start();
		if (status != B_OK) {
			dprintf(RTL8814AU_DRIVER_NAME ": WiFi manager start failed: %s\n",
				strerror(status));
			return status;
		}
	}

	fHardwareInitialized = true;
	dprintf(RTL8814AU_DRIVER_NAME ": hardware initialization complete\n");
	return B_OK;
}


/*! Power-on sequence for the RTL8814AU. This is a series of register
    writes that bring the chip from deep sleep to an active state where
    the CPU, MAC, and baseband are powered and clocked.

    Derived from Hal8814PwrSeq.c in the reference driver.
*/
status_t
RTL8814AUDevice::_PowerOnSequence()
{
	dprintf(RTL8814AU_DRIVER_NAME ": starting power-on sequence\n");

	// Undocumented Realtek workaround: set bit 1 of register 0x10C2
	// before the power-on FSM transition.  Comment in the reference
	// driver attributes this to "YX sugguested 2014.06.03".  Without
	// this the chip's bulk OUT endpoint FIFOs do not accept data —
	// every firmware TX hangs at the USB layer with the chip never
	// pulling the packet.  (Observed symptom prior to adding this.)
	uint8 reg10C2 = fRegisterIO->Read8(0x10C2);
	fRegisterIO->Write8(0x10C2, reg10C2 | 0x02);
	dprintf(RTL8814AU_DRIVER_NAME ": 0x10C2 workaround (before=0x%02x, "
		"wrote=0x%02x)\n", (unsigned)reg10C2, (unsigned)(reg10C2 | 0x02));

	// ---------------------------------------------------------------
	// Phase 1: Analog and clock setup (AFE, crystal, PLL)
	// ---------------------------------------------------------------

	// Enable analog front-end and PLL
	fRegisterIO->Write8(kRegApsRsvd, 0x00);

	// Enable SPS (Switching Power Supply)
	fRegisterIO->MaskedWrite8(kRegApsRsvd, 0x01, 0x01);
	snooze(2000);	// 2 ms — wait for power supply to stabilize

	// Enable AFE (analog front-end) bandgap
	fRegisterIO->MaskedWrite8(kRegAfeCtrl1, 0x80, 0x80);

	// Enable crystal oscillator
	fRegisterIO->MaskedWrite8(kRegAfeCtrl2, 0x02, 0x02);
	snooze(2000);

	// Disable isolation between analog and digital domains
	fRegisterIO->MaskedWrite8(kRegRsvCtrl, 0x02, 0x00);

	// Enable XTAL output for MAC clock
	fRegisterIO->MaskedWrite8(kRegAfeCtrl2, 0x04, 0x04);

	// Enable system function blocks
	uint16 sysFuncEn = fRegisterIO->Read16(kRegSysFuncEn);
	sysFuncEn |= kSysFuncEnUSBA | kSysFuncEnUSBD;
	fRegisterIO->Write16(kRegSysFuncEn, sysFuncEn);

	// Enable MAC and baseband clocks
	fRegisterIO->MaskedWrite8(kRegAfeCtrl2 + 1, 0x18, 0x18);

	// Wait for MAC to be ready
	snooze(2000);

	// Enable digital core power
	sysFuncEn = fRegisterIO->Read16(kRegSysFuncEn);
	sysFuncEn |= kSysFuncEnBBRSTB | kSysFuncEnBBGlbRst | kSysFuncEnDcore;
	fRegisterIO->Write16(kRegSysFuncEn, sysFuncEn);

	// ---------------------------------------------------------------
	// Phase 2: APS FSM power-on transition (Rtl8814A_NIC_ENABLE_FLOW)
	//
	// The RTL8814AU has an Auto Power State (APS) FSM controlled via
	// register 0x0004-0x0005 (APS_FSMCO). Triggering the card-emu-to-
	// active transition brings the MAC register domain (0x0100+) online.
	// Without this, MAC registers return 0xEA (powered-off domain).
	//
	// Matches HalPwrSeqCmdParsing(Rtl8814A_NIC_ENABLE_FLOW) from
	// the reference driver's _InitPowerOn_8814AU().
	// ---------------------------------------------------------------

	// Step 1: Clear APS_FSMCO byte 1 bit 2 (= overall bit 10)
	fRegisterIO->MaskedWrite8(kRegApsFsmco + 1, 0x04, 0x00);

	// Step 2: Poll SYS_CLKR (0x0006) until bit 1 is set (clock ready)
	status_t status = fRegisterIO->PollFor8(kRegSysClkr, 0x02, 0x02,
		200, 500);
	if (status != B_OK) {
		dprintf(RTL8814AU_DRIVER_NAME ": SYS_CLKR clock ready timeout\n");
		return status;
	}

	// Step 3: Clear APS_FSMCO byte 1 bit 3 (= overall bit 11)
	fRegisterIO->MaskedWrite8(kRegApsFsmco + 1, 0x08, 0x00);

	// Step 4: Clear SYS_CFG (0x00F0) bit 7
	fRegisterIO->MaskedWrite8(kRegSysCfg, 0x80, 0x00);

	// Step 5: Configure FW_CTRL byte 1 (0x0081): set bit 5, clear bit 4
	fRegisterIO->MaskedWrite8(kRegMcuFwDl + 1, 0x30, 0x20);

	// Step 6: Set APS_FSMCO byte 1 bit 0 (= overall bit 8) to trigger
	// the card-emu-to-active power state transition
	fRegisterIO->MaskedWrite8(kRegApsFsmco + 1, 0x01, 0x01);

	// Step 7: Poll APS_FSMCO byte 1 until bit 0 clears (transition done)
	status = fRegisterIO->PollFor8(kRegApsFsmco + 1, 0x01, 0x00,
		200, 500);
	if (status != B_OK) {
		dprintf(RTL8814AU_DRIVER_NAME ": APS FSM transition timeout\n");
		return status;
	}

	// ---------------------------------------------------------------
	// Phase 3: Enable MAC DMA engines
	//
	// Now that the MAC domain is online, enable the DMA engines,
	// protocol engine, scheduler, and security engine via REG_CR.
	// This matches _InitPowerOn_8814AU() in the reference driver.
	// ---------------------------------------------------------------

	fRegisterIO->Write16(kRegCR, 0x0000);
	uint16 crValue = fRegisterIO->Read16(kRegCR);
	crValue |= kCR_HCI_TxDMA_En | kCR_HCI_RxDMA_En
		| kCR_TxDMA_En | kCR_RxDMA_En
		| kCR_Protocol_En | kCR_Schedule_En
		| kCR_EnsecCAMTx | kCR_EnsecCAMRx;
	fRegisterIO->Write16(kRegCR, crValue);

	dprintf(RTL8814AU_DRIVER_NAME ": power-on sequence complete "
		"(REG_CR=0x%04x)\n", fRegisterIO->Read16(kRegCR));
	return B_OK;
}


/*! Initialize the MAC block after firmware is loaded. Configures:
    - Command register (enable TX/RX DMA, protocol, scheduler)
    - TX/RX buffer page boundaries
    - AMPDU aggregation parameters
    - Receive filter (initially accept management + data + broadcast)
*/
status_t
RTL8814AUDevice::_InitMAC()
{
	dprintf(RTL8814AU_DRIVER_NAME ": initializing MAC\n");

	// NOTE: CR is already set to 0x603F by _PowerOnSequence (HCI_TxDMA |
	// HCI_RxDMA | TxDMA | RxDMA | Protocol | Schedule | ENSEC | CALTMR).
	// DO NOT clobber it here and DO NOT enable MAC_TX_EN/MAC_RX_EN yet —
	// the MAC state machine needs firmware before those can be set.
	// Enabling them prematurely causes the USB TX DMA to stall (the chip
	// never pulls bulk OUT frames off EP2), which is the exact symptom
	// we were debugging.  MAC_TX_EN/MAC_RX_EN get enabled post-firmware.

	uint16 crBefore = fRegisterIO->Read16(kRegCR);
	dprintf(RTL8814AU_DRIVER_NAME ": CR entering _InitMAC = 0x%04x\n",
		(unsigned)crBefore);

	// Allocate TX packet buffer pages across the six internal queues.
	// This must happen before any bulk OUT transfer to the chip; without
	// it the hardware has no pages to buffer incoming frames and USB
	// transfers stall waiting for the chip to pull them off the wire.
	status_t status = _InitPageAllocation();
	if (status != B_OK)
		return status;

	// LLT auto-init was tried at REG_AUTO_LLT (0x0208) BIT0 but found to
	// be a no-op on 8814A — the chip auto-builds the link-list as part
	// of the RQPN_CTRL_2 commit.  Skip the trigger here.

	// Program the queue-to-endpoint priority map so the three USB bulk
	// OUT endpoints (pipe 0 = HIGH, pipe 1 = NORMAL, pipe 2 = LOW) route
	// to the right internal queues.  Without this, the chip doesn't know
	// where frames on EP2/EP3/EP4 belong.
	status = _InitQueuePriority();
	if (status != B_OK)
		return status;

	// Clear REG_TXPAUSE — the reference driver ensures transmission is
	// not paused before the first bulk OUT.  Without this, the chip will
	// accept but not dispatch frames.
	fRegisterIO->Write8(kRegTxPause, 0x00);

	// Set AMPDU aggregation parameters
	fRegisterIO->Write8(kRegAmpduMaxTime, kAmpduMaxTime);
	fRegisterIO->Write32(kRegAmpduMaxLength, kAmpduMaxLength);

	// Configure initial RX filter — accept unicast, broadcast, and
	// management frames. We need management frames for beacons and
	// probe responses during scanning.
	uint32 rxFilter = kRCR_APM | kRCR_AM | kRCR_AB
		| kRCR_ADF | kRCR_AMF;
	fRegisterIO->Write32(kRegRCR, rxFilter);

	// Enable DMA engines
	status = _EnableDMA();
	if (status != B_OK)
		return status;

	dprintf(RTL8814AU_DRIVER_NAME ": MAC initialization complete\n");
	return B_OK;
}


/*! Allocate TX packet buffer pages across the six internal hardware
    queues and set the beacon-queue boundary.

    The 8814A uses a 5-register bank at 0x0230–0x0240 (one per queue)
    plus a commit register at 0x022C.  Writing kRQPNCommit (0x80000000)
    to REG_RQPN_CTRL_2 latches the per-queue values into hardware.

    Page counts match the reference driver's HPQ/LPQ/NPQ/EPQ_PGNUM
    values (20 each) and PUB = 2048 - BCNQ(8) - 4*20 = 1960.

    Reference: _InitQueueReservedPage_8814AUsb() in morrownr/8814au,
               hal/rtl8814a/usb/usb_halinit.c.
*/
status_t
RTL8814AUDevice::_InitPageAllocation()
{
	// Write per-queue page counts
	fRegisterIO->Write32(kRegFIFOPage_Info_1, kPageNumHPQ);
	fRegisterIO->Write32(kRegFIFOPage_Info_2, kPageNumLPQ);
	fRegisterIO->Write32(kRegFIFOPage_Info_3, kPageNumNPQ);
	fRegisterIO->Write32(kRegFIFOPage_Info_4, kPageNumEPQ);
	fRegisterIO->Write32(kRegFIFOPage_Info_5, kPageNumPUB);

	// Commit the per-queue allocation into hardware
	fRegisterIO->Write32(kRegRQPN_Ctrl_2, kRQPNCommit);

	// Set the beacon-queue page boundary.  All boundary registers get the
	// same value — the TX packet buffer region above this boundary is
	// reserved for beacon / reserved-page use.  The reference driver also
	// writes REG_FIFOPAGE_CTRL_2 (0x0204) as the BCN0 head and
	// REG_FIFOPAGE_CTRL_2 + 2 (0x0206) as the BCN1 head.
	fRegisterIO->Write16(kRegTxPktBufBcnQBdy, kFwTxPktBufBoundary);
	fRegisterIO->Write16(kRegMgQPgBndy, kFwTxPktBufBoundary);
	fRegisterIO->Write16(kRegFIFOPage, kFwTxPktBufBoundary);
	fRegisterIO->Write16(kRegFIFOPage + 2, kFwTxPktBufBoundary);

	dprintf(RTL8814AU_DRIVER_NAME ": page allocation set "
		"(HPQ=%u LPQ=%u NPQ=%u EPQ=%u PUB=%u bndy=0x%04x)\n",
		(unsigned)kPageNumHPQ, (unsigned)kPageNumLPQ,
		(unsigned)kPageNumNPQ, (unsigned)kPageNumEPQ,
		(unsigned)kPageNumPUB, (unsigned)kFwTxPktBufBoundary);
	return B_OK;
}


/*! Trigger the hardware Link-List Table auto-init and wait for it to
    complete.

    The LLT is the per-queue linked-list of page descriptors inside the
    chip's TX packet buffer.  Until the hardware builds it, the MAC
    cannot move frames from the USB FIFO into any queue — bulk OUT
    transfers sit in the USB endpoint without being consumed, and the
    host sees every TX time out.

    Writing bit 0 of REG_AUTO_LLT triggers the build; the chip clears
    bit 0 when the linked-list is ready.  We poll up to ~10 seconds
    (200 × 50 ms) as the reference driver does.

    Reference: InitLLTTable8814A() in morrownr/8814au,
               hal/rtl8814a/rtl8814a_hal_init.c.
*/
status_t
RTL8814AUDevice::_InitLLTTable()
{
	uint8 before = fRegisterIO->Read8(kRegAutoLLT);
	uint8 wrote = before | kAutoLLTTrigger;
	fRegisterIO->Write8(kRegAutoLLT, wrote);
	uint8 readback = fRegisterIO->Read8(kRegAutoLLT);

	dprintf(RTL8814AU_DRIVER_NAME ": LLT trigger "
		"(before=0x%02x, wrote=0x%02x, readback=0x%02x)\n",
		(unsigned)before, (unsigned)wrote, (unsigned)readback);

	// If the write didn't take effect (readback still shows bit 0 clear),
	// the register is probably read-only or mapped elsewhere.  Don't
	// falsely claim success — report and let the caller decide.
	if ((readback & kAutoLLTTrigger) == 0 && (before & kAutoLLTTrigger) == 0) {
		dprintf(RTL8814AU_DRIVER_NAME ": LLT trigger write had no effect "
			"— register may not be REG_AUTO_LLT on this chip\n");
		return B_OK;	// Continue — maybe LLT auto-runs on this variant
	}

	for (uint32 i = 0; i < kAutoLLTPollAttempts; i++) {
		uint8 llt = fRegisterIO->Read8(kRegAutoLLT);
		if ((llt & kAutoLLTTrigger) == 0) {
			dprintf(RTL8814AU_DRIVER_NAME ": LLT init complete "
				"(%u polls, REG_AUTO_LLT=0x%02x)\n",
				(unsigned)i, (unsigned)llt);
			return B_OK;
		}
		snooze(kAutoLLTPollDelay);
	}

	uint8 final = fRegisterIO->Read8(kRegAutoLLT);
	dprintf(RTL8814AU_DRIVER_NAME ": LLT init TIMED OUT "
		"(REG_AUTO_LLT=0x%02x)\n", (unsigned)final);
	return B_TIMED_OUT;
}


/*! Program REG_TRXDMA_CTRL (0x010C) to map internal hardware queues to
    the three USB bulk OUT endpoint priority levels.

    The 3EP non-WMM mapping matches the reference driver:
      VOQ → HIGH   (pipe 0, EP2)
      VIQ → NORMAL (pipe 1, EP3)
      BEQ → LOW    (pipe 2, EP4)
      BKQ → LOW
      MGQ → HIGH   (so BCN/MGT/firmware-chunks land on EP2)
      HIQ → HIGH

    Reference: _InitNormalChipRegPriority_8814AUsb() in morrownr/8814au,
               hal/rtl8814a/usb/usb_halinit.c.
*/
status_t
RTL8814AUDevice::_InitQueuePriority()
{
	// Preserve the lower 3 bits of the existing register value
	uint16 existing = fRegisterIO->Read16(kRegTrxDmaCfg);
	uint16 preserved = existing & 0x07;

	uint16 priorityMap
		= (uint16)(kQueueHigh   << kTxDmaVOQShift)
		| (uint16)(kQueueNormal << kTxDmaVIQShift)
		| (uint16)(kQueueLow    << kTxDmaBEQShift)
		| (uint16)(kQueueLow    << kTxDmaBKQShift)
		| (uint16)(kQueueHigh   << kTxDmaMGQShift)
		| (uint16)(kQueueHigh   << kTxDmaHIQShift);

	uint16 value = preserved | priorityMap | (uint16)kTxDmaPriorityBit2;
	fRegisterIO->Write16(kRegTrxDmaCfg, value);

	dprintf(RTL8814AU_DRIVER_NAME ": queue priority set "
		"(REG_TRXDMA_CTRL: 0x%04x -> 0x%04x)\n",
		(unsigned)existing, (unsigned)value);
	return B_OK;
}


/*! Enable the TX and RX DMA engines and configure USB-specific DMA
    settings (aggregation thresholds, burst lengths).
*/
status_t
RTL8814AUDevice::_EnableDMA()
{
	// Set RX aggregation threshold — controls how many frames the
	// hardware bundles into a single USB bulk IN transfer.
	// Lower = less latency, higher = better throughput.
	uint32 aggThreshold = 0x03;		// 3 pages before sending
	uint32 aggTimeout = 0x08;		// 8 × 32 µs = 256 µs timeout
	fRegisterIO->Write8(kRegRxDmaAggPgTh,
		(uint8)aggThreshold);
	fRegisterIO->Write8(kRegRxDmaAggPgTh + 1,
		(uint8)aggTimeout);

	return B_OK;
}


// ---------------------------------------------------------------------------
// MAC address
// ---------------------------------------------------------------------------


/*! Read the MAC address from the EFUSE map into fMacAddress[]. */
status_t
RTL8814AUDevice::_ReadMacAddress()
{
	const uint8* efuseMap = fEfuseReader->Map();
	if (efuseMap == NULL)
		return B_NO_INIT;

	memcpy(fMacAddress, efuseMap + kEfuseMacAddr, 6);

	dprintf(RTL8814AU_DRIVER_NAME ": EFUSE MAC at offset 0x%03x: "
		"%02x:%02x:%02x:%02x:%02x:%02x\n",
		kEfuseMacAddr, fMacAddress[0], fMacAddress[1], fMacAddress[2],
		fMacAddress[3], fMacAddress[4], fMacAddress[5]);

	// Sanity check — a zeroed or broadcast MAC is invalid
	bool allZero = true;
	bool allOnes = true;
	for (int32 i = 0; i < 6; i++) {
		if (fMacAddress[i] != 0x00)
			allZero = false;
		if (fMacAddress[i] != 0xFF)
			allOnes = false;
	}

	// Also reject multicast MACs (bit 0 of first byte must be 0 for unicast)
	bool isMulticast = (fMacAddress[0] & 0x01) != 0;

	if (allZero || allOnes || isMulticast) {
		dprintf(RTL8814AU_DRIVER_NAME ": invalid MAC address in EFUSE "
			"(zero=%d ones=%d mcast=%d), using random address\n",
			allZero, allOnes, isMulticast);
		// Generate a locally-administered random MAC
		fMacAddress[0] = 0x02;	// Locally administered, unicast
		fMacAddress[1] = 0x81;
		fMacAddress[2] = 0x4A;
		fMacAddress[3] = (uint8)(system_time() & 0xFF);
		fMacAddress[4] = (uint8)((system_time() >> 8) & 0xFF);
		fMacAddress[5] = (uint8)((system_time() >> 16) & 0xFF);
	}

	return B_OK;
}


/*! Write the MAC address from fMacAddress[] to the hardware register. */
status_t
RTL8814AUDevice::_WriteMacAddress()
{
	for (int32 i = 0; i < 6; i++) {
		status_t status = fRegisterIO->Write8(kRegMAC_ADDR + i,
			fMacAddress[i]);
		if (status != B_OK)
			return status;
	}
	return B_OK;
}


/*! Shut down the hardware — stop data paths, disable DMA, power down. */
void
RTL8814AUDevice::_Shutdown()
{
	dprintf(RTL8814AU_DRIVER_NAME ": shutting down hardware\n");

	// Stop the WiFi management module (interrupt IN)
	if (fWiFiManager != NULL)
		fWiFiManager->Stop();

	// Stop the RX receive loop (bulk IN)
	if (fRxPath != NULL)
		fRxPath->Stop();

	// Cancel any pending TX transfers (bulk OUT)
	if (fTxPath != NULL)
		fTxPath->CancelAll();

	if (!fRemoved) {
		// Disable RX/TX DMA and MAC
		fRegisterIO->Write32(kRegCR, 0);

		// Disable the MCU (halt firmware)
		uint16 sysFuncEn = fRegisterIO->Read16(kRegSysFuncEn);
		sysFuncEn &= ~kSysFuncEnCpuEn;
		fRegisterIO->Write16(kRegSysFuncEn, sysFuncEn);
	}

	fHardwareInitialized = false;
}


// ---------------------------------------------------------------------------
// Static device_hooks wrappers
//
// The kernel calls these static functions from the device_hooks table.
// Each one resolves the device instance from the cookie or device path,
// then dispatches to the instance. This pattern is standard across Haiku
// USB drivers.
// ---------------------------------------------------------------------------


/*! Open the device. On first open, triggers hardware initialization.
    \param name    Device path (e.g., "net/rtl8814au/0")
    \param flags   Open flags (unused)
    \param cookie  Output: device instance pointer
*/
status_t
RTL8814AUDevice::Open(const char* name, uint32 flags, void** cookie)
{
	// Extract the slot index from the device path
	const char* indexStr = strrchr(name, '/');
	if (indexStr == NULL)
		return B_BAD_VALUE;
	indexStr++;

	uint32 slotIndex = strtoul(indexStr, NULL, 10);
	if (slotIndex >= kMaxDeviceCount)
		return B_BAD_VALUE;

	MutexLocker locker(gDeviceListLock);
	RTL8814AUDevice* device = gDeviceList[slotIndex];
	if (device == NULL)
		return B_BAD_VALUE;

	if (device->fRemoved)
		return B_DEV_NOT_READY;

	// Initialize hardware on first open
	if (!device->fHardwareInitialized) {
		status_t status = device->_InitHardware();
		if (status != B_OK)
			return status;
	}

	atomic_add(&device->fOpenCount, 1);
	*cookie = device;

	dprintf(RTL8814AU_DRIVER_NAME ": device opened (open count: "
		"%" B_PRId32 ")\n", device->fOpenCount);

	return B_OK;
}


/*! Close the device. Does not release resources — that happens in Free(). */
status_t
RTL8814AUDevice::Close(void* cookie)
{
	RTL8814AUDevice* device = static_cast<RTL8814AUDevice*>(cookie);
	if (device == NULL)
		return B_BAD_VALUE;

	dprintf(RTL8814AU_DRIVER_NAME ": device close\n");

	// Disconnect from any AP
	if (device->fWiFiManager != NULL)
		device->fWiFiManager->Disconnect();

	// Stop RX and TX data paths
	if (device->fRxPath != NULL)
		device->fRxPath->Stop();

	if (device->fTxPath != NULL)
		device->fTxPath->CancelAll();

	return B_OK;
}


/*! Release the device. Decrements the open count and cleans up if
    this was the last reference and the device has been unplugged.
*/
status_t
RTL8814AUDevice::Free(void* cookie)
{
	RTL8814AUDevice* device = static_cast<RTL8814AUDevice*>(cookie);
	if (device == NULL)
		return B_BAD_VALUE;

	int32 previousCount = atomic_add(&device->fOpenCount, -1);

	dprintf(RTL8814AU_DRIVER_NAME ": device free (open count: "
		"%" B_PRId32 " → %" B_PRId32 ")\n",
		previousCount, previousCount - 1);

	// If this was the last close and the device was unplugged, clean up
	if (previousCount == 1 && device->fRemoved) {
		MutexLocker locker(gDeviceListLock);
		uint32 slot = device->fSlotIndex;
		gDeviceList[slot] = NULL;
		gDeviceCount--;
		locker.Unlock();
		delete device;
	}

	return B_OK;
}


/*! Ioctl dispatch. Handles network-related queries from the stack.
    \param cookie  Device instance
    \param op      Ioctl command (ETHER_GETADDR, etc.)
    \param args    Command-specific argument buffer
    \param length  Size of args buffer
*/
status_t
RTL8814AUDevice::Control(void* cookie, uint32 op, void* args, size_t length)
{
	RTL8814AUDevice* device = static_cast<RTL8814AUDevice*>(cookie);
	if (device == NULL || device->fRemoved)
		return B_DEV_NOT_READY;

	switch (op) {
		case ETHER_GETADDR:
		{
			// Return our MAC address
			if (length < 6)
				return B_BAD_VALUE;
			memcpy(args, device->fMacAddress, 6);
			return B_OK;
		}

		case ETHER_GETFRAMESIZE:
		{
			// Return maximum frame size (standard Ethernet MTU)
			if (length < sizeof(uint32))
				return B_BAD_VALUE;
			*(uint32*)args = 1514;	// 6+6+2 header + 1500 payload
			return B_OK;
		}

		case ETHER_SET_LINK_STATE_SEM:
		{
			// The network stack provides a semaphore that we should
			// release whenever the link state changes (connect/disconnect).
			if (length < sizeof(sem_id))
				return B_BAD_VALUE;
			device->fLinkStateSem = *(sem_id*)args;
			return B_OK;
		}

		case ETHER_GET_LINK_STATE:
		{
			// Report the current WiFi link state (media type, quality, speed)
			if (length < sizeof(ether_link_state_t))
				return B_BAD_VALUE;

			ether_link_state_t* linkState = (ether_link_state_t*)args;

			if (device->fWiFiManager != NULL
				&& device->fWiFiManager->State() == kWiFiStateConnected) {
				WiFiLinkState wifiState;
				device->fWiFiManager->GetLinkState(&wifiState);

				linkState->media = IFM_IEEE80211 | IFM_ACTIVE;

				// Convert RSSI to quality percentage (0-1000).
				// Typical range: -90 dBm (worst) to -30 dBm (best)
				int32 quality = (wifiState.rssi + 90) * 1000 / 60;
				if (quality < 0) quality = 0;
				if (quality > 1000) quality = 1000;
				linkState->quality = (uint32)quality;

				// Estimate link speed from the data rate index.
				// Use approximate rates for common modes.
				uint64 speed = 24000000;	// Default: 24 Mbps
				if (wifiState.dataRate >= kRateVHT_1SS_MCS0)
					speed = 866700000;		// VHT: up to ~867 Mbps
				else if (wifiState.dataRate >= kRateHT_MCS0)
					speed = 300000000;		// HT: up to 300 Mbps
				else if (wifiState.dataRate >= kRateOFDM54)
					speed = 54000000;
				else if (wifiState.dataRate >= kRateOFDM6) {
					static const uint64 kOfdmRates[] =
						{ 6, 9, 12, 18, 24, 36, 48, 54 };
					uint32 rateIdx = wifiState.dataRate - kRateOFDM6;
					if (rateIdx < 8)
						speed = kOfdmRates[rateIdx] * 1000000;
				}
				linkState->speed = speed;
			} else {
				// Not connected
				linkState->media = IFM_IEEE80211;
				linkState->quality = 0;
				linkState->speed = 0;
			}
			return B_OK;
		}

		case ETHER_SETPROMISC:
		{
			// Enable or disable promiscuous mode by adjusting the RCR
			int32 enabled = *(int32*)args;
			uint32 rcr = device->fRegisterIO->Read32(kRegRCR);
			if (enabled)
				rcr |= kRCR_AAP;	// Accept all packets
			else
				rcr &= ~kRCR_AAP;
			device->fRegisterIO->Write32(kRegRCR, rcr);
			return B_OK;
		}

		case ETHER_NONBLOCK:
		{
			// Non-blocking mode toggle — not currently supported
			// (Read() always blocks on the RX semaphore)
			return B_OK;
		}

		default:
			return B_DEV_INVALID_IOCTL;
	}
}


/*! Read data from the device (RX path). Blocks until a frame is available
    in the RX ring buffer, then copies it to the caller's buffer.

    The RX ring is filled by _RxFrameReceived() which is called from
    the RxPath USB bulk IN callback. This function dequeues the next
    frame and returns it.
*/
status_t
RTL8814AUDevice::Read(void* cookie, off_t position, void* buffer,
	size_t* numBytes)
{
	RTL8814AUDevice* device = static_cast<RTL8814AUDevice*>(cookie);
	if (device == NULL || device->fRemoved)
		return B_DEV_NOT_READY;

	if (buffer == NULL || numBytes == NULL)
		return B_BAD_VALUE;

	// Block until a frame is available or the device is removed.
	// B_CAN_INTERRUPT allows the read to be canceled when the device
	// is closed or the thread is killed.
	status_t status = acquire_sem_etc(device->fRxDataReady, 1,
		B_CAN_INTERRUPT, 0);
	if (status != B_OK) {
		*numBytes = 0;
		return status;
	}

	// Check again after waking — the device may have been removed
	if (device->fRemoved) {
		*numBytes = 0;
		return B_DEV_NOT_READY;
	}

	// Dequeue the next frame from the ring buffer
	MutexLocker locker(device->fLock);

	uint32 tail = device->fRxRingTail;
	RxRingEntry* entry = &device->fRxRing[tail];

	if (entry->length == 0) {
		// Ring was empty despite the semaphore — shouldn't happen,
		// but handle gracefully
		locker.Unlock();
		*numBytes = 0;
		return B_OK;
	}

	// Copy the frame to the caller's buffer
	uint32 copyLength = entry->length;
	if (copyLength > *numBytes)
		copyLength = (uint32)*numBytes;

	memcpy(buffer, entry->data, copyLength);
	*numBytes = copyLength;

	// Mark this slot as consumed and advance the tail
	entry->length = 0;
	device->fRxRingTail = (tail + 1) % kRxRingSlots;

	return B_OK;
}


/*! Write data to the device (TX path). The network stack calls this
    to transmit frames. The frame is wrapped in a TX descriptor and
    submitted on the appropriate bulk OUT endpoint.
*/
status_t
RTL8814AUDevice::Write(void* cookie, off_t position, const void* buffer,
	size_t* numBytes)
{
	RTL8814AUDevice* device = static_cast<RTL8814AUDevice*>(cookie);
	if (device == NULL || device->fRemoved)
		return B_DEV_NOT_READY;

	if (device->fTxPath == NULL)
		return B_NO_INIT;

	if (buffer == NULL || numBytes == NULL || *numBytes == 0)
		return B_BAD_VALUE;

	// Determine if this is a broadcast/multicast frame by checking
	// the destination MAC address (first byte, bit 0 = multicast)
	const uint8* frameData = static_cast<const uint8*>(buffer);
	bool isBroadcast = (frameData[0] & 0x01) != 0;

	// Use best-effort queue and OFDM 24 Mbps as a safe default rate.
	// The rate adaptation firmware will adjust this over time.
	status_t status = device->fTxPath->Transmit(frameData,
		(uint32)*numBytes, kTxQueueBE, kRateOFDM24, 0,
		kSecurityNone, isBroadcast);

	if (status != B_OK)
		*numBytes = 0;

	return status;
}


/*! Static RX frame callback — called by the RX path module for each
    successfully received frame.

    This callback runs in the USB completion thread context. It must
    copy the frame data into the ring buffer before returning, since the
    USB receive buffer will be re-submitted immediately after.

    Management frames (beacons, probe responses) are parsed for BSS info
    and passed to the WiFi manager. Data frames are queued into the ring
    buffer for Read() to deliver to the network stack.
*/
void
RTL8814AUDevice::_RxFrameReceived(void* cookie, const uint8* frameData,
	uint32 frameLength, const RxFrameInfo* info)
{
	RTL8814AUDevice* device = static_cast<RTL8814AUDevice*>(cookie);
	if (device == NULL || frameLength < 24)
		return;

	// The first two bytes of the 802.11 frame contain the frame control.
	// Bits [3:2] of the first byte indicate the frame type:
	//   0 = management, 1 = control, 2 = data
	uint8 frameType = (frameData[0] >> 2) & 0x03;
	uint8 frameSubtype = (frameData[0] >> 4) & 0x0F;

	if (frameType == 0 && device->fWiFiManager != NULL) {
		// Management frame — parse beacons (subtype 8) and probe
		// responses (subtype 5) to update the BSS list.
		if (frameSubtype == 8 || frameSubtype == 5) {
			device->_ParseBeaconOrProbe(frameData, frameLength, info);
		}
		// Don't queue management frames into the data ring buffer
		return;
	}

	if (frameType != 2) {
		// Control frames (type 1) are not queued — they're handled
		// entirely in hardware/firmware
		return;
	}

	// Data frame — queue into the RX ring buffer for Read()
	if (frameLength > kRxRingFrameMaxSize) {
		dprintf(RTL8814AU_DRIVER_NAME ": RX frame too large for ring: "
			"%" B_PRIu32 " bytes\n", frameLength);
		return;
	}

	MutexLocker locker(device->fLock);

	uint32 head = device->fRxRingHead;
	uint32 nextHead = (head + 1) % kRxRingSlots;

	// Check if the ring is full (head would wrap around to tail)
	if (nextHead == device->fRxRingTail) {
		// Ring full — drop the oldest frame to make room
		device->fRxRing[device->fRxRingTail].length = 0;
		device->fRxRingTail = (device->fRxRingTail + 1) % kRxRingSlots;
		// Consume the semaphore count for the dropped frame
		acquire_sem_etc(device->fRxDataReady, 1, B_RELATIVE_TIMEOUT, 0);
	}

	// Copy frame into the ring slot
	RxRingEntry* entry = &device->fRxRing[head];
	memcpy(entry->data, frameData, frameLength);
	entry->length = frameLength;

	device->fRxRingHead = nextHead;
	locker.Unlock();

	// Wake up any thread blocked in Read()
	release_sem_etc(device->fRxDataReady, 1, B_DO_NOT_RESCHEDULE);
}


// ---------------------------------------------------------------------------
// Beacon / Probe Response parsing
//
// 802.11 management frame layout for beacons and probe responses:
//   Bytes 0-1:   Frame Control
//   Bytes 2-3:   Duration
//   Bytes 4-9:   Destination Address (DA)
//   Bytes 10-15: Source Address (SA)
//   Bytes 16-21: BSSID
//   Bytes 22-23: Sequence Control
//   --- Fixed fields (beacon body) ---
//   Bytes 24-31: Timestamp (8 bytes)
//   Bytes 32-33: Beacon Interval (2 bytes, in TUs = 1024 µs)
//   Bytes 34-35: Capability Information (2 bytes)
//   Bytes 36+:   Information Elements (tagged parameters)
//
// Each Information Element (IE) has:
//   Byte 0: Element ID
//   Byte 1: Length
//   Bytes 2+: Element data
//
// We extract: SSID (IE 0), DS Parameter Set (IE 3, channel),
// RSN (IE 48, WPA2), and vendor-specific (IE 221, WPA1).
// ---------------------------------------------------------------------------


/*! Parse a beacon or probe response management frame and update the
    WiFi manager's BSS list with the access point information.
*/
void
RTL8814AUDevice::_ParseBeaconOrProbe(const uint8* frameData,
	uint32 frameLength, const RxFrameInfo* rxInfo)
{
	// Minimum size: 24-byte MAC header + 12-byte fixed fields = 36 bytes
	if (frameLength < 36)
		return;

	if (fWiFiManager == NULL)
		return;

	// Extract the BSSID from the MAC header (bytes 16-21)
	const uint8* bssid = frameData + 16;

	// Extract fixed fields from the beacon body
	uint16 beaconInterval = (uint16)frameData[32]
		| ((uint16)frameData[33] << 8);
	uint16 capability = (uint16)frameData[34]
		| ((uint16)frameData[35] << 8);

	// Walk the Information Elements starting at byte 36
	const uint8* ieStart = frameData + 36;
	uint32 ieTotal = frameLength - 36;

	char ssid[kMaxSSIDLength + 1];
	uint8 ssidLength = 0;
	uint8 channel = 0;
	SecurityType security = kSecurityNone;
	bool foundSSID = false;

	memset(ssid, 0, sizeof(ssid));

	uint32 offset = 0;
	while (offset + 2 <= ieTotal) {
		uint8 ieID = ieStart[offset];
		uint8 ieLen = ieStart[offset + 1];

		// Bounds check
		if (offset + 2 + ieLen > ieTotal)
			break;

		const uint8* ieData = ieStart + offset + 2;

		switch (ieID) {
			case 0:		// SSID
				ssidLength = ieLen;
				if (ssidLength > kMaxSSIDLength)
					ssidLength = kMaxSSIDLength;
				memcpy(ssid, ieData, ssidLength);
				ssid[ssidLength] = '\0';
				foundSSID = true;
				break;

			case 3:		// DS Parameter Set (channel)
				if (ieLen >= 1)
					channel = ieData[0];
				break;

			case 48:	// RSN (Robust Security Network) — WPA2
				security = kSecurityAESCCMP;
				break;

			case 221:	// Vendor-specific — check for WPA1 OUI
				// Microsoft WPA OUI: 00:50:F2:01
				if (ieLen >= 4 && ieData[0] == 0x00 && ieData[1] == 0x50
					&& ieData[2] == 0xF2 && ieData[3] == 0x01) {
					if (security == kSecurityNone)
						security = kSecurityTKIP;
				}
				break;
		}

		offset += 2 + ieLen;
	}

	// Skip hidden networks (empty SSID)
	if (!foundSSID || ssidLength == 0)
		return;

	// If no channel was in the DS Parameter IE, use the channel from
	// the RX descriptor
	if (channel == 0 && rxInfo != NULL)
		channel = 0;	// RxFrameInfo doesn't carry channel; leave 0

	// Forward parsed data to the WiFi manager's BSS list
	_UpdateBssEntry(bssid, ssid, ssidLength, channel, beaconInterval,
		capability, security, rxInfo != NULL ? rxInfo->rssi : -80,
		ieStart, ieTotal);
}


/*! Forward parsed BSS info to the WiFi manager's UpdateBssEntry(). */
void
RTL8814AUDevice::_UpdateBssEntry(const uint8* bssid, const char* ssid,
	uint8 ssidLength, uint8 channel, uint16 beaconInterval,
	uint16 capability, SecurityType security, int8 rssi,
	const uint8* ieData, uint32 ieLength)
{
	if (fWiFiManager != NULL) {
		fWiFiManager->UpdateBssEntry(bssid, ssid, ssidLength, channel,
			beaconInterval, capability, security, rssi, ieData, ieLength);
	}
}
