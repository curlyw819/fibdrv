#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("Fibonacci engine driver");
MODULE_VERSION("0.1");

#define DEV_FIBONACCI_NAME "fibonacci"

/* MAX_LENGTH is set to 92 because
 * ssize_t can't fit the number > 92
 */
#define MAX_LENGTH 100
#define MAX_DIGIT 100
#define OFFSET 48

typedef struct {
    char decimal[MAX_DIGIT];
    int length;
} bignum;

#define bn_tmp(x) \
    bn_new(&(bignum) { .length = 0 }, x)

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

void bn_subtract(bignum *x, bignum *y, bignum *dest)
{
    bignum *x_copy = bn_tmp(x->decimal);
    if (x->length == y->length) {
        int length = x->length;

        while (x->decimal[length - 1] == y->decimal[length - 1]) {
            length--;
            if (length == 1) {
                dest->length = 1;
                dest->decimal[0] = x->decimal[0] - y->decimal[0];
            }
        }
        x_copy->decimal[length - 1] =
            x->decimal[length - 1] - y->decimal[length - 1] + OFFSET;
        x_copy->decimal[length] = '\0';
        x_copy->length = length;
    }

    char nine[MAX_DIGIT];
    for (int i = 0; i < x->length - 1; i++)
        nine[i] = '9';
    nine[x_copy->length - 1] = '\0';
    bignum *nines = bn_tmp(nine);

    if (!(--x_copy->decimal[x_copy->length - 1]))
        x_copy->length--;

    for (int i = 0; i < y->length; i++) {
        nines->decimal[i] -= y->decimal[i];
        nines->decimal[i] += OFFSET;
    }
    bn_add(x_copy, nines, x_copy);
    bn_add(bn_tmp("1"), x_copy, dest);
}

void bn_multiply(bignum *x, bignum *y, bignum *dest)
{
    if (x->length < y->length)
        return bn_multiply(y, x, dest);
    bignum *sum = bn_tmp("0");
    for (int i = 0; i < y->length; i++) {
        bignum *tmp = bn_tmp("0");
        for (int j = 0; j < y->decimal[i] - OFFSET; j++)
            bn_add(x, tmp, tmp);
        for (int j = tmp->length; j >= 0; j--)
            tmp->decimal[j + i] = tmp->decimal[j];
        for (int j = 0; j < i; j++)
            tmp->decimal[j] = '0';
        tmp->length += i;
        bn_add(tmp, sum, sum);
    }
    dest->length = sum->length;
    for (int i = 0; i <= sum->length; i++)
        dest->decimal[i] = sum->decimal[i];
}

static dev_t fib_dev = 0;
static struct cdev *fib_cdev;
static struct class *fib_class;
static DEFINE_MUTEX(fib_mutex);

bignum *fib_sequence(int k)
{
    /* FIXME: use clz/ctz and fast algorithms to speed up */
    bignum *a = bn_tmp("0"), *b = bn_tmp("1");
    bignum *t1 = bn_tmp("0"), *t2 = bn_tmp("0");
    bignum *tmp = bn_tmp("2");
    if (k == 0)
        return a;
    int i = 31 - __builtin_clz(k);
    for (; i >= 0; i--) {
        bn_multiply(bn_tmp("2"), b, tmp);
        bn_subtract(tmp, a, tmp);
        bn_multiply(a, tmp, t1);
        bn_multiply(b, b, tmp);
        bn_multiply(a, a, t2);
        bn_add(tmp, t2, t2);
        for (int j = 0; j < t2->length; j++) {
            a->decimal[j] = t1->decimal[j];
            b->decimal[j] = t2->decimal[j];
        }
        a->length = t1->length;
        b->length = t2->length;
        if ((k >> i) & 1) {
            bn_add(a, b, t1);
            for (int j = 0; j < t1->length; j++) {
                a->decimal[j] = b->decimal[j];
                b->decimal[j] = t1->decimal[j];
            }
            a->length = b->length;
            b->length = t1->length;
        }
    }
    return a;
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
    char *rev_fib = fib_sequence(*offset)->decimal;
    int length = strlen(rev_fib);
    char result[MAX_DIGIT];
    for (int i = 0; i < length; i++)
        result[i] = rev_fib[length - 1 - i];
    result[length] = '\0';
    copy_to_user(buf, result, length + 1);
    return 0;
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
