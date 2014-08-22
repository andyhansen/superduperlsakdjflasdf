
/**
 * File: asgn1.c
 * Date: 13/03/2011
 * Author: Andy Hansen
 * Version: 0.4
 *
 * This is a module which serves as a virtual ramdisk which disk size is
 * limited by the amount of memory available and serves as the requirement for
 * COSC440 assignment 1 in 2012.
 *
 * Note: multiple devices and concurrent modules are not supported in this
 *       version.
 */

/* This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/list.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/device.h>

#define MYDEV_NAME "asgn1"
#define MYIOC_TYPE 'k'
#define MYPROC_NAME "asgn1"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andy Hansen");
MODULE_DESCRIPTION("COSC440 asgn1");


/**
 * The node structure for the memory page linked list.
 */ 
typedef struct page_node_rec {
  struct list_head list;
  struct page *page;
} page_node;

typedef struct asgn1_dev_t {
  dev_t dev;            /* the device */
  struct cdev *cdev;
  struct list_head mem_list; 
  int num_pages;        /* number of memory pages this module currently holds */
  size_t data_size;     /* total data size in this module */
  atomic_t nprocs;      /* number of processes accessing this device */ 
  atomic_t max_nprocs;  /* max number of processes accessing this device */
  struct kmem_cache *cache;      /* cache memory */
  struct class *class;     /* the udev class */
  struct device *device;   /* the udev device node */
} asgn1_dev;

asgn1_dev asgn1_device;

static struct proc_dir_entry *proc_entry;

int asgn1_major = 0;                      /* major number of module */  
int asgn1_minor = 0;                      /* minor number of module */
int asgn1_dev_count = 1;                  /* number of devices */


/**
 * This function frees all memory pages held by the module.
 */
void free_memory_pages(void) {
  page_node *curr;
  struct list_head *ptr;
  struct list_head *tmp;

  /* Loop through the page list */
  list_for_each_safe(ptr, tmp, &asgn1_device.mem_list) {
    /* If a page has been allocated, free it. The list node is then deleted */
    curr = list_entry(ptr, page_node, list);
    if (curr->page) 
      __free_page(curr->page);
    list_del(&curr->list);
    kfree(curr);
  }

  /* Reset the number of pages and data size to 0 since there is nothing in the driver
   * anymore */
  asgn1_device.num_pages = 0;
  asgn1_device.data_size = 0;
}


/**
 * This function opens the virtual disk, if it is opened in the write-only
 * mode, all memory pages will be freed.
 */
int asgn1_open(struct inode *inode, struct file *filp) {

  /* Increment the process count and check it's not greater than
   * the maximum allowed procs. If it is then decrement it back
   * to what it was and return EBUSY. */
  atomic_inc(&asgn1_device.nprocs);
  if (atomic_read(&asgn1_device.nprocs) >
      atomic_read(&asgn1_device.max_nprocs)) {
    atomic_dec(&asgn1_device.nprocs);
    return -EBUSY;
  }

  if (filp->f_flags & O_APPEND) filp->f_pos = asgn1_device.data_size;

  /* Only truncate the file if it is opened for writing, and is expected
   * to be truncated */
  if (filp->f_flags & O_TRUNC &&
      filp->f_flags & O_WRONLY) free_memory_pages();

  return 0; /* success */
}


/**
 * This function releases the virtual disk, but nothing needs to be done
 * in this case. 
 */
int asgn1_release (struct inode *inode, struct file *filp) {
  if (atomic_read(&asgn1_device.nprocs) > 0)
    atomic_dec(&asgn1_device.nprocs);
  return 0;
}


/**
 * This function reads contents of the virtual disk and writes to the user space.
 */
