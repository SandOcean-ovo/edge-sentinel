#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>

static const struct of_device_id mcu_of_match[] = {
    {.compatible = "edge_sentinel,mcu"},
    {/* sentinel */}};

typedef struct edge_alarm_dev
{
    dev_t dev_num;
    struct cdev alarm_cdev;
    struct class *alarm_class;
    struct device *alarm_device;
    int irq_num;
    wait_queue_head_t wait_queue;
    int alarm_flag;
} edge_alarm_dev;

MODULE_DEVICE_TABLE(of, mcu_of_match);

irqreturn_t mcu_alarm_irq_handler(int irq, void *dev_id);

irqreturn_t mcu_alarm_irq_handler(int irq, void *dev_id)
{
    struct edge_alarm_dev *my_dev = (struct edge_alarm_dev *)dev_id;
    
    my_dev->alarm_flag = 1;

    wake_up_interruptible(&my_dev->wait_queue);

    return IRQ_HANDLED;
}

EXPORT_SYMBOL(mcu_alarm_irq_handler);

static int mcu_alarm_open(struct inode *inode, struct file *file)
{
    file->private_data = container_of(inode->i_cdev, struct edge_alarm_dev, alarm_cdev);

    pr_info("alarm: 设备已打开\n");

    return 0;
}

static int mcu_alarm_release(struct inode *inode, struct file *file)
{
    pr_info("alarm: 设备已关闭\n");

    return 0;
}

static ssize_t mcu_alarm_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
    edge_alarm_dev *my_dev = filp->private_data;
    /* 等待中断触发 */
    if (my_dev->alarm_flag == 0)
    {
        /* 非阻塞模式 直接返回 */
        if (filp->f_flags & O_NONBLOCK)
            return -EAGAIN;

        /* 阻塞模式 阻塞等待中断触发 */
        if (wait_event_interruptible(my_dev->wait_queue, my_dev->alarm_flag))
            return -ERESTARTSYS;
    }
    my_dev->alarm_flag = 0;

    char str[] = "ALARM TRIGGERED!\n";

    if (count < sizeof(str))
        return -EINVAL;

    int ret = copy_to_user(buf, str, sizeof(str));

    if (ret != 0)
        return -EFAULT;

    return sizeof(str);
}

static __poll_t mcu_alarm_poll(struct file *filp, struct poll_table_struct *wait)
{
    edge_alarm_dev *my_dev = filp->private_data;
    __poll_t mask = 0;
    poll_wait(filp, &my_dev->wait_queue, wait);

    if (my_dev->alarm_flag == 1)
    {
        mask |= (EPOLLIN | EPOLLRDNORM);
    }

    return mask;
}

struct file_operations mcu_alarm_fops = {
    .owner = THIS_MODULE,
    .open = mcu_alarm_open,
    .release = mcu_alarm_release,
    .read = mcu_alarm_read,
    .poll = mcu_alarm_poll,
};

static int mcu_alarm_probe(struct platform_device *pdev)
{
    edge_alarm_dev *my_dev;
    int ret;

    my_dev = devm_kzalloc(&pdev->dev, sizeof(edge_alarm_dev), GFP_KERNEL);
    if (my_dev == NULL)
        return -ENOMEM;

    init_waitqueue_head(&my_dev->wait_queue);

    // 为了让 remove 能够回收 my_dev
    platform_set_drvdata(pdev, my_dev);

    my_dev->irq_num = platform_get_irq(pdev, 0);
    if (my_dev->irq_num < 0)
    {
        pr_err("edge_alarm: get irq_num failed\n");
        return my_dev->irq_num;
    }

    pr_info("edge_alarm: get irq_num %d", my_dev->irq_num);

    // 注册isr
    ret = devm_request_irq(&pdev->dev, my_dev->irq_num, mcu_alarm_irq_handler, 0, "edge_alarm", my_dev);
    if (ret < 0)
    {
        pr_err("edge_alarm: 注册ISR失败\n");
        return ret;
    }

    ret = alloc_chrdev_region(&my_dev->dev_num, 0, 1, "edge_alarm_region");
    if (ret < 0)
    {
        pr_err("edge_alarm: 无法申请设备号\n");
        return ret;
    }

    cdev_init(&my_dev->alarm_cdev, &mcu_alarm_fops);

    my_dev->alarm_cdev.owner = THIS_MODULE;

    ret = cdev_add(&my_dev->alarm_cdev, my_dev->dev_num, 1);
    if (ret < 0)
    {
        pr_err("edge_alarm: cdev 添加失败\n");
        goto unregister_region;
    }

    my_dev->alarm_class = class_create("edge_alarm_class");
    if (IS_ERR(my_dev->alarm_class))
    {
        ret = PTR_ERR(my_dev->alarm_class);
        goto delete_cdev;
    }

    my_dev->alarm_device = device_create(my_dev->alarm_class, NULL, my_dev->dev_num, NULL, "edge_alarm");
    if (IS_ERR(my_dev->alarm_device))
    {
        ret = PTR_ERR(my_dev->alarm_device);
        goto delete_class;
    }

    pr_info("edge_alarm: probe success!\n");
    return 0;

delete_class:
    class_destroy(my_dev->alarm_class);

delete_cdev:
    cdev_del(&my_dev->alarm_cdev);

unregister_region:
    unregister_chrdev_region(my_dev->dev_num, 1);

    return ret;
}

static void mcu_alarm_remove(struct platform_device *pdev)
{
    // 从 pdev 中取回私有数据
    edge_alarm_dev *my_dev = platform_get_drvdata(pdev);

    device_destroy(my_dev->alarm_class, my_dev->dev_num);
    class_destroy(my_dev->alarm_class);
    cdev_del(&my_dev->alarm_cdev);
    unregister_chrdev_region(my_dev->dev_num, 1);

    pr_info("edge_alarm: bye bye\n");
}

static struct platform_driver mcu_alarm_driver = {
    .driver = {
        .name = "edge_mcu_alarm",
        .of_match_table = mcu_of_match,
    },
    .probe = mcu_alarm_probe,
    .remove = mcu_alarm_remove,
};

module_platform_driver(mcu_alarm_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SandOcean");