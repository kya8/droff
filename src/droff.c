#define _XOPEN_SOURCE 700

#include "busy.h"
#include "common.h"
#include "device.h"
#include "power.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(FILE *stream)
{
    fputs("Usage: droff [--dry-run] DEVICE\n"
          "       droff --list\n"
          "Safely stop storage and remove its containing USB device.\n"
          "\n"
          "  -l, --list        List removable USB drives and dry-check status.\n"
          "  -n, --dry-run     Discover devices and check use without changing them.\n"
          "  -h, --help        Show this help.\n", stream);
}

static bool can_pass_dry_check(const struct usb_drive *drive)
{
    bool found_disk = false;
    for (size_t index = 0; index < drive->devices.count; ++index) {
        const struct block_device *device = &drive->devices.devices[index];
        if (!device->whole_disk)
            continue;
        found_disk = true;
        char usb_path[PATH_MAX];
        dev_t disk_number = 0;
        if (find_usb_device(device->device_path, usb_path, &disk_number) < 0)
            return false;
        if (disk_number != device->device_number || strcmp(usb_path, drive->usb_path) != 0) {
            report_error("device identity changed for %s", device->device_path);
            return false;
        }
    }
    return found_disk && check_not_in_use(&drive->devices) == 0;
}

static int list_usb_drives(void)
{
    struct usb_drive_list list = {0};
    int result = 1;

    if (collect_usb_drives(&list) < 0)
        goto out;
    if (list.count == 0) {
        puts("No removable USB drives found.");
        result = 0;
        goto out;
    }
    for (size_t index = 0; index < list.count; ++index) {
        struct usb_drive *drive = &list.drives[index];
        bool can_pass = can_pass_dry_check(drive);
        printf("[%s] %s", can_pass ? "pass" : "blocked", drive->usb_path);
        for (size_t device_index = 0; device_index < drive->devices.count; ++device_index) {
            const struct block_device *device = &drive->devices.devices[device_index];
            if (device->whole_disk)
                printf(" %s", device->device_path);
        }
        putchar('\n');
        if (fflush(stdout) != 0) {
            report_error("cannot write list output: %s", strerror(errno));
            result = 1;
            goto out;
        }
    }
    result = 0;
out:
    if (release_usb_drives(&list) < 0)
        result = 1;
    return result;
}

int main(int argc, char **argv)
{
    const char *device_path = NULL;
    bool dry_run = false;
    bool list_requested = false;
    bool end_options = false;
    for (int index = 1; index < argc; ++index) {
        const char *argument = argv[index];
        if (!end_options && (strcmp(argument, "--help") == 0 || strcmp(argument, "-h") == 0)) {
            print_usage(stdout);
            return 0;
        }
        if (!end_options && (strcmp(argument, "--list") == 0 || strcmp(argument, "-l") == 0)) {
            list_requested = true;
            continue;
        }
        if (!end_options && (strcmp(argument, "--dry-run") == 0 || strcmp(argument, "-n") == 0)) {
            dry_run = true;
            continue;
        }
        if (!end_options && strcmp(argument, "--") == 0) {
            end_options = true;
            continue;
        }
        if ((!end_options && argument[0] == '-') || device_path != NULL) {
            report_error("unexpected argument: %s", argument);
            print_usage(stderr);
            return 2;
        } else {
            device_path = argument;
        }
    }
    if (list_requested) {
        if (device_path != NULL || dry_run) {
            report_error("--list cannot be combined with DEVICE or --dry-run");
            print_usage(stderr);
            return 2;
        }
        return list_usb_drives();
    }
    if (device_path == NULL) {
        print_usage(stderr);
        return 2;
    }

    char usb_path[PATH_MAX];
    dev_t disk_number = 0;
    if (find_usb_device(device_path, usb_path, &disk_number) < 0)
        return 1;
    struct device_list list = {0};
    int result = 1;
    if (collect_devices(usb_path, &list) < 0)
        goto out;
    const struct block_device *target = find_device(&list, disk_number);
    if (target == NULL || !target->whole_disk) {
        report_error("target disk disappeared during discovery");
        goto out;
    }
    printf("USB device: %s\n", usb_path);
    for (size_t index = 0; index < list.count; ++index)
        if (list.devices[index].whole_disk)
            printf("Affected disk: %s\n", list.devices[index].device_path);
    fflush(stdout);
    if (check_not_in_use(&list) < 0)
        goto out;
    if (dry_run) {
        puts("Dry run: checks passed; exclusive access and removal permissions were not tested.");
    } else {
        if (power_off(&list, disk_number, usb_path) < 0)
            goto out;
        puts("USB device removed; physical power switching depends on the hardware.");
    }
    result = 0;
out:
    if (release_devices(&list) < 0)
        result = 1;
    return result;
}
