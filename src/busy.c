#define _XOPEN_SOURCE 700

#include "busy.h"
#include "common.h"
#include "device.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

static int check_holders(const struct block_device *device)
{
    char path[PATH_MAX];
    if (format_path(path, "%s/holders", device->sysfs_path) < 0)
        return -1;
    DIR *directory = opendir(path);
    if (directory == NULL)
        return report_error("cannot open %s: %s", path, strerror(errno));
    int result = 0;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(directory);
        if (entry == NULL) {
            if (errno != 0)
                result = report_error("cannot read %s: %s", path, strerror(errno));
            break;
        }
        if (entry->d_name[0] != '.') {
            result = report_error("%s is held by %s", device->device_path, entry->d_name);
            break;
        }
    }
    if (closedir(directory) != 0)
        result = report_error("cannot close %s: %s", path, strerror(errno));
    return result;
}

static int check_mounts(const struct device_list *list)
{
    FILE *stream = fopen("/proc/self/mountinfo", "r");
    if (stream == NULL)
        return report_error("cannot read /proc/self/mountinfo: %s", strerror(errno));
    char *line = NULL;
    size_t capacity = 0;
    int result = 0;
    while (getline(&line, &capacity, stream) >= 0) {
        unsigned int major_number, minor_number;
        if (sscanf(line, "%*u %*u %u:%u", &major_number, &minor_number) != 2) {
            result = report_error("invalid entry in /proc/self/mountinfo");
            break;
        }
        const struct block_device *device = find_device(list, makedev(major_number, minor_number));
        if (device != NULL) {
            result = report_error("%s is mounted", device->device_path);
            break;
        }
    }
    if (ferror(stream) || (result == 0 && !feof(stream)))
        result = report_error("cannot read /proc/self/mountinfo: %s", strerror(errno));
    free(line);
    if (fclose(stream) != 0)
        result = report_error("cannot close /proc/self/mountinfo: %s", strerror(errno));
    return result;
}

static void unescape_proc_path(char *path)
{
    char *source = path;
    char *destination = path;
    while (*source != '\0') {
        if (source[0] == '\\' && source[1] >= '0' && source[1] <= '3' &&
            source[2] >= '0' && source[2] <= '7' &&
            source[3] >= '0' && source[3] <= '7') {
            *destination++ = (char)((source[1] - '0') * 64 +
                                    (source[2] - '0') * 8 + source[3] - '0');
            source += 4;
        } else {
            *destination++ = *source++;
        }
    }
    *destination = '\0';
}

static int check_swap(const struct device_list *list)
{
    FILE *stream = fopen("/proc/swaps", "r");
    if (stream == NULL)
        return report_error("cannot read /proc/swaps: %s", strerror(errno));
    char *line = NULL;
    size_t capacity = 0;
    int result = 0;
    if (getline(&line, &capacity, stream) < 0) {
        report_error("cannot read /proc/swaps header");
        result = -1;
    }
    while (result == 0 && getline(&line, &capacity, stream) >= 0) {
        line[strcspn(line, " \t\n")] = '\0';
        unescape_proc_path(line);
        struct stat status;
        if (stat(line, &status) != 0) {
            result = report_error("cannot inspect swap %s: %s", line, strerror(errno));
            break;
        }
        dev_t number = S_ISBLK(status.st_mode) ? status.st_rdev : status.st_dev;
        const struct block_device *device = find_device(list, number);
        if (device != NULL)
            result = report_error("%s backs active swap %s", device->device_path, line);
    }
    if (ferror(stream) || (result == 0 && !feof(stream)))
        result = report_error("cannot read /proc/swaps: %s", strerror(errno));
    free(line);
    if (fclose(stream) != 0)
        result = report_error("cannot close /proc/swaps: %s", strerror(errno));
    return result;
}

int check_not_in_use(const struct device_list *list)
{
    for (size_t index = 0; index < list->count; ++index)
        if (check_holders(&list->devices[index]) < 0)
            return -1;
    if (check_mounts(list) < 0 || check_swap(list) < 0)
        return -1;
    return 0;
}
