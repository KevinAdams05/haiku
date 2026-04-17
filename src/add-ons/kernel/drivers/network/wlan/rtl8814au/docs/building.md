# Building and Testing the RTL8814AU Driver

## Prerequisites

### Build Environment

The driver builds as part of the Haiku source tree. You need:

1. **Haiku source tree** (already at `c:\Code\Haiku\haiku\`)
2. **Cross-compilation toolchain** — built via `./configure` on Linux, or
   native compilation on Haiku itself
3. **Jam** build tool (part of Haiku's build system)

### Test Environment

1. **QEMU VM** with a Haiku installation (disk image at `C:\VMs\QEMU\haiku.img`)
2. **A physical RTL8814AU USB adapter** (see supported devices below)
3. **USB passthrough** from host to QEMU, or test on bare metal Haiku

### Supported USB Adapters

| Vendor:Product | Name |
|---------------|------|
| 0b05:1817 | ASUS USB-AC68 |
| 7392:a833 | Edimax AC1750 |
| 0b05:1852 | ASUS USB-AC68 (rev 2) |
| 0846:9054 | Netgear A7000 |
| 2001:331a | D-Link DWA-192 |
| 2357:0106 | TP-Link Archer T9UH |
| 20f4:809a | TRENDnet TEW-809UB |
| 056e:400b | Elecom WDB-867DU3S |
| 056e:400d | Elecom WDC-867DU3S |

---

## Building

### Option 1: Build Only the Driver (Fastest)

From the Haiku build directory (after running `./configure`):

```bash
jam -q '<build>rtl8814au'
```

Or target just the wlan drivers:

```bash
jam -q src/add-ons/kernel/drivers/network/wlan/rtl8814au
```

### Option 2: Full Haiku Image Build

Build the complete Haiku image with the driver included:

```bash
jam -q @nightly-anyboot
```

### Option 3: Build on Haiku Itself

On a running Haiku system with the development tools installed:

```bash
cd /boot/home/haiku
jam -q rtl8814au
```

---

## Firmware

The driver expects the firmware file at:

```
/boot/system/data/firmware/rtl8814aufw.bin
```

This file must be extracted from the Linux reference driver package
(`ulli-kroll/rtl8814au`) or the Realtek vendor package. The firmware
is not included in the Haiku source tree due to licensing.

To install the firmware on a running Haiku system:

```bash
cp rtl8814aufw.bin /boot/system/data/firmware/
```

---

## Installing the Driver

### Method 1: Copy to Running System

After building, copy the compiled driver to the Haiku VM:

```bash
# On the host, mount the Haiku disk image or use network transfer
# The driver binary goes to:
/boot/system/add-ons/kernel/drivers/bin/rtl8814au

# Create a symlink for device discovery:
ln -s ../../bin/rtl8814au /boot/system/add-ons/kernel/drivers/dev/net/rtl8814au
```

### Method 2: Include in Image Build

The driver is automatically included when building a full Haiku image
since it's registered in the parent `wlan/Jamfile`.

---

## Testing

### Step 1: Verify Driver Load

After installing the driver and plugging in the USB adapter:

```bash
# Check if the driver loaded
ls /dev/net/rtl8814au/

# Check kernel log for initialization messages
syslog | grep rtl8814au
```

Expected log output on successful init:

```
rtl8814au: device added: ASUS USB-AC68
rtl8814au: endpoint setup: 3 bulk OUT, 1 bulk IN, 1 interrupt IN
rtl8814au: device opened (open count: 1)
rtl8814au: initializing hardware for ASUS USB-AC68
rtl8814au: starting power-on sequence
rtl8814au: power-on sequence complete
rtl8814au: reading EFUSE map (1024 bytes)
rtl8814au: MAC address: xx:xx:xx:xx:xx:xx
rtl8814au: loading firmware from /boot/system/data/firmware/rtl8814aufw.bin
rtl8814au: firmware loaded: DMEM xxxx bytes + IRAM xxxx bytes
rtl8814au: initializing MAC
rtl8814au: initializing PHY (4-path RF)
rtl8814au: loading BB register tables (xxx entries)
rtl8814au: loading AGC tables for 2.4 GHz (256 entries)
rtl8814au: configuring RF transceivers (4 paths, xx common entries each)
rtl8814au: initializing RF path A
rtl8814au: initializing RF path B
rtl8814au: initializing RF path C
rtl8814au: initializing RF path D
rtl8814au: running IQ calibration
rtl8814au: configuring TX power from EFUSE
rtl8814au: hardware initialization complete
```

### Step 2: Check Network Interface

```bash
# List network interfaces
ifconfig

# The rtl8814au adapter should appear as an interface
```

### Step 3: Test with QEMU + USB Passthrough

To pass a physical USB adapter to the QEMU VM, add to the QEMU
command line:

```
-device usb-host,vendorid=0x0b05,productid=0x1817
```

Or for a specific bus/device:

```
-device usb-host,hostbus=1,hostaddr=5
```

The `run-haiku.ps1` script already includes a QEMU xHCI controller
(`-device qemu-xhci`), so USB 3.0 passthrough should work.

### Step 4: Debugging

If the driver fails to load or initialize:

1. **Check syslog** for error messages:
   ```bash
   syslog | grep rtl8814au
   ```

2. **Check USB devices** are detected:
   ```bash
   listusb
   ```

3. **Enable kernel debugging** by booting Haiku with serial console:
   ```
   -serial stdio
   ```
   in the QEMU command line, then check serial output for early driver
   messages.

4. **Common failure points**:
   - Firmware file missing or wrong format
   - USB endpoints not matching expected configuration
   - Power-on sequence timeout (chip not responding)
   - EFUSE read failure (invalid MAC address)

---

## Development Cycle

For rapid iteration:

1. Edit source on Windows (in `c:\Code\Haiku\haiku\`)
2. Cross-compile on Linux build server
3. Copy the built `rtl8814au` binary to the Haiku VM
4. Reload: either reboot the VM, or from Haiku Terminal:
   ```bash
   # Unload the old driver (if loaded)
   # Copy new binary
   # Replug the USB device to trigger re-detection
   ```

---

## Known Limitations (Initial Build)

- **Station mode only** — no AP, P2P, or monitor mode
- **No WPA supplicant integration yet** — scan works, association is
  available via ioctl but not yet exposed to the network preferences UI
- **Fixed data rate** — initial TX uses OFDM 24 Mbps; firmware rate
  adaptation will adjust over time
- **No power management** — the adapter stays fully powered
- **No USB 3.0 optimization** — bulk transfers use conservative sizes
