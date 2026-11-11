#ifndef DROFF_DEVICE_H
#define DROFF_DEVICE_H

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

struct block_device {
    char sysfs_path[PATH_MAX];
    char device_path[PATH_MAX];
    dev_t device_number;
    bool whole_disk;
    int fd;
};

struct device_list {
    struct block_device *devices;
    size_t count;
};

struct usb_drive {
    char usb_path[PATH_MAX];
    struct device_list devices;
};

struct usb_drive_list {
    struct usb_drive *drives;
    size_t count;
};

int find_usb_device(const char *device_path, char usb_path[PATH_MAX],
                    dev_t *disk_number);
int collect_devices(const char *usb_path, struct device_list *list);
int collect_usb_drives(struct usb_drive_list *list);
const struct block_device *find_device(const struct device_list *list,
                                      dev_t device_number);
int open_disks(struct device_list *list);
int release_devices(struct device_list *list);
int release_usb_drives(struct usb_drive_list *list);

#endif
