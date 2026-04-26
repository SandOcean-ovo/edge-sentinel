#ifndef GATEWAY_MONITOR_IOCTL_H
#define GATEWAY_MONITOR_IOCTL_H

#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#endif

#define GATEWAY_MONITOR_IOC_MAGIC 'G'

#define GATEWAY_MONITOR_IOC_SET_PEAK_THR _IOW(GATEWAY_MONITOR_IOC_MAGIC, 1, float)
#define GATEWAY_MONITOR_IOC_SET_RMS_THR  _IOW(GATEWAY_MONITOR_IOC_MAGIC, 2, float)
#define GATEWAY_MONITOR_IOC_SET_GYRO_THR _IOW(GATEWAY_MONITOR_IOC_MAGIC, 3, float)

#define GATEWAY_MONITOR_DEVICE "/dev/gateway_monitor"

#endif
