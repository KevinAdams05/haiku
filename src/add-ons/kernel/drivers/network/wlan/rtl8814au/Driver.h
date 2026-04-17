/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Driver.h — Kernel module entry points and USB notification hooks for the
 * RTL8814AU native USB WiFi driver.
 *
 * This file declares the driver-level infrastructure: USB device matching,
 * device list management, and the standard Haiku kernel driver exports
 * (init_hardware, init_driver, uninit_driver, publish_devices, find_device).
 */
#ifndef RTL8814AU_DRIVER_H
#define RTL8814AU_DRIVER_H


#include <Drivers.h>
#include <USB3.h>

#include "RTL8814AU.h"


class RTL8814AUDevice;


// The USB bus manager module — acquired in init_driver(), released in
// uninit_driver(). All USB operations go through this pointer.
extern usb_module_info* gUSBModule;

// Mutex protecting the device list. Held when adding/removing devices
// and when iterating publish_devices().
extern mutex gDeviceListLock;

// Active device instances. Indexed by device number (0..kMaxDeviceCount-1).
// NULL entries are unused slots.
extern RTL8814AUDevice* gDeviceList[kMaxDeviceCount];

// Number of currently attached devices.
extern uint32 gDeviceCount;


// ---------------------------------------------------------------------------
// USB notification callbacks
//
// These are called by the USB bus manager when a device matching our
// usb_support_descriptor list is plugged in or removed.
// ---------------------------------------------------------------------------

status_t	DeviceAdded(usb_device device, void** cookie);
status_t	DeviceRemoved(void* cookie);


// ---------------------------------------------------------------------------
// Standard Haiku kernel driver exports
// ---------------------------------------------------------------------------

extern "C" {

status_t		init_hardware();
status_t		init_driver();
void			uninit_driver();
const char**	publish_devices();
device_hooks*	find_device(const char* name);

}


#endif	// RTL8814AU_DRIVER_H
