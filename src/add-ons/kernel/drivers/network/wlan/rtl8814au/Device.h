/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Device.h — RTL8814AUDevice class declaration.
 *
 * This is the central device class that owns all subsystem modules and
 * coordinates the hardware lifecycle:
 *   1. USB endpoint discovery and configuration
 *   2. Hardware power-on sequence
 *   3. EFUSE reading (MAC address, calibration data)
 *   4. Firmware loading (Lexra 3081 DMEM + IRAM)
 *   5. PHY/RF initialization (4-path configuration)
 *   6. TX/RX data path management
 *   7. WiFi management (scan, associate, authenticate)
 *
 * The device is created in DeviceAdded() and destroyed either in
 * DeviceRemoved() (if not open) or in the last Free() call.
 */
#ifndef RTL8814AU_DEVICE_H
#define RTL8814AU_DEVICE_H


#include <Drivers.h>
#include <USB3.h>
#include <lock.h>

#include "RTL8814AU.h"
#include "RegisterIO.h"
#include "Firmware.h"
#include "EfuseReader.h"
#include "PhyConfig.h"
#include "TxPath.h"
#include "RxPath.h"
#include "WiFiManagement.h"


class RTL8814AUDevice {
public:
								RTL8814AUDevice(usb_device device,
									uint32 slotIndex,
									const char* deviceName);
								~RTL8814AUDevice();

	// Post-construction status check
	status_t					InitCheck() const { return fInitStatus; }

	// Device identity
	const char*					Name() const { return fDeviceName; }
	uint32						SlotIndex() const { return fSlotIndex; }

	// Device state
	bool						IsOpen() const { return fOpenCount > 0; }
	bool						IsRemoved() const { return fRemoved; }
	void						SetRemoved();

	// --------------- Static device_hooks wrappers ---------------
	// These are called by the kernel via the device_hooks struct
	// returned from find_device(). They resolve the device instance
	// from the cookie and dispatch to instance methods.

	static status_t				Open(const char* name, uint32 flags,
									void** cookie);
	static status_t				Close(void* cookie);
	static status_t				Free(void* cookie);
	static status_t				Control(void* cookie, uint32 op,
									void* args, size_t length);
	static status_t				Read(void* cookie, off_t position,
									void* buffer, size_t* numBytes);
	static status_t				Write(void* cookie, off_t position,
									const void* buffer, size_t* numBytes);

private:
	// Hardware initialization — called on first open()
	status_t					_InitHardware();
	status_t					_PowerOnSequence();
	status_t					_InitMAC();
	status_t					_InitPageAllocation();
	status_t					_InitLLTTable();
	status_t					_InitQueuePriority();
	status_t					_EnableDMA();

	// USB endpoint setup — called from constructor
	status_t					_SetupEndpoints();

	// MAC address management
	status_t					_ReadMacAddress();
	status_t					_WriteMacAddress();

	// RX frame callback — called by the RX path for each received frame
	static void					_RxFrameReceived(void* cookie,
									const uint8* frameData,
									uint32 frameLength,
									const RxFrameInfo* info);

	// Parse a beacon or probe response to update the BSS list
	void						_ParseBeaconOrProbe(
									const uint8* frameData,
									uint32 frameLength,
									const RxFrameInfo* rxInfo);

	// Forward parsed BSS info to the WiFi manager
	void						_UpdateBssEntry(const uint8* bssid,
									const char* ssid, uint8 ssidLength,
									uint8 channel, uint16 beaconInterval,
									uint16 capability,
									SecurityType security, int8 rssi,
									const uint8* ieData, uint32 ieLength);

	// Cleanup
	void						_Shutdown();

	// USB device and endpoints
	usb_device					fUSBDevice;
	usb_pipe					fBulkIn;
	usb_pipe					fBulkOut[kBulkOutEndpointCount];
	usb_pipe					fInterruptIn;

	// Device identity and state
	uint32						fSlotIndex;
	char						fDeviceName[64];
	status_t					fInitStatus;
	bool						fRemoved;
	bool						fHardwareInitialized;
	int32						fOpenCount;

	// Synchronization
	mutex						fLock;

	// RX ring buffer — received data frames are queued here by the
	// _RxFrameReceived callback and dequeued by Read().
	static const uint32			kRxRingSlots = 64;
	static const uint32			kRxRingFrameMaxSize = 2400;

	struct RxRingEntry {
		uint8	data[2400];		// Frame payload
		uint32	length;			// Actual frame length (0 = empty)
	};

	RxRingEntry					fRxRing[kRxRingSlots];
	uint32						fRxRingHead;	// Next slot to write
	uint32						fRxRingTail;	// Next slot to read
	sem_id						fRxDataReady;	// Signaled when frame available

	// Link state change notification
	sem_id						fLinkStateSem;	// Provided by network stack

	// MAC address (read from EFUSE during init)
	uint8						fMacAddress[6];

	// Owned subsystem modules — created in constructor, initialized
	// lazily on first open(). Each module handles a specific aspect
	// of the hardware.
	RTL8814AURegisterIO*		fRegisterIO;
	RTL8814AUFirmware*			fFirmware;
	RTL8814AUEfuseReader*		fEfuseReader;
	RTL8814AUPhyConfig*			fPhyConfig;
	RTL8814AUTxPath*			fTxPath;
	RTL8814AURxPath*			fRxPath;
	RTL8814AUWiFiManager*		fWiFiManager;
};


#endif	// RTL8814AU_DEVICE_H
