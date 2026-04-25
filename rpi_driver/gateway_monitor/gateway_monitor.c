#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h> // 设备树匹配
#include <linux/regmap.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

// 定义一个辅助宏，用于快速生成 XYZ 三轴的加速度通道
#define MPU6050_ACCEL_CHAN(_axis, _addr) {                   \
    .type = IIO_ACCEL,                                       \
    .modified = 1,                                           \
    .channel2 = IIO_MOD_##_axis,                             \
    .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),            \
    .address = _addr,                                        \
}

static const struct of_device_id gateway_monitor_of_match[] = {
    {.compatible = "invensense,mpu6050"},
    {/* 结尾哨兵，必须有 */}};

MODULE_DEVICE_TABLE(of, gateway_monitor_of_match);

static const struct regmap_config mpu6050_regmap_config = {
    .reg_bits = 8,        // 寄存器地址是 8 位
    .val_bits = 8,        // 寄存器里的数据是 8 位
    .max_register = 0x75, // 我们目前知道的最大寄存器地址是 WHO_AM_I
};

struct mpu6050_data
{
    struct regmap *regmap;
};

static int mpu6050_read_raw(struct iio_dev *indio_dev,
                            struct iio_chan_spec const *chan,
                            int *val, int *val2, long mask)
{
    struct mpu6050_data *data = iio_priv(indio_dev);
    unsigned int val_h, val_l;
    int ret;

    // 我们目前只处理读取 RAW 数据的情况
    if (mask != IIO_CHAN_INFO_RAW)
        return -EINVAL;

    // 1. 使用 regmap_read 读取 chan->address (高八位)，存入 val_h
    ret = regmap_read(data->regmap, chan->address, &val_h);
    if (ret < 0)
    {
        pr_err("gateway_monitor: regmap_read 失败 %d\n", ret);
        return ret;
    }

    // 2. 使用 regmap_read 读取 chan->address + 1 (低八位)，存入 val_l
    ret = regmap_read(data->regmap, chan->address + 1, &val_l);
    if (ret < 0)
    {
        pr_err("gateway_monitor: regmap_read 失败 %d\n", ret);
        return ret;
    }
    // 3. 把 val_h 和 val_l 拼起来，强转为 short (有符号16位)，赋给 *val
    *val = (short)((val_h << 8) | val_l);

    // 返回 IIO_VAL_INT，告诉 IIO 子系统 *val 里面有一个整型数据
    return IIO_VAL_INT;
}

static const struct iio_info mpu6050_iio_info = {
    .read_raw = mpu6050_read_raw,
};

static const struct iio_chan_spec mpu6050_channels[] = {
    // 0x3B 是 ACCEL_XOUT_H 的地址
    MPU6050_ACCEL_CHAN(X, 0x3B),
    MPU6050_ACCEL_CHAN(Y, 0x3D),
    MPU6050_ACCEL_CHAN(Z, 0x3F),
};

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
        pr_err("gateway_monitor: regmap 失败 %ld\n", PTR_ERR(regmap));
        return PTR_ERR(regmap);
    }

    indio_dev = devm_iio_device_alloc(&client->dev, sizeof(struct mpu6050_data));
    if (!indio_dev)
        return -ENOMEM;

    data = iio_priv(indio_dev);
    data->regmap = regmap;

    indio_dev->name = "mpu6050";
    indio_dev->modes = INDIO_DIRECT_MODE;
    indio_dev->info = &mpu6050_iio_info;
    indio_dev->channels = mpu6050_channels;
    indio_dev->num_channels = ARRAY_SIZE(mpu6050_channels);

    ret = regmap_read(regmap, 0x75, &val);
    if (ret < 0)
    {
        pr_err("gateway_monitor: regmap_read 失败 %d\n", ret);
        return ret;
    }
    pr_info("gateway_monitor: WHO_AM_I = 0x%02x\n", val);

    if (val != 0x68)
    {
        pr_err("gateway_monitor: 芯片 ID 不匹配\n");
        return -ENODEV;
    }

    // 写入 0x00 到 0x6B 寄存器，解除休眠模式
    ret = regmap_write(regmap, 0x6B, 0x00);
    if (ret < 0)
    {
        pr_err("gateway_monitor: 唤醒传感器失败\n");
        return ret;
    }

    ret = devm_iio_device_register(&client->dev, indio_dev);
    if (ret)
        return ret;

    pr_info("gateway_monitor: probe 成功!\n");
    return 0;
}

static void imu_remove(struct i2c_client *client)
{
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