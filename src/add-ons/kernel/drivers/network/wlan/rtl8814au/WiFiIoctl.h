/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * WiFiIoctl.h — Minimal ABI for the IEEE 802.11 ioctls that userland uses
 * to talk to WLAN device drivers.
 *
 * This header reproduces just enough of the FreeBSD-derived
 * net80211 ioctl interface (struct ieee80211req, ieee80211_scan_req,
 * ieee80211req_scan_result, the IEEE80211_IOC_* command codes) for the
 * RTL8814AU driver to handle SIOCS80211 / SIOCG80211 without pulling in
 * the entire freebsd_wlan compat layer.
 *
 * Layout MUST match exactly what BNetworkDevice / ifconfig and the rest
 * of net80211_ioctl.h define — they share a single ABI.  See
 * src/libs/compat/freebsd_wlan/net80211/ieee80211_ioctl.h for the
 * canonical definitions.
 */
#ifndef RTL8814AU_WIFI_IOCTL_H
#define RTL8814AU_WIFI_IOCTL_H


#include <SupportDefs.h>

// libnetapi (BNetworkDevice / NetworkDevice.cpp) is built with
// src/libs/compat/freebsd_network/ on its include path, so its
// <compat/sys/ioccom.h> resolves to the FreeBSD compat shim where
//   _IOW(g,n,t)  := SIOCEND + n
//   _IOWR(g,n,t) := SIOCEND + n
// not the BSD-style bit-encoded form (IOC_IN | len<<16 | group<<8 | n).
// We must use the same convention here so SIOCS80211/SIOCG80211 match
// the values userland actually puts on the wire.
#include <sys/sockio.h>


// Sizes that match the canonical ABI exactly.
#ifndef IEEE80211_ADDR_LEN
#	define IEEE80211_ADDR_LEN	6
#endif
#ifndef IEEE80211_NWID_LEN
#	define IEEE80211_NWID_LEN	32
#endif
#ifndef IEEE80211_RATE_MAXSIZE
#	define IEEE80211_RATE_MAXSIZE	15
#endif
#ifndef IFNAMSIZ
#	define IFNAMSIZ	32
#endif


// SIOC*80211 carry a struct ieee80211req describing what to get/set.
struct ieee80211req {
	char		i_name[IFNAMSIZ];	// device name (e.g. \"rtl8814au/0\")
	uint16		i_type;				// IEEE80211_IOC_* command code
	int16		i_val;				// scalar value (set/get)
	uint16		i_len;				// length of i_data buffer
	void*		i_data;				// command-specific extra data
};

#define SIOCS80211	(SIOCEND + 234)
#define SIOCG80211	(SIOCEND + 235)


// Command codes (subset — what we actually handle).
#define IEEE80211_IOC_SSID				1
#define IEEE80211_IOC_BSSID				15
#define IEEE80211_IOC_SCAN_RESULTS		76
#define IEEE80211_IOC_STA_INFO			78
#define IEEE80211_IOC_SCAN_REQ			103
#define IEEE80211_IOC_SCAN_CANCEL		104

#define IEEE80211_IOC_WPAKEY			19
#define IEEE80211_IOC_MLME				21

// Haiku-specific extensions (see freebsd_wlan/net80211/ieee80211_ioctl.h).
#define IEEE80211_IOC_HAIKU_COMPAT_WLAN_UP		0x6000
#define IEEE80211_IOC_HAIKU_COMPAT_WLAN_DOWN	0x6001
#define IEEE80211_IOC_HAIKU_JOIN				0x6002


// Scan request — passed via i_data when i_type == IEEE80211_IOC_SCAN_REQ.
struct ieee80211_scan_req {
	int		sr_flags;
	uint32	sr_duration;
	uint32	sr_mindwell;
	uint32	sr_maxdwell;
	int		sr_nssid;
#define IEEE80211_IOC_SCAN_MAX_SSID	3
	struct {
		int		len;
		uint8	ssid[IEEE80211_NWID_LEN];
	} sr_ssid[IEEE80211_IOC_SCAN_MAX_SSID];
};

// sr_flags values
#define IEEE80211_IOC_SCAN_NOPICK		0x00001
#define IEEE80211_IOC_SCAN_ACTIVE		0x00002
#define IEEE80211_IOC_SCAN_PICK1ST		0x00004
#define IEEE80211_IOC_SCAN_BGSCAN		0x00008
#define IEEE80211_IOC_SCAN_ONCE			0x00010
#define IEEE80211_IOC_SCAN_NOBCAST		0x00020
#define IEEE80211_IOC_SCAN_NOJOIN		0x00040
#define IEEE80211_IOC_SCAN_FLUSH		0x10000
#define IEEE80211_IOC_SCAN_CHECK		0x20000


// One scan result record — variable length (isr_len bytes total).
// Layout: this fixed header, then variable SSID, then variable IE data.
// All records are rounded to a multiple of 4 bytes.
struct ieee80211req_scan_result {
	uint16	isr_len;					// total record length (4-aligned)
	uint16	isr_ie_off;					// offset to SSID + IE data
	uint16	isr_ie_len;					// IE block length
	uint16	isr_freq;					// channel center freq (MHz)
	uint16	isr_flags;					// channel flags
	int8	isr_noise;
	int8	isr_rssi;
	uint16	isr_intval;					// beacon interval (TUs)
	uint8	isr_capinfo;				// capability info
	uint8	isr_erp;					// ERP element
	uint8	isr_bssid[IEEE80211_ADDR_LEN];
	uint8	isr_nrates;
	uint8	isr_rates[IEEE80211_RATE_MAXSIZE];
	uint8	isr_ssid_len;
	uint8	isr_meshid_len;
	// followed by ssid bytes, mesh id bytes, IE bytes
};


// Channel flag bits used by isr_flags (subset).
#define IEEE80211_CHAN_2GHZ		0x00080
#define IEEE80211_CHAN_5GHZ		0x00100
#define IEEE80211_CHAN_CCK		0x00020
#define IEEE80211_CHAN_OFDM		0x00040
#define IEEE80211_CHAN_DYN		0x00400


// Stub for STA_INFO get — only used to return 'no stations' currently.
struct ieee80211req_sta_info {
	uint16 isi_len;
	uint8 isi_pad[126];
};

#endif	// RTL8814AU_WIFI_IOCTL_H
