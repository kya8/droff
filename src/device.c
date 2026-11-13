#define _XOPEN_SOURCE 700

#include "device.h"
#include "utils.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

static int read_property(const char *directory, const char *key,
                         char value[PATH_MAX])
{
    char path[PATH_MAX];
    if (format_path(path, "%s/uevent", directory) < 0)
        return -1;
    FILE *stream = fopen(path, "r");
    if (stream == NULL) {
        if (errno == ENOENT)
            return 0;
        return report_error("cannot read %s: %s", path, strerror(errno));
    }

    char *line = NULL;
    size_t capacity = 0;
    size_t key_length = strlen(key);
    int result = 0;
    while (getline(&line, &capacity, stream) >= 0) {
        if (strncmp(line, key, key_length) != 0 || line[key_length] != '=')
            continue;
        char *contents = line + key_length + 1;
        contents[strcspn(contents, "\n")] = '\0';
        result = format_path(value, "%s", contents) < 0 ? -1 : 1;
        break;
    }
    if (ferror(stream) || (result == 0 && !feof(stream)))
        result = report_error("cannot read %s: %s", path, strerror(errno));
    free(line);
    if (fclose(stream) != 0)
        result = report_error("cannot close %s: %s", path, strerror(errno));
    return result;
}

static bool parent_directory(char *path)
{
    char *separator = strrchr(path, '/');
    if (separator == NULL || separator == path)
        return false;
    *separator = '\0';
    return true;
}

static bool is_descendant(const char *path, const char *ancestor)
{
    size_t length = strlen(ancestor);
    return strncmp(path, ancestor, length) == 0 && path[length] == '/';
}

static int resolve_device(dev_t device_number, char path[PATH_MAX])
{
    char link_path[PATH_MAX];
    if (format_path(link_path, "/sys/dev/block/%u:%u",
                    major(device_number), minor(device_number)) < 0)
        return -1;
    if (realpath(link_path, path) == NULL)
        return report_error("cannot resolve %s: %s", link_path, strerror(errno));
    return 0;
}

static int find_usb_ancestor(char path[PATH_MAX], char usb_path[PATH_MAX])
{
    char device_type[PATH_MAX];

    while (parent_directory(path)) {
        int result = read_property(path, "DEVTYPE", device_type);
        if (result < 0)
            return -1;
        if (result != 1 || !streq(device_type, "usb_device"))
            continue;

        char subsystem_path[PATH_MAX];
        char subsystem[PATH_MAX];
        if (format_path(subsystem_path, "%s/subsystem", path) < 0)
            return -1;
        if (realpath(subsystem_path, subsystem) == NULL)
            return report_error("cannot resolve %s: %s", subsystem_path, strerror(errno));
        if (streq(subsystem, "/sys/bus/usb")) {
            if (format_path(usb_path, "%s", path) < 0)
                return -1;
            return 1;
        }
    }
    return 0;
}

int find_usb_device(const char *device_path, char usb_path[PATH_MAX],
                    dev_t *disk_number)
{
    struct stat status;
    if (stat(device_path, &status) != 0)
        return report_error("cannot stat %s: %s", device_path, strerror(errno));
    if (!S_ISBLK(status.st_mode))
        return report_error("%s is not a block device", device_path);

    char path[PATH_MAX];
    char device_type[PATH_MAX];
    if (resolve_device(status.st_rdev, path) < 0)
        return -1;
    if (read_property(path, "DEVTYPE", device_type) != 1)
        return report_error("cannot determine block device type for %s", device_path);
    if (streq(device_type, "partition")) {
        if (!parent_directory(path))
            return report_error("cannot find disk for %s", device_path);
        if (read_property(path, "DEVTYPE", device_type) != 1)
            return report_error("cannot determine parent disk type for %s", device_path);
    }
    if (!streq(device_type, "disk"))
        return report_error("%s does not identify a disk or partition", device_path);

    char number_path[PATH_MAX];
    if (format_path(number_path, "%s/dev", path) < 0)
        return -1;
    FILE *stream = fopen(number_path, "r");
    if (stream == NULL)
        return report_error("cannot read %s: %s", number_path, strerror(errno));
    unsigned int major_number, minor_number;
    int fields = fscanf(stream, "%u:%u", &major_number, &minor_number);
    int close_result = fclose(stream);
    if (fields != 2 || close_result != 0)
        return report_error("cannot read device number from %s", number_path);
    *disk_number = makedev(major_number, minor_number);

    int ancestor_result = find_usb_ancestor(path, usb_path);
    if (ancestor_result < 0)
        return -1;
    if (ancestor_result == 0)
        return report_error("%s is not a disk attached through USB", device_path);
    return 0;
}

