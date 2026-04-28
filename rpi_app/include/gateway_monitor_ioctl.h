#ifndef GATEWAY_MONITOR_IOCTL_H
#define GATEWAY_MONITOR_IOCTL_H

#ifdef __KERNEL__
#include <linux/ioctl.h>
#include <linux/types.h>
#else
#include <sys/ioctl.h>
#include <stdint.h>
#endif

#define GATEWAY_MONITOR_IOC_MAGIC 'G'

#define GATEWAY_MONITOR_IOC_SET_PEAK_THR _IOW(GATEWAY_MONITOR_IOC_MAGIC, 1, int32_t)
#define GATEWAY_MONITOR_IOC_SET_RMS_THR  _IOW(GATEWAY_MONITOR_IOC_MAGIC, 2, int32_t)
#define GATEWAY_MONITOR_IOC_SET_GYRO_THR _IOW(GATEWAY_MONITOR_IOC_MAGIC, 3, int32_t)

#define GATEWAY_MONITOR_DEVICE "/dev/gateway_monitor"

#endif
