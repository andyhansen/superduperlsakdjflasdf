/* **************** LDD:1.0 s_04/lab3_seek.c **************** */
/*
 * The code herein is: Copyright Jerry Cooperstein, 2009
 *
 * This Copyright is retained for the purpose of protecting free
 * redistribution of source.
 *
 *     URL:    http://www.coopj.com
 *     email:  coop@coopj.com
 *
 * The primary maintainer for this code is Jerry Cooperstein
 * The CONTRIBUTORS file (distributed with this
 * file) lists those known to have contributed to the source.
 *
 * This code is distributed under Version 2 of the GNU General Public
 * License, which you should have received with the source.
 *
 */
/*
 * Seeking and the End of the Device.
 *
 * Keeping track of file position.
 *
 * Adapt one of the previous drivers to have the read and write
 * entries watch out for going off the end of the device.
 *
 * Implement a lseek entry point.  See the man page for lseek to see
 * how return values and error codes should be specified.
 *
 * For an extra exercise, unset the FMODE_LSEEK bit to make any
 * attempt to seek result in an error.
 @*/

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/cdev.h>

#define MYDEV_NAME "mycdrv"

static char *ramdisk;
static size_t ramdisk_size = (16 * PAGE_SIZE);
static dev_t first;
static unsigned int count = 1;
static int my_major = 700, my_minor = 0;
static struct cdev *my_cdev;
//static void *private_data;

static int mycdrv_open(struct inode *inode, struct file *file)
{
	static int counter = 0;
	file->private_data = kmalloc(ramdisk_size, GFP_KERNEL);
	printk(KERN_INFO " attempting to open device: %s:\n", MYDEV_NAME);
	printk(KERN_INFO " MAJOR number = %d, MINOR number = %d\n",
	       imajor(inode), iminor(inode));
	counter++;

	printk(KERN_INFO " successfully open  device: %s:\n\n", MYDEV_NAME);
	printk(KERN_INFO "I have been opened  %d times since being loaded\n",
	       counter);
	//printk(KERN_INFO "ref=%d\n", module_refcount(THIS_MODULE));

	/* turn this on to inhibit seeking */
	/* file->f_mode = file->f_mode & ~FMODE_LSEEK; */

	return 0;
}

static int mycdrv_release(struct inode *inode, struct file *file)
{
	printk(KERN_INFO " CLOSING device: %s:\n\n", MYDEV_NAME);
	kfree(file->private_data);
	return 0;
}

static ssize_t
mycdrv_read(struct file *file, char __user * buf, size_t lbuf, loff_t * ppos)
{
	int nbytes, maxbytes, bytes_to_do;
	maxbytes = ramdisk_size - *ppos;
	bytes_to_do = maxbytes > lbuf ? lbuf : maxbytes;
	if (bytes_to_do == 0)
		printk(KERN_INFO "Reached end of the device on a read");
	nbytes = bytes_to_do - copy_to_user(buf, file->private_data + *ppos, bytes_to_do);
	*ppos += nbytes;
	printk(KERN_INFO "\n Leaving the   READ function, nbytes=%d, pos=%d\n",
	       nbytes, (int)*ppos);
	return nbytes;
}

static ssize_t
mycdrv_write(struct file *file, const char __user * buf, size_t lbuf,
	     loff_t * ppos)
{
	int nbytes, maxbytes, bytes_to_do;
	maxbytes = ramdisk_size - *ppos;
	bytes_to_do = maxbytes > lbuf ? lbuf : maxbytes;
	if (bytes_to_do == 0)
		printk(KERN_INFO "Reached end of the device on a write");
	nbytes =
	    bytes_to_do - copy_from_user(file->private_data + *ppos, buf, bytes_to_do);
	*ppos += nbytes;
	printk(KERN_INFO "\n Leaving the   WRITE function, nbytes=%d, pos=%d\n",
	       nbytes, (int)*ppos);
	return nbytes;
}

static loff_t mycdrv_lseek(struct file *file, loff_t offset, int orig)
{
	loff_t testpos;
	if (orig == SEEK_SET) {
		testpos = offset;
	} else if (orig == SEEK_CUR) {
		testpos = file->f_pos + offset;
	} else if (orig == SEEK_END) {
		testpos = ramdisk_size + offset;
	} else {
		printk(KERN_INFO "Wrong whence\n");
		return -EINVAL;
	}
	
	if (testpos < 0) {
		printk(KERN_INFO "Seek before file\n");
		return -EINVAL;
	} else if (testpos > ramdisk_size) {
		printk(KERN_INFO "Seek before file\n");
		return -EINVAL;
	}
	
	file->f_pos = testpos;

	printk(KERN_INFO "Seeking to pos=%ld\n", (long)testpos);
	return testpos;
}

static const struct file_operations mycdrv_fops = {
	.owner = THIS_MODULE,
	.read = mycdrv_read,
	.write = mycdrv_write,
	.open = mycdrv_open,
	.release = mycdrv_release,
	.llseek = mycdrv_lseek
};

static int __init my_init(void)
{
	first = MKDEV(my_major, my_minor);
	if (register_chrdev_region(first, count, MYDEV_NAME) < 0) {
		printk(KERN_ERR "failed to register character device region\n");
		return -1;
	}
	if (!(my_cdev = cdev_alloc())) {
		printk(KERN_ERR "cdev_alloc() failed\n");
		unregister_chrdev_region(first, count);
		return -1;
	}
	cdev_init(my_cdev, &mycdrv_fops);
	/*ramdisk = kmalloc(ramdisk_size, GFP_KERNEL);*/

	if (cdev_add(my_cdev, first, count) < 0) {
		printk(KERN_ERR "cdev_add() failed\n");
		cdev_del(my_cdev);
		unregister_chrdev_region(first, count);
		/*kfree(ramdisk);*/
		return -1;
	}

	printk(KERN_INFO "\nSucceeded in registering character device %s\n",
	       MYDEV_NAME);
	return 0;
}

static void __exit my_exit(void)
{
	if (my_cdev)
		cdev_del(my_cdev);
	unregister_chrdev_region(first, count);
	kfree(ramdisk);
	printk(KERN_INFO "\ndevice unregistered\n");
}

module_init(my_init);
module_exit(my_exit);

MODULE_AUTHOR("Jerry Cooperstein");
MODULE_DESCRIPTION("LDD:1.0 s_04/lab3_seek.c");
MODULE_LICENSE("GPL v2");