int collect_devices(const char *usb_path, struct device_list *list)
{
    DIR *directory = opendir("/sys/class/block");
    if (directory == NULL)
        return report_error("cannot open /sys/class/block: %s", strerror(errno));

    int result = -1;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(directory);
        if (entry == NULL) {
            if (errno != 0)
                report_error("cannot read /sys/class/block: %s", strerror(errno));
            else
                result = 0;
            break;
        }
        if (entry->d_name[0] == '.')
            continue;
        char link_path[PATH_MAX];
        struct block_device device = {.fd = -1};
        if (format_path(link_path, "/sys/class/block/%s", entry->d_name) < 0)
            break;
        if (realpath(link_path, device.sysfs_path) == NULL) {
            report_error("cannot resolve %s: %s", link_path, strerror(errno));
            break;
        }
        if (!is_descendant(device.sysfs_path, usb_path))
            continue;

        char value[PATH_MAX];
        if (read_property(device.sysfs_path, "DEVTYPE", value) != 1) {
            report_error("cannot read device type for %s", link_path);
            break;
        }
        device.whole_disk = streq(value, "disk");
        if (!device.whole_disk && !streq(value, "partition")) {
            report_error("unexpected block device type: %s", value);
            break;
        }
        if (read_property(device.sysfs_path, "MAJOR", value) != 1) {
            report_error("cannot read major device number for %s", link_path);
            break;
        }
        unsigned int major_number, minor_number;
        if (sscanf(value, "%u", &major_number) != 1) {
            report_error("invalid major device number for %s", link_path);
            break;
        }
        if (read_property(device.sysfs_path, "MINOR", value) != 1) {
            report_error("cannot read minor device number for %s", link_path);
            break;
        }
        if (sscanf(value, "%u", &minor_number) != 1) {
            report_error("invalid minor device number for %s", link_path);
            break;
        }
        device.device_number = makedev(major_number, minor_number);
        if (read_property(device.sysfs_path, "DEVNAME", value) != 1) {
            report_error("cannot read device name for %s", link_path);
            break;
        }
        if (format_path(device.device_path, "/dev/%s", value) < 0)
            break;
        if (list->count >= SIZE_MAX / sizeof(*list->devices)) {
            report_error("too many block devices");
            break;
        }
        struct block_device *resized = realloc(list->devices,
                                              (list->count + 1) * sizeof(*resized));
        if (resized == NULL) {
            report_error("cannot allocate device list: %s", strerror(errno));
            break;
        }
        list->devices = resized;
        list->devices[list->count++] = device;
    }
    if (closedir(directory) != 0)
        result = report_error("cannot close /sys/class/block: %s", strerror(errno));
    return result;
}

static int add_usb_drive(struct usb_drive_list *list, const char *usb_path)
{
    for (size_t index = 0; index < list->count; ++index)
        if (streq(list->drives[index].usb_path, usb_path))
            return 0;

    if (list->count >= SIZE_MAX / sizeof(*list->drives))
        return report_error("too many USB drives");
    struct usb_drive *resized = realloc(list->drives,
                                        (list->count + 1) * sizeof(*resized));
    if (resized == NULL)
        return report_error("cannot allocate USB drive list: %s", strerror(errno));
    list->drives = resized;
    struct usb_drive *drive = &list->drives[list->count];
    memset(drive, 0, sizeof(*drive));
    if (format_path(drive->usb_path, "%s", usb_path) < 0)
        return -1;
    ++list->count;
    return 0;
}

