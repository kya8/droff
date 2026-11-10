#ifndef DROFF_BUSY_H
#define DROFF_BUSY_H

struct device_list;

int check_not_in_use(const struct device_list *list);

#endif
