
/**
 * File: asgn1.c
 * Date: 13/03/2011
 * Author: Andy Hansen
 * Version: 0.3
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

/* Don't know if this is needed */
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

  printk(KERN_INFO "%s: %d pages freed\n", MYDEV_NAME, asgn1_device.num_pages);
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
  /**
   * Increment process count, if exceeds max_nprocs, return -EBUSY
   *
   * if opened in write-only mode, free all memory pages
   *
   */

  /* Look at lecture 4 slide 16 for what to do here */
  atomic_inc(&asgn1_device.nprocs);
  if (atomic_read(&asgn1_device.nprocs) >
                      atomic_read(&asgn1_device.max_nprocs)) {
      atomic_dec(&asgn1_device.nprocs);
      printk(KERN_WARNING "I'M BLOODY BUSY MATE nprocs is %d\n",
          atomic_read(&asgn1_device.nprocs));
      return -EBUSY;
  }

  /* TODO: Why is this always truncating */
  if (filp->f_flags & O_APPEND) filp->f_pos = asgn1_device.data_size;
  if (filp->f_flags & O_TRUNC &&
      filp->f_flags & O_WRONLY) free_memory_pages();

  return 0; /* success */
}


/**
 * This function releases the virtual disk, but nothing needs to be done
 * in this case. 
 */
int asgn1_release (struct inode *inode, struct file *filp) {
  /**
   * decrement process count
   */
  if (atomic_read(&asgn1_device.nprocs) > 0)
    atomic_dec(&asgn1_device.nprocs);
  return 0;
}


/**
 * This function reads contents of the virtual disk and writes to the user 
 */
ssize_t asgn1_read(struct file *filp, char __user *buf, size_t count,
		 loff_t *f_pos) {
  size_t size_read = 0;     /* size read from virtual disk in this function */
  size_t begin_offset;      /* the offset from the beginning of a page to
                               start reading */
  int begin_page_no = *f_pos / PAGE_SIZE; /* the first page which contains
                                             the requested data */
  int final_page_no = (*f_pos - 1 + count) / PAGE_SIZE;
  int curr_page_no = 0;     /* the current page number */
  size_t curr_size_read;    /* size read from the virtual disk in this round */
  size_t size_to_be_read;   /* size to be read in the current round in 
                               while loop */
  size_t size_not_read;

  page_node *curr;

  /* Maybe should be just f_pos, not filp->f_pos */
  printk(KERN_WARNING "\n%s: Begin read\n", MYDEV_NAME);
  if (*f_pos >= asgn1_device.data_size) return 0;
  if (*f_pos + count > asgn1_device.data_size) count = asgn1_device.data_size-*f_pos;
  printk(KERN_WARNING "reading %d bytes starting at page %d\n", count, curr_page_no);
  /* Seek to the right starting page */
  list_for_each_entry(curr, &asgn1_device.mem_list, list) {
    if (begin_page_no <= curr_page_no && curr_page_no <= final_page_no) {
      if (curr->page == NULL) {
        printk(KERN_WARNING "Attempting to read an unallocated page\n");
        return -1;
      }

      begin_offset = *f_pos % PAGE_SIZE;
      size_to_be_read = min((int)count - size_read, (int)PAGE_SIZE - begin_offset);

      /*printk(KERN_WARNING "offset %d\n", begin_offset);
        printk(KERN_WARNING "attempting to read %d on page %d\n",
                               size_to_be_read, curr_page_no);*/

      size_not_read = copy_to_user(buf + size_read,
          page_address(curr->page) + begin_offset,
          size_to_be_read);

      curr_size_read = size_to_be_read - size_not_read;
      *f_pos += curr_size_read;
      size_read += curr_size_read;
      /*printk(KERN_WARNING "%d/%d bytes read\n", size_read, asgn1_device.data_size);*/

      /* If we didn't read enough in this iteration then stop reading */
      if (size_not_read) break;
    }
    curr_page_no++;
  }

  printk(KERN_WARNING "%s: %d bytes read\n", MYDEV_NAME, size_read);
  return size_read;
}




static loff_t asgn1_lseek (struct file *file, loff_t offset, int cmd)
{
    loff_t testpos;
    size_t buffer_size = asgn1_device.num_pages * PAGE_SIZE;

    /* Can change this to a switch statement */
    switch (cmd) {
      case SEEK_SET:
        testpos = offset;
        break;
      case SEEK_CUR:
        testpos = file->f_pos + offset;
        break;
      case SEEK_END:
        testpos = asgn1_device.data_size + offset;
        break;
      default:
        return -EINVAL;
    }

    if (testpos < 0) testpos = 0;
    else if (testpos > buffer_size) testpos = buffer_size;

    //printk (KERN_INFO "Seeking to pos=%ld\n", (long)testpos);
    file->f_pos = testpos;
    return testpos;
}


/**
 * This function writes from the user buffer to the virtual disk of this
 * module
 */
