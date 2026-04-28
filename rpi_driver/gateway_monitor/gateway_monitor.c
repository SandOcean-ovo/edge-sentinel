#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/delay.h>

/* ioctl 命令码 —— 与 userspace 头文件 gateway_monitor_ioctl.h 保持同步
 * 阈值单位：milli-g (s32)，例如 1500 表示 1.5g */
#define GATEWAY_MONITOR_IOC_MAGIC 'G'
#define GATEWAY_MONITOR_IOC_SET_PEAK_THR _IOW(GATEWAY_MONITOR_IOC_MAGIC, 1, s32)
#define GATEWAY_MONITOR_IOC_SET_RMS_THR _IOW(GATEWAY_MONITOR_IOC_MAGIC, 2, s32)
#define GATEWAY_MONITOR_IOC_SET_GYRO_THR _IOW(GATEWAY_MONITOR_IOC_MAGIC, 3, s32)

#define MPU6050_ACCEL_CHAN(_axis, _addr) {        \
    .type = IIO_ACCEL,                            \
    .modified = 1,                                \
    .channel2 = IIO_MOD_##_axis,                  \
    .info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
    .address = _addr,                             \
}

static const struct of_device_id gateway_monitor_of_match[] = {
    {.compatible = "invensense,mpu6050"},
    {/* sentinel */}};

MODULE_DEVICE_TABLE(of, gateway_monitor_of_match);

static const struct regmap_config mpu6050_regmap_config = {
    .reg_bits = 8,
    .val_bits = 8,
    .max_register = 0x75,
};

struct mpu6050_data
{
    struct regmap *regmap;
    struct miscdevice miscdev;
    /* 阈值 (milli-g / milli-deg/s) —— mutex 保护 */
    struct mutex thr_lock;
    s32 accel_peak_thr;
    s32 accel_rms_thr;
    s32 gyro_thr;
};

static struct i2c_client *global_client;

/* ---------- misc 设备 ioctl ---------- */

static long gateway_monitor_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct mpu6050_data *data = filp->private_data;
    s32 val;

    switch (cmd)
    {
    case GATEWAY_MONITOR_IOC_SET_PEAK_THR:
        if (copy_from_user(&val, (s32 __user *)arg, sizeof(val)))
            return -EFAULT;
        mutex_lock(&data->thr_lock);
        data->accel_peak_thr = val;
        mutex_unlock(&data->thr_lock);
        pr_info("gateway_monitor: peak threshold set to %d.%03d g\n",
                val / 1000, abs(val) % 1000);
        break;
    case GATEWAY_MONITOR_IOC_SET_RMS_THR:
        if (copy_from_user(&val, (s32 __user *)arg, sizeof(val)))
            return -EFAULT;
        mutex_lock(&data->thr_lock);
        data->accel_rms_thr = val;
        mutex_unlock(&data->thr_lock);
        pr_info("gateway_monitor: rms threshold set to %d.%03d g\n",
                val / 1000, abs(val) % 1000);
        break;
    case GATEWAY_MONITOR_IOC_SET_GYRO_THR:
        if (copy_from_user(&val, (s32 __user *)arg, sizeof(val)))
            return -EFAULT;
        mutex_lock(&data->thr_lock);
        data->gyro_thr = val;
        mutex_unlock(&data->thr_lock);
        pr_info("gateway_monitor: gyro threshold set to %d.%d deg/s\n",
                val / 1000, abs(val) % 1000);
        break;
    default:
        return -ENOTTY;
    }
    return 0;
}

static int gateway_monitor_misc_open(struct inode *inode, struct file *file)
{
    if (!global_client)
        return -ENODEV;

    struct iio_dev *indio_dev = i2c_get_clientdata(global_client);
    if (!indio_dev)
        return -ENODEV;

    file->private_data = iio_priv(indio_dev);
    return 0;
}

static const struct file_operations gateway_monitor_misc_fops = {
    .owner = THIS_MODULE,
    .open = gateway_monitor_misc_open,
    .unlocked_ioctl = gateway_monitor_ioctl,
};

/* ---------- IIO read_raw ---------- */

