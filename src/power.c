#define _XOPEN_SOURCE 700

#include "power.h"
#include "busy.h"
#include "common.h"
#include "device.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/bsg.h>
#include <scsi/sg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

enum scsi_result {
    SCSI_OK,
    SCSI_FAILED,
    SCSI_ACCESS_DENIED
};

static enum scsi_result scsi_ioctl_error(const char *version)
{
    int error_number = errno;
    report_error("SG_IO %s: %s", version, strerror(error_number));
    return error_number == EACCES || error_number == EPERM ? SCSI_ACCESS_DENIED : SCSI_FAILED;
}

static enum scsi_result send_scsi_command(int fd, unsigned char *command, unsigned char length)
{
    unsigned char sense[32] = {0};
    struct sg_io_v4 request = {0};
    request.guard = 'Q';
    request.protocol = BSG_PROTOCOL_SCSI;
    request.subprotocol = BSG_SUB_PROTOCOL_SCSI_CMD;
    request.request_len = length;
    request.request = (uintptr_t)command;
    request.max_response_len = sizeof(sense);
    request.response = (uintptr_t)sense;
    request.timeout = 30000;
    if (ioctl(fd, SG_IO, &request) == 0) {
        if (request.device_status == 0 && request.transport_status == 0 && request.driver_status == 0)
            return SCSI_OK;
        report_error("SCSI status: device=%u transport=%u driver=%u",
                     request.device_status, request.transport_status, request.driver_status);
        return SCSI_FAILED;
    }
    if (errno != EINVAL)
        return scsi_ioctl_error("v4");

    struct sg_io_hdr fallback = {0};
    fallback.interface_id = 'S';
    fallback.cmdp = command;
    fallback.cmd_len = length;
    fallback.dxfer_direction = SG_DXFER_NONE;
    fallback.sbp = sense;
    fallback.mx_sb_len = sizeof(sense);
    fallback.timeout = 30000;
    if (ioctl(fd, SG_IO, &fallback) != 0)
        return scsi_ioctl_error("v3");
    if (fallback.status != 0 || fallback.host_status != 0 || fallback.driver_status != 0) {
        report_error("SCSI status: device=%u host=%u driver=%u",
                     fallback.status, fallback.host_status, fallback.driver_status);
        return SCSI_FAILED;
    }
    return SCSI_OK;
}

int power_off(struct device_list *list, dev_t disk_number, const char *usb_path)
{
    const struct block_device *target = find_device(list, disk_number);
    if (target == NULL || !target->whole_disk)
        return report_error("target disk is missing from the device list");
    if (open_disks(list) < 0 || check_not_in_use(list) < 0)
        return -1;
    char remove_path[PATH_MAX];
    if (format_path(remove_path, "%s/remove", usb_path) < 0)
        return -1;
    int remove_fd = open(remove_path, O_WRONLY | O_CLOEXEC);
    if (remove_fd < 0)
        return report_error("cannot open %s: %s", remove_path, strerror(errno));

    int result = -1;
    for (size_t index = 0; index < list->count; ++index) {
        const struct block_device *device = &list->devices[index];
        if (device->whole_disk && fsync(device->fd) != 0) {
            report_error("cannot sync %s: %s", device->device_path, strerror(errno));
            goto out;
        }
    }
    unsigned char synchronize_cache[10] = {0x35};
    unsigned char stop_unit[6] = {0x1b};
    enum scsi_result command_result = send_scsi_command(target->fd, synchronize_cache, sizeof(synchronize_cache));
    if (command_result == SCSI_ACCESS_DENIED)
        goto out;
    if (command_result == SCSI_FAILED)
        fprintf(stderr, "droff: warning: ignoring SYNCHRONIZE CACHE failure on %s\n", target->device_path);
    command_result = send_scsi_command(target->fd, stop_unit, sizeof(stop_unit));
    if (command_result == SCSI_ACCESS_DENIED)
        goto out;
    if (command_result == SCSI_FAILED)
        fprintf(stderr, "droff: warning: ignoring START STOP UNIT failure on %s\n", target->device_path);

    ssize_t written;
    do {
        written = write(remove_fd, "1", 1);
    } while (written < 0 && errno == EINTR);
    if (written != 1) {
        report_error("cannot remove %s: %s", usb_path, written < 0 ? strerror(errno) : "short write");
        goto out;
    }
    result = 0;
out:
    if (close(remove_fd) != 0)
        result = report_error("cannot close %s: %s", remove_path, strerror(errno));
    return result;
}