ssize_t asgn1_write(struct file *filp, const char __user *buf, size_t count,
		  loff_t *f_pos) {
  size_t orig_f_pos = *f_pos;  /* the original file position */
  size_t size_written = 0;  /* size written to virtual disk in this function */
  size_t begin_offset;  /* the offset from the beginning of a page to
			       start writing */
  int begin_page_no = *f_pos / PAGE_SIZE;  /* the first page this function 
                                              should start writing to */
  int final_page_no = (*f_pos-1 + count) / PAGE_SIZE;

  int curr_page_no = 0;     /* the current page number */
  size_t curr_size_written; /* size written to virtual disk in this round */
  size_t size_to_be_written;  /* size to be read in the current round in 
                                 while loop */
  size_t size_not_written;
  
  page_node *curr;

  printk(KERN_WARNING "\n%s: Begin write\n", MYDEV_NAME);

  /* Allocate all the pages we are going to need and add 
   * them to the list of memory pages */
  while (asgn1_device.num_pages <= final_page_no) {
    if (asgn1_device.num_pages == 16) return -ENOMEM;
    curr = kmalloc(sizeof(page_node), GFP_KERNEL);
    curr->page = alloc_page(GFP_KERNEL);
    if (NULL == curr->page) {
      printk(KERN_WARNING "Not enough memory left\n");
      return -ENOMEM;
    }
    list_add_tail(&(curr->list), &asgn1_device.mem_list);
    asgn1_device.num_pages++;
  }
  //printk(KERN_INFO "%s: %d pages total\n", MYDEV_NAME, asgn1_device.num_pages);
  //printk(KERN_INFO "%s: %u to be written\n", MYDEV_NAME, count / PAGE_SIZE);

  list_for_each_entry(curr, &asgn1_device.mem_list, list) {
    if (begin_page_no <= curr_page_no && curr_page_no <= final_page_no) {
      if (curr->page == NULL) {
        printk(KERN_INFO "%s: no page ready to be written to\n", MYDEV_NAME);
        return -1;
      }
      begin_offset = *f_pos % PAGE_SIZE;
      /* Make sure the size we are about to write fits within a page */
      size_to_be_written = min((int) count - size_written, (int) PAGE_SIZE - begin_offset);
      /*printk(KERN_INFO "%s: writing %d bytes to page %d from offset %d\n",
        MYDEV_NAME, size_to_be_written, curr_page_no, begin_offset);*/

      size_not_written = copy_from_user(page_address(curr->page) + begin_offset,
          buf + size_written,
          size_to_be_written);

      /* Update the file position and the total amount written. If the write was
       * not successful then break out of the loop to prevent any more writes.
       * The user can recall the write function and try again. */
      curr_size_written = size_to_be_written - size_not_written;
      *f_pos += curr_size_written;
      size_written += curr_size_written;
      /*printk(KERN_INFO "%s: total written %d\n", MYDEV_NAME, size_written);*/
      if (size_not_written) break;
    }
    curr_page_no++;
  }

  /* TODO: may not be needed */
  filp->f_pos = *f_pos;
  asgn1_device.data_size = max(asgn1_device.data_size,
                               orig_f_pos + size_written);
  printk(KERN_INFO "%s: %d bytes written\n", MYDEV_NAME, size_written);
  return (size_written > 0) ? size_written : -EFAULT;
}

#define SET_NPROC_OP 1
#define TEM_SET_NPROC _IOW(MYIOC_TYPE, SET_NPROC_OP, int) 

#define GET_MEMSIZE_OP 2
#define TEM_GET_MEMSIZE _IOW(MYIOC_TYPE, GET_MEMSIZE_OP, int) 

#define GET_CUR_PROCS 3
#define TEM_GET_CUR_PROCS _IOW(MYIOC_TYPE, GET_CUR_PROCS, int) 
/**
 * The ioctl function, which nothing needs to be done in this case.
 */
long asgn1_ioctl (struct file *filp, unsigned cmd, unsigned long arg) {
  int nr;
  int new_nprocs;
  int result;

  if (_IOC_TYPE(cmd) != MYIOC_TYPE) return -EINVAL;
  nr = _IOC_NR(cmd);

  switch (nr) {
    case SET_NPROC_OP:
      printk(KERN_WARNING "Setting max nprocs\n");
      result = copy_from_user((int *) &new_nprocs, (int *) arg, sizeof (arg));
      if (result) return -EINVAL;
      if (new_nprocs < atomic_read(&asgn1_device.nprocs)) return -EINVAL;
      atomic_set(&asgn1_device.max_nprocs, new_nprocs);
      printk(KERN_WARNING "%d is new nprocs\n", new_nprocs);
      return 0;
    case GET_MEMSIZE_OP:
      printk(KERN_WARNING "Getting memory size\n");
      return asgn1_device.num_pages * PAGE_SIZE;
      break;
    case GET_CUR_PROCS:
      printk(KERN_WARNING "Getting number of pages allocated\n");
      return atomic_read(&asgn1_device.nprocs);
    default:
      printk(KERN_WARNING "Wrong command\n");
      return -EINVAL;
  }
  return -ENOTTY;
}