ssize_t asgn1_read(struct file *filp, char __user *buf, size_t count,
    loff_t *f_pos) {
  size_t size_read = 0;     /* size read from virtual disk in this function */
  size_t begin_offset;      /* the offset from the beginning of a page to
                               start reading */
  int begin_page_no = (*f_pos - 1) / PAGE_SIZE; /* the first page which contains
                                             the requested data */

  /* Subtract 1 to stop it from grabbing one extra page in the case that it
   * is reading an exact page */
  int final_page_no;
  int curr_page_no = 0;     /* the current page number */
  size_t curr_size_read;    /* size read from the virtual disk in this round */
  size_t size_to_be_read;   /* size to be read in the current round in 
                               while loop */
  size_t size_not_read;

  page_node *curr;

  if (*f_pos >= asgn1_device.data_size) return 0;
  if (*f_pos + count > asgn1_device.data_size) count = asgn1_device.data_size-*f_pos;
  final_page_no= (*f_pos - 1 + count) / PAGE_SIZE; /* get the final page num. 1 is subtracted to prevent
                                                      cases that need an exact page amount from getting
                                                      one too many pages */
  list_for_each_entry(curr, &asgn1_device.mem_list, list) {
    if (begin_page_no <= curr_page_no && curr_page_no <= final_page_no) {
      if (curr->page == NULL) {
        printk(KERN_WARNING "Attempting to read an unallocated page\n");
        return -1;
      }

      begin_offset = *f_pos % PAGE_SIZE;
      size_to_be_read = min((long)count - size_read, (long)PAGE_SIZE - begin_offset);

      size_not_read = copy_to_user(buf + size_read,
          page_address(curr->page) + begin_offset,
          size_to_be_read);

      /* Update the file position and the total amount read. If the copy was
       * not successful then break out of the loop to prevent any more reads.
       * The user can recall the read function to complete it. */
      curr_size_read = size_to_be_read - size_not_read;
      *f_pos += curr_size_read;
      size_read += curr_size_read;

      /* If we didn't read all we wanted to this iteration, stop reading and
       * return what we have read so far. */
      if (size_not_read) break;
    }
    curr_page_no++;
  }
  //printk(KERN_WARNING "%s: %d bytes read\n", MYDEV_NAME, size_read);
  filp->f_pos = *f_pos;
  /* If the read function wasn't able to read anything then return an error */
  return (size_read > 0) ? size_read : -EFAULT;
}

/**
 * This function repositions the offset of the open file associated with the 
 * file descriptor fd. */
static loff_t asgn1_lseek (struct file *file, loff_t offset, int cmd)
{
  loff_t testpos;
  size_t buffer_size = asgn1_device.num_pages * PAGE_SIZE;

  switch (cmd) {
    case SEEK_SET:
      testpos = offset;
      break;
    case SEEK_CUR:
      testpos = file->f_pos + offset;
      break;
    case SEEK_END:
      testpos = asgn1_device.data_size - offset;
      break;
    default:
      printk(KERN_WARNING "%s: Invalid cmd given to asgn1_lseek.\n", MYDEV_NAME);
      return -EINVAL;
  }

  if (testpos < 0) testpos = 0;
  else if (testpos > buffer_size) testpos = buffer_size;

  file->f_pos = testpos;
  return testpos;
}


/**
 * This function writes from the user buffer to the virtual disk of this
 * module.
 */
ssize_t asgn1_write(struct file *filp, const char __user *buf, size_t count,
    loff_t *f_pos) {
  size_t orig_f_pos = *f_pos;  /* the original file position */
  size_t size_written = 0;  /* size written to virtual disk in this function */
  size_t begin_offset;  /* the offset from the beginning of a page to
                           start writing */
  int begin_page_no = (*f_pos - 1) / PAGE_SIZE;  /* the first page this function 
                                              should start writing to */
  int final_page_no = (*f_pos - 1 + count) / PAGE_SIZE; /* index of the last
                                                           page which will be
                                                           written to */
  int curr_page_no = 0;     /* the current page number */
  size_t curr_size_written; /* size written to virtual disk in this round */
  size_t size_to_be_written;  /* size to be read in the current round in 
                                 while loop */
  size_t size_not_written;

  page_node *curr;


  /* Allocate all the pages we are going to need and add 
   * them to the list of memory pages */
  while (asgn1_device.num_pages <= final_page_no) {
    curr = kmalloc(sizeof(page_node), GFP_KERNEL);
    if (curr == NULL) {
      printk(KERN_WARNING "%s: Not enough memory to allocate anymore pages\n", MYDEV_NAME);
      return -ENOMEM;
    }
    curr->page = alloc_page(GFP_KERNEL);
    if (curr->page == NULL) {
      printk(KERN_WARNING "%s: Not enough memory to allocate anymore pages\n", MYDEV_NAME);
      return -ENOMEM;
    }
    list_add_tail(&(curr->list), &asgn1_device.mem_list);
    asgn1_device.num_pages++;
  }

  list_for_each_entry(curr, &asgn1_device.mem_list, list) {
    /* Only write on the relevant pages */
    if (begin_page_no <= curr_page_no && curr_page_no <= final_page_no) {
      if (curr->page == NULL) {
        printk(KERN_INFO "%s: Trying to write to an unallocated page\n", MYDEV_NAME);
        return -1;
      }
      /* Get the offset for this iteration */
      begin_offset = *f_pos % PAGE_SIZE;
      /* Make sure the size we are about to write fits within a page */
      size_to_be_written = min((long) count - size_written, (long) PAGE_SIZE - begin_offset);

      size_not_written = copy_from_user(page_address(curr->page) + begin_offset,
          buf + size_written,
          size_to_be_written);

      /* Update the file position and the total amount written. If the copy was
       * not successful then break out of the loop to prevent any more writes.
       * The user can recall the write function to complete it. */
      curr_size_written = size_to_be_written - size_not_written;
      *f_pos += curr_size_written;
      size_written += curr_size_written;
      if (size_not_written) break;
    }
    curr_page_no++;
  }

  filp->f_pos = *f_pos;
  asgn1_device.data_size = max(asgn1_device.data_size,
      orig_f_pos + size_written);
  //printk(KERN_INFO "%s: %d bytes written\n", MYDEV_NAME, size_written);
  /* If the write function wasn't able to write anything then return an error */
  return (size_written > 0) ? size_written : -EFAULT;
}

