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
#include "aesd_ioctl.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Mostafa ElFallal"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");


int __init aesd_init_module(void);
void __exit aesd_cleanup_module(void);
static long aesd_adjust_file_offset(struct file*filp,unsigned int write_cmd,unsigned int write_cmd_offset);
struct aesd_dev aesd_device;
int aesd_open(struct inode *inode, struct file *filp);
int aesd_release(struct inode *inode, struct file *filp);
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
    loff_t *f_pos);
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
        loff_t *f_pos);
loff_t aesd_llseek(struct file *file, loff_t offset, int origin);
static long aesd_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
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
    PDEBUG("read %zu bytes with offset %lld\n",count,*f_pos);
    /**
     * TODO: handle read
     */
    if (mutex_lock_interruptible(&aesd_device.lock)) {
        PDEBUG("Mutex lock interrupted by a signal.\n");
        return -ERESTARTSYS;  // Return error code to indicate interruption
    }
    struct aesd_buffer_entry *entry;
    size_t entry_offset_byte_rtn;
    while((entry = aesd_circular_buffer_find_entry_offset_for_fpos(&aesd_device.circular_buffer, *f_pos, &entry_offset_byte_rtn))) {
        size_t remaining = entry->size - entry_offset_byte_rtn;
        PDEBUG("remaining %zu bytes\n",remaining);
        if(remaining > count) {
            // read only the requested number of bytes
            if(copy_to_user(buf, entry->buffptr + entry_offset_byte_rtn, count)) {
                mutex_unlock(&aesd_device.lock);
                return -EFAULT;
            }
            retval = count;
            *f_pos += retval;
            PDEBUG("retval %zu",retval);
            mutex_unlock(&aesd_device.lock);
            return retval;
        }
        else {
            // read the entire entry
            if(copy_to_user(buf, entry->buffptr + entry_offset_byte_rtn, remaining)) {
                mutex_unlock(&aesd_device.lock);
                return -EFAULT;
            }
            retval += remaining;
            count -= remaining;
            buf += remaining;
        }
    }
    *f_pos += retval;
    PDEBUG("retval %zu",retval);
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
    if (mutex_lock_interruptible(&aesd_device.lock)) {
        PDEBUG("Mutex lock interrupted by a signal.\n");
        return -ERESTARTSYS;  // Return error code to indicate interruption
    }
    if(aesd_device.entry->buffptr == NULL) {
        aesd_device.entry->buffptr = kmalloc(count, GFP_KERNEL);
        if(aesd_device.entry->buffptr == NULL) {
            mutex_unlock(&aesd_device.lock);
            return -ENOMEM;
        }
    }
    else {
        aesd_device.entry->buffptr = krealloc(aesd_device.entry->buffptr, aesd_device.entry->size + count, GFP_KERNEL);
        if(aesd_device.entry->buffptr == NULL) {
            mutex_unlock(&aesd_device.lock);
            return -ENOMEM;
        }
    }
    if(copy_from_user((void *)&aesd_device.entry->buffptr[aesd_device.entry->size], buf, count)) {
        kfree(aesd_device.entry->buffptr);
        aesd_device.entry->buffptr = NULL;
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
            aesd_device.size -= oldEntry->size;
            PDEBUG("Freeing Old Entry buffer");
            kfree(oldEntry->buffptr);
        }
        aesd_device.size += count;
        aesd_device.entry->buffptr = NULL;
    }
    retval = count;
    *f_pos += retval;
    mutex_unlock(&aesd_device.lock);
    return retval;
}

loff_t aesd_llseek(struct file *file, loff_t offset, int origin)
{
    loff_t new_pos;
    if (mutex_lock_interruptible(&aesd_device.lock)) {
        PDEBUG("Mutex lock interrupted by a signal.\n");
        return -ERESTARTSYS;  // Return error code to indicate interruption
    }
    // loff_t size = 0;
    // uint8_t index;
    // struct aesd_circular_buffer buffer;
    // struct aesd_buffer_entry *entry;
    // AESD_CIRCULAR_BUFFER_FOREACH(entry,&buffer,index) {
    //     size += entry->size;
    // }
    PDEBUG("Total size = %lld\n",aesd_device.size);
    new_pos = fixed_size_llseek(file, offset, origin, aesd_device.size);
    mutex_unlock(&aesd_device.lock);


    PDEBUG("New file position: %lld\n", new_pos);

    return new_pos;
}
static long aesd_adjust_file_offset(struct file*filp,unsigned int write_cmd,unsigned int write_cmd_offset)
{
    /**
     * Check for valid write_cmd and write_cmd_offset values
     * When would values be invalid? 
     * * haven’t written this command yet
     * * out of range cmd (11)
     * * write_cmd_offset is >= size of command
     * Calculate the start offset to write_cmd
     * Add write_cmd_offset
     * Save as filp->f_pos
     * ret -ERESTARTSYS (failed to obtain mutex)
     * ret -EINVAL (out of range)
     * .unlocked_ioctl = mychardev_ioctl,  // Use this for newer kernelsde .
     */
    if (mutex_lock_interruptible(&aesd_device.lock)) {
        PDEBUG("Mutex lock interrupted by a signal.\n");
        return -ERESTARTSYS;  // Return error code to indicate interruption
    }
    uint8_t index;
    struct aesd_buffer_entry *entry;
    loff_t total = 0;
    struct aesd_circular_buffer *buf = &aesd_device.circular_buffer ;
    AESD_CIRCULAR_BUFFER_FOREACH(entry,buf,index)
    {
        if( write_cmd == index )
        {
            if(write_cmd_offset < entry->size){
                filp->f_pos = total+write_cmd_offset;
                mutex_unlock(&aesd_device.lock);
                return 0;
            }
        }
        else{
            total += entry->size;
        }
    }
    mutex_unlock(&aesd_device.lock);
    return -EINVAL;
}
static long aesd_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int retval = 0;
    switch (cmd) {
    case AESDCHAR_IOCSEEKTO:
        struct aesd_seekto seekto;
        if (copy_from_user(&seekto, (const void __user *)arg, sizeof(struct aesd_seekto)))
            return -EFAULT;
        else{
            retval = aesd_adjust_file_offset(file,seekto.write_cmd,seekto.write_cmd_offset);
        }
        break;
    default:
        return -ENOTTY;
    }

    return retval;
}
struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .llseek = aesd_llseek,
    .open =     aesd_open,
    .release =  aesd_release,
    .unlocked_ioctl = aesd_ioctl,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);
    // log err and devno to printk
    // PDEBUG("No Worries err = %d and devno = %d\n",err,devno);
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
    PDEBUG("No Worries Just starting init\n");
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,"aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        PDEBUG("Worries Major is not found\n");
        PDEBUG("Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */
    aesd_circular_buffer_init(&aesd_device.circular_buffer);
    mutex_init(&aesd_device.lock);
    aesd_device.entry = kmalloc(sizeof(struct aesd_buffer_entry), GFP_KERNEL);
    aesd_device.entry->buffptr = NULL;
    aesd_device.entry->size = 0;
    aesd_device.size = 0;
    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
        PDEBUG("Worries unregister\n");
    }
    PDEBUG("aesdchar module loaded\n");
    return result;

}

void __exit aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */
    kfree(aesd_device.entry);

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