/**
 * Displays information about current status of the module,
 * which helps debugging.
 */
int asgn1_read_procmem(char *buf, char **start, off_t offset, int count,
		     int *eof, void *data) {
  /* stub */
  int result;

  result = sprintf(buf, "Amount written: %d, Ramdisk size: %ld\n",
      asgn1_device.data_size, PAGE_SIZE * asgn1_device.num_pages);
  return result;
}


static int asgn1_mmap (struct file *filp, struct vm_area_struct *vma)
{
    unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
    unsigned long len = vma->vm_end - vma->vm_start;
    unsigned long ramdisk_size = asgn1_device.num_pages * PAGE_SIZE;
    page_node *curr;
    unsigned long index = 0;
    unsigned long endpage = offset + len / PAGE_SIZE;
    

    /*printk(KERN_WARNING "offset: %lu, len: %lu, end page: %lu, pf_off: %lu\n", offset, len, endpagetest, vma->vm_pgoff);

    printk(KERN_WARNING "num pages: %d, page size: %d, device size: %d\n", asgn1_device.num_pages, PAGE_SIZE, asgn1_device.data_size);
    printk(KERN_WARNING "offset: %lu, len: %lu, ramdisk_size: %lu\n", offset,
                                                                    len,
                                                                    ramdisk_size);*/
    if (offset + len > ramdisk_size) return -1;
    list_for_each_entry(curr, &asgn1_device.mem_list, list) {
      /* Add a check we haven't gone too far */
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
  /* COMPLETE ME */
  /**
   * set nprocs and max_nprocs of the device
   *
   * allocate major number
   * allocate cdev, and set ops and owner field 
   * add cdev
   * initialize the page list
   * create proc entries
   */

  atomic_set(&asgn1_device.nprocs, 0);
  atomic_set(&asgn1_device.max_nprocs, 1);
  result = alloc_chrdev_region(&asgn1_device.dev, asgn1_minor,
                                       asgn1_dev_count, MYDEV_NAME);
  if (result < 0) {
    printk(KERN_WARNING "%s: couldn't get a major number\n", MYDEV_NAME);
    goto fail_device;
  }
  /* Set the major number variable*/
  asgn1_major = MAJOR(asgn1_device.dev);
  printk(KERN_INFO "%s: allocated the major number %d\n", MYDEV_NAME, asgn1_major);

  /* Set up cdev internal structure */
  asgn1_device.cdev = cdev_alloc();
  asgn1_device.cdev->ops = &asgn1_fops;
  /*cdev_init(asgn1_device.cdev, &asgn1_fops);*/
  asgn1_device.cdev->owner = THIS_MODULE;

  /* Register the device */
  result = cdev_add(asgn1_device.cdev, asgn1_device.dev, asgn1_dev_count);
  if (result < 0) {
    printk(KERN_WARNING "%s: unable to add cdev\n", MYDEV_NAME);
    goto fail_device;
  }

  /* Initialise the list head */
  INIT_LIST_HEAD(&asgn1_device.mem_list);

  /* TODO: make sure this is the right thing work out where to put fops*/
  proc_entry = create_proc_entry("asgn1", S_IRUGO | S_IWUSR, NULL);
  if (!proc_entry) {
    printk(KERN_WARNING "%s: failed making the proc\n", "proc_entry");
    result = -1;
    goto fail_device;
  }
  proc_entry->read_proc = asgn1_read_procmem;

  asgn1_device.class = class_create(THIS_MODULE, MYDEV_NAME);
  if (IS_ERR(asgn1_device.class)) {
  }

  asgn1_device.device = device_create(asgn1_device.class, NULL, 
                                      asgn1_device.dev, "%s", MYDEV_NAME);
  if (IS_ERR(asgn1_device.device)) {
    printk(KERN_WARNING "%s: can't create udev device\n", MYDEV_NAME);
    result = -ENOMEM;
    goto fail_device;
  }
  
  printk(KERN_WARNING "set up udev entry\n");
  printk(KERN_WARNING "Hello world from %s\n", MYDEV_NAME);
  return 0;

  /* cleanup code called when any of the initialization steps fail */
fail_device:
   class_destroy(asgn1_device.class);

  /* COMPLETE ME */
  /* PLEASE PUT YOUR CLEANUP CODE HERE, IN REVERSE ORDER OF ALLOCATION */

   /* proc */
   remove_proc_entry("asgn1", NULL);
   /* remove list head */
   /* de-register the device */
   cdev_del(asgn1_device.cdev);
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
  printk(KERN_WARNING "cleaned up udev entry\n");
  
  remove_proc_entry("asgn1", NULL);
  free_memory_pages();
  cdev_del(asgn1_device.cdev);
  /* unregister device */
  unregister_chrdev_region(asgn1_device.dev, asgn1_dev_count);
  printk(KERN_WARNING "Good bye from %s\n", MYDEV_NAME);
}


module_init(asgn1_init_module);
module_exit(asgn1_exit_module);