int collect_usb_drives(struct usb_drive_list *list)
{
    DIR *directory = opendir("/sys/class/block");
    if (directory == NULL)
        return report_error("cannot open /sys/class/block: %s", strerror(errno));

    int result = 0;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(directory);
        if (entry == NULL) {
            if (errno != 0)
                result = report_error("cannot read /sys/class/block: %s", strerror(errno));
            break;
        }
        if (entry->d_name[0] == '.')
            continue;

        char link_path[PATH_MAX];
        char sysfs_path[PATH_MAX];
        char device_type[PATH_MAX];
        char usb_path[PATH_MAX];
        if (format_path(link_path, "/sys/class/block/%s", entry->d_name) < 0) {
            result = -1;
            break;
        }
        if (realpath(link_path, sysfs_path) == NULL) {
            result = report_error("cannot resolve %s: %s", link_path, strerror(errno));
            break;
        }
        if (read_property(sysfs_path, "DEVTYPE", device_type) != 1) {
            result = report_error("cannot read device type for %s", link_path);
            break;
        }
        if (!streq(device_type, "disk"))
            continue;
        int ancestor_result = find_usb_ancestor(sysfs_path, usb_path);
        if (ancestor_result < 0) {
            result = -1;
            break;
        }
        if (ancestor_result > 0 && add_usb_drive(list, usb_path) < 0) {
            result = -1;
            break;
        }
    }
    if (closedir(directory) != 0)
        result = report_error("cannot close /sys/class/block: %s", strerror(errno));

    if (result == 0) {
        for (size_t index = 0; index < list->count; ++index) {
            if (collect_devices(list->drives[index].usb_path,
                                &list->drives[index].devices) < 0) {
                result = -1;
                break;
            }
        }
    }
    return result;
}

const struct block_device *find_device(const struct device_list *list,
                                      dev_t device_number)
{
    for (size_t index = 0; index < list->count; ++index)
        if (list->devices[index].device_number == device_number)
            return &list->devices[index];
    return NULL;
}

int open_disks(struct device_list *list)
{
    for (size_t index = 0; index < list->count; ++index) {
        struct block_device *device = &list->devices[index];
        if (!device->whole_disk)
            continue;
        device->fd = open(device->device_path, O_RDONLY | O_NONBLOCK | O_EXCL | O_CLOEXEC);
        if (device->fd < 0)
            return report_error("cannot exclusively open %s: %s", device->device_path, strerror(errno));
        struct stat status;
        if (fstat(device->fd, &status) != 0)
            return report_error("cannot inspect %s: %s", device->device_path, strerror(errno));
        if (!S_ISBLK(status.st_mode) || status.st_rdev != device->device_number)
            return report_error("device identity changed for %s", device->device_path);
        char current_path[PATH_MAX];
        if (resolve_device(status.st_rdev, current_path) < 0)
            return -1;
        if (!streq(current_path, device->sysfs_path))
            return report_error("sysfs identity changed for %s", device->device_path);
    }
    return 0;
}

int release_devices(struct device_list *list)
{
    int result = 0;
    for (size_t index = 0; index < list->count; ++index) {
        if (list->devices[index].fd >= 0 && close(list->devices[index].fd) != 0) {
            report_error("cannot close %s: %s", list->devices[index].device_path, strerror(errno));
            result = -1;
        }
    }
    free(list->devices);
    *list = (struct device_list){0};
    return result;
}

int release_usb_drives(struct usb_drive_list *list)
{
    int result = 0;
    for (size_t index = 0; index < list->count; ++index)
        if (release_devices(&list->drives[index].devices) < 0)
            result = -1;
    free(list->drives);
    *list = (struct usb_drive_list){0};
    return result;
}
