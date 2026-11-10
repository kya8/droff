#ifndef DROFF_POWER_H
#define DROFF_POWER_H

#include <sys/types.h>

struct device_list;

int power_off(struct device_list *list, dev_t disk_number, const char *usb_path);

#endif
