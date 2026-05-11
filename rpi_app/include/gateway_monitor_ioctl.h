#ifndef GATEWAY_MONITOR_IOCTL_H
#define GATEWAY_MONITOR_IOCTL_H

#ifdef __KERNEL__
#include <linux/ioctl.h>
#include <linux/types.h>
typedef s32 gateway_monitor_threshold_t;
#else
#include <sys/ioctl.h>
#include <stdint.h>
typedef int32_t gateway_monitor_threshold_t;
#endif

#define GATEWAY_MONITOR_IOC_MAGIC 'G'

#define GATEWAY_MONITOR_IOC_SET_PEAK_THR _IOW(GATEWAY_MONITOR_IOC_MAGIC, 1, gateway_monitor_threshold_t)
#define GATEWAY_MONITOR_IOC_SET_RMS_THR  _IOW(GATEWAY_MONITOR_IOC_MAGIC, 2, gateway_monitor_threshold_t)
#define GATEWAY_MONITOR_IOC_SET_GYRO_THR _IOW(GATEWAY_MONITOR_IOC_MAGIC, 3, gateway_monitor_threshold_t)
#define GATEWAY_MONITOR_IOC_GET_PEAK_THR _IOR(GATEWAY_MONITOR_IOC_MAGIC, 4, gateway_monitor_threshold_t)
#define GATEWAY_MONITOR_IOC_GET_RMS_THR  _IOR(GATEWAY_MONITOR_IOC_MAGIC, 5, gateway_monitor_threshold_t)
#define GATEWAY_MONITOR_IOC_GET_GYRO_THR _IOR(GATEWAY_MONITOR_IOC_MAGIC, 6, gateway_monitor_threshold_t)

#define GATEWAY_MONITOR_DEVICE "/dev/gateway_monitor"

#endif
