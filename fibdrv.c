#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/time.h>
#include <linux/uaccess.h>

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("Fibonacci engine driver");
MODULE_VERSION("0.1");

#define DEV_FIBONACCI_NAME "fibonacci"

/* MAX_LENGTH is set to 92 because
 * ssize_t can't fit the number > 92
 */
#define MAX_LENGTH 500
#define MAX_DIGIT 500
#define OFFSET 48

typedef struct {
    char decimal[MAX_DIGIT];
    int length;
} bignum;

#define bn_tmp(x) bn_new(&(bignum){.length = 0}, x)

bignum *bn_new(bignum *bn, char decimal[])
{
    bn->length = strlen(decimal);
    memcpy(bn->decimal, decimal, bn->length);
    return bn;
}

void bn_add(bignum *x, bignum *y, bignum *dest)
{
    if (x->length < y->length)
        return bn_add(y, x, dest);
    int carry = 0;
    for (int i = 0; i < y->length; i++) {
        dest->decimal[i] = y->decimal[i] + x->decimal[i] + carry - OFFSET;
        if (dest->decimal[i] >= 10 + OFFSET) {
            carry = 1;
            dest->decimal[i] -= 10;
        } else
            carry = 0;
    }
    for (int i = y->length; i < x->length; i++) {
        dest->decimal[i] = x->decimal[i] + carry;
        if (dest->decimal[i] >= 10 + OFFSET) {
            carry = 1;
            dest->decimal[i] -= 10;
        } else
            carry = 0;
    }
    dest->length = x->length;
    dest->decimal[dest->length] = carry + OFFSET;
    if (carry)
        dest->length++;
    dest->decimal[dest->length] = '\0';
}


static dev_t fib_dev = 0;
static struct cdev *fib_cdev;
static struct class *fib_class;
static DEFINE_MUTEX(fib_mutex);

bignum *fib_sequence_org(int k)
{
    int i;
    bignum *f0 = bn_tmp("0");
    bignum *f1 = bn_tmp("1");
    bignum *ans = bn_tmp("0");
    if (k == 0)
        return f0;
    if (k == 1)
        return f1;

    for (i = 2; i <= k; i++) {
        bn_add(f0, f1, ans);
        f0 = f1;
        f1 = ans;
    }
    return ans;
}



static int fib_open(struct inode *inode, struct file *file)
{
    if (!mutex_trylock(&fib_mutex)) {
        printk(KERN_ALERT "fibdrv is in use");
        return -EBUSY;
    }
    return 0;
}

static int fib_release(struct inode *inode, struct file *file)
{
    mutex_unlock(&fib_mutex);
    return 0;
}

/* calculate the fibonacci number at given offset */
static ssize_t fib_read(struct file *file,
                        char *buf,
                        size_t size,
                        loff_t *offset)
{
    ktime_t k1, k2;
    k1 = ktime_get();
    char *rev_fib = fib_sequence_org(*offset)->decimal;
    int length = strlen(rev_fib);
    char result[MAX_DIGIT];
    for (int i = 0; i < length; i++)
        result[i] = rev_fib[length - 1 - i];
    result[length] = '\0';
    copy_to_user(buf, result, length + 1);
    k2 = ktime_sub(ktime_get(), k1);
    return (long long) ktime_to_ns(k2);
}

/* write operation is skipped */
static ssize_t fib_write(struct file *file,
                         const char *buf,
                         size_t size,
                         loff_t *offset)
{
    return 1;
}

static loff_t fib_device_lseek(struct file *file, loff_t offset, int orig)
{
    loff_t new_pos = 0;
    switch (orig) {
    case 0: /* SEEK_SET: */
        new_pos = offset;
        break;
    case 1: /* SEEK_CUR: */
        new_pos = file->f_pos + offset;
        break;
    case 2: /* SEEK_END: */
        new_pos = MAX_LENGTH - offset;
        break;
    }

    if (new_pos > MAX_LENGTH)
        new_pos = MAX_LENGTH;  // max case
    if (new_pos < 0)
        new_pos = 0;        // min case
    file->f_pos = new_pos;  // This is what we'll use now
    return new_pos;
}

const struct file_operations fib_fops = {
    .owner = THIS_MODULE,
    .read = fib_read,
    .write = fib_write,
    .open = fib_open,
    .release = fib_release,
    .llseek = fib_device_lseek,
};

static int __init init_fib_dev(void)
{
    int rc = 0;

    mutex_init(&fib_mutex);

    // Let's register the device
    // This will dynamically allocate the major number
    rc = alloc_chrdev_region(&fib_dev, 0, 1, DEV_FIBONACCI_NAME);

    if (rc < 0) {
        printk(KERN_ALERT
               "Failed to register the fibonacci char device. rc = %i",
               rc);
        return rc;
    }

    fib_cdev = cdev_alloc();
    if (fib_cdev == NULL) {
        printk(KERN_ALERT "Failed to alloc cdev");
        rc = -1;
        goto failed_cdev;
    }
    cdev_init(fib_cdev, &fib_fops);
    rc = cdev_add(fib_cdev, fib_dev, 1);

    if (rc < 0) {
        printk(KERN_ALERT "Failed to add cdev");
        rc = -2;
        goto failed_cdev;
    }

    fib_class = class_create(THIS_MODULE, DEV_FIBONACCI_NAME);

    if (!fib_class) {
        printk(KERN_ALERT "Failed to create device class");
        rc = -3;
        goto failed_class_create;
    }

    if (!device_create(fib_class, NULL, fib_dev, NULL, DEV_FIBONACCI_NAME)) {
        printk(KERN_ALERT "Failed to create device");
        rc = -4;
        goto failed_device_create;
    }
    return rc;
failed_device_create:
    class_destroy(fib_class);
failed_class_create:
    cdev_del(fib_cdev);
failed_cdev:
    unregister_chrdev_region(fib_dev, 1);
    return rc;
}

static void __exit exit_fib_dev(void)
{
    mutex_destroy(&fib_mutex);
    device_destroy(fib_class, fib_dev);
    class_destroy(fib_class);
    cdev_del(fib_cdev);
    unregister_chrdev_region(fib_dev, 1);
}

module_init(init_fib_dev);
module_exit(exit_fib_dev);