#define SET_NPROC_OP 1
#define TEM_SET_NPROC _IOW(MYIOC_TYPE, SET_NPROC_OP, int) 

#define GET_CUR_PROCS_OP 2
#define TEM_GET_CUR_PROCS _IOR(MYIOC_TYPE, GET_CUR_PROCS_OP, int)

#define RESET_DEVICE_OP 3
#define TEM_RESET_DEVICE _IO(MYIOC_TYPE, RESET_DEVICE_OP)

/**
 * The ioctl function, which nothing needs to be done in this case.
 * This module supports 3 options by giving it the following commands:
 * 1 - The integer you pass with be used to set the new max processes allowed.
 *     You cannot set it to a number lower than the current amount of processes.
 *
 * 2 - Can be used to retrive the current amount of processes using the device.
 * 3 - Can be used to free all of the memory pages used by the device.
 */
long asgn1_ioctl (struct file *filp, unsigned cmd, unsigned long arg) {
  int nr;
  int new_nprocs;
  int pages_allocated;
  int result;

  if (_IOC_TYPE(cmd) != MYIOC_TYPE) return -EINVAL;
  nr = _IOC_NR(cmd);

  switch (nr) {
    case SET_NPROC_OP:
      result = get_user(new_nprocs, (int *) arg);
      if (result) {
        printk(KERN_WARNING "%s: Error when retriving the new nprocs value\n", MYDEV_NAME);
        return -EINVAL;
      }
      if (new_nprocs < atomic_read(&asgn1_device.nprocs)) return -EINVAL;
      atomic_set(&asgn1_device.max_nprocs, new_nprocs);
      return 0;
    case GET_CUR_PROCS_OP:
      pages_allocated = atomic_read(&asgn1_device.nprocs);
      result = put_user(pages_allocated, (int *) arg);
      if (result) {
        printk(KERN_WARNING
            "%s: Error when copying the procs value to userspace\n", MYDEV_NAME);
        return -EINVAL;
      }
      return 0;
    case RESET_DEVICE_OP:
      if (atomic_read(&asgn1_device.nprocs) > 1) return -EINVAL;
      free_memory_pages();
      return 0;
    default:
      printk(KERN_WARNING "ioctl command doesn't match any available\n");
      return -EINVAL;
  }
}


/**
 * Displays information about current status of the module,
 * which helps debugging. This message will tell the caller
 * how many bytes they have written to the device, how much 
 * space it has been currently allocated.
 */
int asgn1_read_procmem(char *buf, char **start, off_t offset, int count,
    int *eof, void *data) {
  int result;
  /* 65 is the largest amount of space this print statement can take up */
  if (60 > count) {
    printk(KERN_WARNING "%s: Buffer provided needs to have 60 or more bytes of space\n", MYDEV_NAME);
    return -EINVAL;
  }
  result =
      sprintf(buf, "Bytes written: %d, Total allocated space in bytes: %ld\n",
      asgn1_device.data_size, PAGE_SIZE * asgn1_device.num_pages);
  *eof = 1;
  return result;
}


/**
 * Creates a new mapping in the virtual address space of the calling process.
 */