static int mpu6050_read_raw(struct iio_dev *indio_dev,
                            struct iio_chan_spec const *chan,
                            int *val, int *val2, long mask)
{
    struct mpu6050_data *data = iio_priv(indio_dev);
    unsigned int val_h, val_l;
    int ret;

    if (mask != IIO_CHAN_INFO_RAW)
        return -EINVAL;

    ret = regmap_read(data->regmap, chan->address, &val_h);
    if (ret < 0)
    {
        pr_err("gateway_monitor: regmap_read failed %d\n", ret);
        return ret;
    }

    ret = regmap_read(data->regmap, chan->address + 1, &val_l);
    if (ret < 0)
    {
        pr_err("gateway_monitor: regmap_read failed %d\n", ret);
        return ret;
    }

    *val = (short)((val_h << 8) | val_l);

    /* 阈值比较：raw -> milli-g 定点转换
     * MPU6050 ±2g 量程: 1 LSB = 1/16384 g ≈ 0.061 mg
     * milli_g = raw * 1000 / 16384 */
    if (chan->type == IIO_ACCEL)
    {
        s32 milli_g = (s32)(*val) * 1000 / 16384;
        s32 abs_mg = milli_g < 0 ? -milli_g : milli_g;

        mutex_lock(&data->thr_lock);
        if (data->accel_peak_thr > 0 && abs_mg > data->accel_peak_thr)
        {
            pr_warn("gateway_monitor: ACCEL ch0x%02lx raw=%d (%d.%03d g) exceeds peak threshold %d.%03d g\n",
                    chan->address, *val,
                    milli_g / 1000, abs(milli_g) % 1000,
                    data->accel_peak_thr / 1000, abs(data->accel_peak_thr) % 1000);
        }
        mutex_unlock(&data->thr_lock);
    }

    return IIO_VAL_INT;
}

static const struct iio_info mpu6050_iio_info = {
    .read_raw = mpu6050_read_raw,
};

static const struct iio_chan_spec mpu6050_channels[] = {
    MPU6050_ACCEL_CHAN(X, 0x3B),
    MPU6050_ACCEL_CHAN(Y, 0x3D),
    MPU6050_ACCEL_CHAN(Z, 0x3F),
};

/* ---------- probe / remove ---------- */

static int imu_probe(struct i2c_client *client)
{
    unsigned int val;
    int ret;
    struct regmap *regmap;
    struct iio_dev *indio_dev;
    struct mpu6050_data *data;

    regmap = devm_regmap_init_i2c(client, &mpu6050_regmap_config);
    if (IS_ERR(regmap))
    {
        pr_err("gateway_monitor: regmap init failed %ld\n", PTR_ERR(regmap));
        return PTR_ERR(regmap);
    }

    indio_dev = devm_iio_device_alloc(&client->dev, sizeof(struct mpu6050_data));
    if (!indio_dev)
        return -ENOMEM;

    data = iio_priv(indio_dev);
    data->regmap = regmap;
    mutex_init(&data->thr_lock);

    indio_dev->name = "mpu6050";
    indio_dev->modes = INDIO_DIRECT_MODE;
    indio_dev->info = &mpu6050_iio_info;
    indio_dev->channels = mpu6050_channels;
    indio_dev->num_channels = ARRAY_SIZE(mpu6050_channels);

    ret = regmap_read(regmap, 0x75, &val);
    if (ret < 0)
    {
        pr_err("gateway_monitor: WHO_AM_I read failed %d\n", ret);
        return ret;
    }
    pr_info("gateway_monitor: WHO_AM_I = 0x%02x\n", val);

    if (val != 0x68)
    {
        pr_err("gateway_monitor: chip ID mismatch\n");
        return -ENODEV;
    }

    /* 软复位 MPU6050，防止异常卡死 */
    ret = regmap_write(regmap, 0x6B, 0x80);
    if (ret < 0)
    {
        pr_err("gateway_monitor: regmap write failed\n");
        return ret;
    }
    msleep(100);

    ret = regmap_write(regmap, 0x6B, 0x00);
    if (ret < 0)
    {
        pr_err("gateway_monitor: regmap write failed\n");
        return ret;
    }
    msleep(50);

    /* 注册 IIO 设备 */
    ret = devm_iio_device_register(&client->dev, indio_dev);
    if (ret)
        return ret;

    /* 注册 misc 字符设备，暴露 ioctl 接口 */
    data->miscdev.minor = MISC_DYNAMIC_MINOR;
    data->miscdev.name = "gateway_monitor";
    data->miscdev.fops = &gateway_monitor_misc_fops;
    ret = misc_register(&data->miscdev);
    if (ret)
    {
        pr_err("gateway_monitor: misc_register failed %d\n", ret);
        return ret;
    }

    i2c_set_clientdata(client, indio_dev);
    global_client = client;

    pr_info("gateway_monitor: probe success, misc device /dev/gateway_monitor\n");
    return 0;
}

static void imu_remove(struct i2c_client *client)
{
    struct iio_dev *indio_dev = i2c_get_clientdata(client);
    struct mpu6050_data *data = iio_priv(indio_dev);

    misc_deregister(&data->miscdev);
    mutex_destroy(&data->thr_lock);
    global_client = NULL;

    pr_info("gateway_monitor: removed\n");
}

static struct i2c_driver gateway_monitor_driver = {
    .driver = {
        .name = "mpu6050",
        .of_match_table = gateway_monitor_of_match,
    },
    .probe = imu_probe,
    .remove = imu_remove,
};

module_i2c_driver(gateway_monitor_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SandOcean");
