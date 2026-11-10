# droff

A CLI tool for Linux to perform the USB storage removal sequence used by
`udisksctl power-off -b DEVICE`. Requires only the Linux kernel, libc,
and `/proc`, `/sys` access.

## Build

Requires a C11 compiler, GNU make, libc development headers, and Linux UAPI
headers (including `linux/bsg.h` and `scsi/sg.h`).

```sh
make
```

The executable is `build/droff`. Compiler and linker options can be overridden
on the make command line, for example `make CC=clang CFLAGS='-O2 -g -Werror'`.

## Usage

Unmount the filesystems, disable swap, and close encryption or other block
device mappings using the storage before running:

```sh
build/droff --dry-run /dev/sdb
sudo build/droff /dev/sdb
```

The block device path is the required positional argument. Partition paths and
device symlinks are accepted; the containing whole disk is selected. Only USB
storage is supported.
The entire USB device is removed, including every interface and logical unit.
The program lists all affected disks before proceeding.

There is no authentication prompt or privilege escalation. Device access,
SCSI command permissions, and sysfs write access are enforced by the kernel;
access failures are reported. Typically the removal command requires root.

`--dry-run` only discovers devices and checks for use. It does not open disk
nodes, flush caches, send SCSI commands, or open/write the sysfs removal file.
It cannot confirm exclusive access or permissions for the real operation.

## Behavior and limits

1. Resolve the block device through `/sys/dev/block/MAJOR:MINOR`, find its
   enclosing USB device, and enumerate all descendant disks and partitions.
2. Reject mounted devices, active swap, and any sysfs block-device holders
   (including encryption mappings, LVM, and MD RAID).
3. Open every affected disk with `O_RDONLY | O_NONBLOCK | O_EXCL`, verify its
   device number and sysfs path, and repeat the busy checks. Hold these disk
   descriptors until the operation finishes.
4. Open the USB device's `remove` attribute for writing and `fsync()` each disk.
5. Send SCSI SYNCHRONIZE CACHE (10) and START STOP UNIT (`START=0`, `LOEJ=0`)
   to the selected disk, using SG_IO v4 with v3 fallback on `EINVAL` and a
   30-second timeout per command. As in UDisks, SCSI command failures produce
   warnings and removal continues. Permission failures (including SG_IO
   `EACCES`/`EPERM`), open, sync, and removal failures are fatal.
6. Write `1` to the enclosing USB device's sysfs `remove` attribute.

Busy checks fail if required system information cannot be inspected. Mount
checks use the current mount namespace; run from the host's namespace with
its normal `/dev`, `/proc`, and `/sys` views. Exclusive opens add kernel checks
for block-device claims, including use outside the current mount namespace,
but cannot exclude every concurrent raw-device user or hardware hotplug race.
The program does not scan process file descriptors, unmount anything, or tear
down mappings.

Removal logically disconnects the USB device. Disk spin-down and actual USB
power switching depend on the device, bridge, and hub. Successful removal does
not guarantee that the hardware is electrically unpowered, especially if it
uses an external supply. Reconnecting normally requires unplugging and
replugging the device.