static int asgn1_mmap (struct file *filp, struct vm_area_struct *vma)
{
  /* offset is in pages, not bytes */
  unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
  unsigned long len = vma->vm_end - vma->vm_start;
  unsigned long ramdisk_size = asgn1_device.num_pages * PAGE_SIZE;
  page_node *curr;
  unsigned long index = 0;
  unsigned long endpage = offset + (len / PAGE_SIZE);

  /* check that they don't want to map past memory that we have available */
  if ((offset * PAGE_SIZE) + len > ramdisk_size) {
    printk(KERN_WARNING "Attempting to map past available memory\n");
    return -EINVAL;
  }
  list_for_each_entry(curr, &asgn1_device.mem_list, list) {
    /* Only map the relevant range of pages */
    if (index >= offset && index < endpage) {
      if (remap_pfn_range(vma, vma->vm_start + PAGE_SIZE * (index - vma->vm_pgoff),
            page_to_pfn(curr->page), PAGE_SIZE, vma->vm_page_prot))
        return -EAGAIN;
    }
    index++;
  }
  return 0;
}


struct file_operations asgn1_fops = {
  .owner = THIS_MODULE,
  .read = asgn1_read,
  .write = asgn1_write,
  .unlocked_ioctl = asgn1_ioctl,
  .open = asgn1_open,
  .mmap = asgn1_mmap,
  .release = asgn1_release,
  .llseek = asgn1_lseek
};


/**
 * Initialise the module and create the master device
 */
int __init asgn1_init_module(void){
  int result; 

  atomic_set(&asgn1_device.nprocs, 0);
  atomic_set(&asgn1_device.max_nprocs, 1);
  result = alloc_chrdev_region(&asgn1_device.dev, asgn1_minor,
      asgn1_dev_count, MYDEV_NAME);
  if (result < 0) {
    printk(KERN_WARNING "%s: Couldn't get a major number\n", MYDEV_NAME);
    goto fail_dev;
  }
  /* Set the major number variable*/
  asgn1_major = MAJOR(asgn1_device.dev);
  //printk(KERN_INFO "%s: Allocated the major number %d\n", MYDEV_NAME, asgn1_major);

  /* Set up cdev internal structure */
  asgn1_device.cdev = cdev_alloc();
  asgn1_device.cdev->ops = &asgn1_fops;
  asgn1_device.cdev->owner = THIS_MODULE;

  /* Register the device */
  result = cdev_add(asgn1_device.cdev, asgn1_device.dev, asgn1_dev_count);
  if (result < 0) {
    printk(KERN_WARNING "%s: Unable to add cdev\n", MYDEV_NAME);
    goto fail_cdev;
  }

  /* Initialise the list head */
  INIT_LIST_HEAD(&asgn1_device.mem_list);

  /* Create the proc entry and add its read method */
  proc_entry = create_proc_entry(MYPROC_NAME, 0, NULL);
  if (!proc_entry) {
    printk(KERN_WARNING "%s: failed making the proc entry\n", MYDEV_NAME);
    result = -1;
    goto fail_proc;
  }
  //printk(KERN_WARNING "%s: proc created successfully\n", MYDEV_NAME);
  proc_entry->read_proc = asgn1_read_procmem;

  asgn1_device.class = class_create(THIS_MODULE, MYDEV_NAME);
  if (IS_ERR(asgn1_device.class)) {
    printk(KERN_WARNING "%s: can't create class\n", MYDEV_NAME);
    result = -ENOMEM;
    goto fail_class;
  }

  asgn1_device.device = device_create(asgn1_device.class, NULL, 
      asgn1_device.dev, "%s", MYDEV_NAME);
  if (IS_ERR(asgn1_device.device)) {
    printk(KERN_WARNING "%s: can't create udev device\n", MYDEV_NAME);
    result = -ENOMEM;
    goto fail_device;
  }

  printk(KERN_WARNING "%s: set up udev entry\n", MYDEV_NAME);
  return 0;

  /* cleanup code called when any of the initialization steps fail */
fail_device:
fail_class:
  class_destroy(asgn1_device.class);
fail_proc:
  /* remove the proc proc */
  if (proc_entry) remove_proc_entry(MYPROC_NAME, NULL);
fail_cdev:
  /* de-register the device */
  cdev_del(asgn1_device.cdev);
fail_dev:
  /* unregister device */
  unregister_chrdev_region(asgn1_device.dev, asgn1_dev_count);
  return result;
}

/**
 * Finalise the module
 */
void __exit asgn1_exit_module(void){
  device_destroy(asgn1_device.class, asgn1_device.dev);
  class_destroy(asgn1_device.class);

  free_memory_pages();
  if (proc_entry) remove_proc_entry(MYPROC_NAME, NULL);
  /* de-register the device */
  cdev_del(asgn1_device.cdev);
  /* unregister device */
  unregister_chrdev_region(asgn1_device.dev, asgn1_dev_count);
  printk(KERN_WARNING "%s: dismounted.\n", MYDEV_NAME);
}


module_init(asgn1_init_module);
module_exit(asgn1_exit_module);
