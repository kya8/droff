# droff

A command-line utility for Linux to safetly remove USB drives.
It performs the USB storage removal sequence used by
`udisksctl power-off -b DEVICE`.

Requires only the Linux kernel and `/proc`, `/sys` access to run.

## Rationale
I never found an exact alternative to `udisksctl power-off -b`, which is really
handy for ___safely___ removing a USB drive, especially spinning portable HDDs
and portable SSDs: simply unmounting filesystems and/or using `eject(1)`
usually do not power them off cleanly. No other utils did the trick like udisks,
so I wrote my own.

## Download
Grab statically-linked binaries from Github actions:
[![Builds](https://github.com/kya8/droff/actions/workflows/build.yml/badge.svg)](https://github.com/kya8/droff/actions/workflows/build.yml)

## Build

Requires a C11 compiler, GNU make, libc development headers, and Linux UAPI
headers (including `linux/bsg.h` and `scsi/sg.h`).

Build with make:

```sh
make
```
Or, build the static binary:
```sh
make static
```

The executable is `build/droff`. Compiler and linker options can be overridden
on the make command line, for example `make CC=clang CFLAGS='-O2 -g -Werror'`.

## Usage

Unmount filesystems on the target drive before running:

```sh
droff --dry-run /dev/sdx  # Optional
sudo droff /dev/sdx  # Or whatever privilege escalator you use...
```

The block device path is the required positional argument. Partition paths and
device symlinks are accepted; the containing whole disk is selected. Only USB
storage is supported.
The entire USB device is removed, including every interface and logical unit.
The program lists all affected disks before proceeding.

There is no authentication prompt or privilege escalation, like those in
`udisksctl`. Access permissions are enforced by the kernel.
To perform the removal typically requires root.

`--dry-run` only discovers devices and checks for use. It does not open disk
nodes, flush caches, send SCSI commands, or open/write the sysfs removal file.
It cannot confirm exclusive access or permissions for the real operation.

## Details and limits

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
power switching depend on the device. Successful removal does
not guarantee that the hardware is electrically unpowered. E.g., some portable
HDDs might take a few seconds after successful removal, to eventually spin-down
and power off (while flashing their LED indicator).

Reconnecting normally requires unplugging and replugging the device.
