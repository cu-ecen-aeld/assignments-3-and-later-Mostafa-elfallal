/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Mostafa ElFallal"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     */
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     */
    mutex_lock_interruptible(&aesd_device.lock);
    struct aesd_buffer_entry *entry;
    size_t entry_offset_byte_rtn;
    while(entry = aesd_circular_buffer_find_entry_offset_for_fpos(&aesd_device.circular_buffer, *f_pos, &entry_offset_byte_rtn)) {
        size_t remaining = entry->size - entry_offset_byte_rtn;
        if(remaining > count) {
            // read only the requested number of bytes
            if(copy_to_user(buf, entry->buffptr + entry_offset_byte_rtn, count)) {
                mutex_unlock(&aesd_device.lock);
                return -EFAULT;
            }
            *f_pos += count;
            retval = count;
            mutex_unlock(&aesd_device.lock);
            return retval;
        }
        else {
            // read the entire entry
            if(copy_to_user(buf, entry->buffptr + entry_offset_byte_rtn, remaining)) {
                mutex_unlock(&aesd_device.lock);
                return -EFAULT;
            }
            *f_pos += remaining;
            retval += remaining;
            count -= remaining;
            buf += remaining;
        }
    }
    if(retval == 0) {
        // no more data to read
        *f_pos = 0;
    }
    mutex_unlock(&aesd_device.lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle write
     */
    mutex_lock_interruptible(&aesd_device.lock);
    if(aesd_device.entry == NULL) {
        aesd_device.entry = kmalloc(sizeof(struct aesd_buffer_entry), GFP_KERNEL);
        if(aesd_device.entry == NULL) {
            mutex_unlock(&aesd_device.lock);
            return -ENOMEM;
        }
        aesd_device.entry->buffptr = kmalloc(count, GFP_KERNEL);
        if(aesd_device.entry->buffptr == NULL) {
            kfree(aesd_device.entry);
            aesd_device.entry = NULL;
            mutex_unlock(&aesd_device.lock);
            return -ENOMEM;
        }
        aesd_device.entry->size = 0;
    }
    else {
        aesd_device.entry->buffptr = krealloc(aesd_device.entry->buffptr, aesd_device.entry->size + count, GFP_KERNEL);
        if(aesd_device.entry->buffptr == NULL) {
            kfree(aesd_device.entry);
            aesd_device.entry = NULL;
            mutex_unlock(&aesd_device.lock);
            return -ENOMEM;
        }
    }
    if(copy_from_user(aesd_device.entry->buffptr + aesd_device.entry->size, buf, count)) {
        kfree(aesd_device.entry->buffptr);
        kfree(aesd_device.entry);
        aesd_device.entry = NULL;
        mutex_unlock(&aesd_device.lock);
        return -EFAULT;
    }
    aesd_device.entry->size += count;
    if(aesd_device.entry->buffptr[aesd_device.entry->size - 1] == '\n') {
        bool wasFull = aesd_device.circular_buffer.full;
        size_t entry_offset_byte_rtn;
        struct aesd_buffer_entry *oldEntry = NULL;
        if(wasFull){
            oldEntry = aesd_circular_buffer_find_entry_offset_for_fpos(&aesd_device.circular_buffer,0, &entry_offset_byte_rtn);
        }
        aesd_circular_buffer_add_entry(&aesd_device.circular_buffer, aesd_device.entry);
        if(wasFull)
        {
            PDEBUG("Freeing Old Entry buffer");
            kfree(oldEntry->buffptr);
            PDEBUG("Freeing Old Entry");
            kfree(oldEntry);
        }
        aesd_device.entry = NULL;
    }
    retval = count;
    mutex_unlock(&aesd_device.lock);
    return retval;
}
struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);
    // log err and devno to printk
    printk(KERN_WARNING "No Worries err = %d and devno = %d\n",err,devno);
    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int __init aesd_init_module(void)
{
    printk(KERN_WARNING "No Worries Just starting init\n");
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,"aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Worries Major is not found\n");
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */
    aesd_circular_buffer_init(&aesd_device.circular_buffer);
    mutex_init(&aesd_device.lock);
    aesd_device.entry = NULL;

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
        printk(KERN_WARNING "Worries unregister\n");
    }
    printk(KERN_INFO "aesdchar module loaded\n");
    return result;

}

void __exit aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
