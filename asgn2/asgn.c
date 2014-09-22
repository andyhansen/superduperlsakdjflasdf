/**
 * File: asgn2.c
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
#include <linux/sched.h>
#include "gpio.h"

#define MYDEV_NAME "asgn2"
#define MYIOC_TYPE 'k'

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andy Hansen");
MODULE_DESCRIPTION("COSC440 asgn2");


/**
 * The node structure for the memory page linked list.
 */ 
typedef struct page_node_rec {
  struct list_head list;
  struct page *page;
} page_node;

typedef struct file_node_rec {
  struct list_head flist;
  struct list_head plist;
  size_t data_size;
  int head;
  int tail;
  int num_pages;
} file_node;

typedef struct asgn2_dev_t {
  dev_t dev;            /* the device */
  struct cdev *cdev;
  struct list_head file_list;
  int num_pages;        /* number of memory pages this module currently holds */
  size_t data_size;     /* total data size in this module */
  atomic_t nprocs;      /* number of processes accessing this device */ 
  atomic_t max_nprocs;  /* max number of processes accessing this device */
  atomic_t num_files;
  struct kmem_cache *cache;      /* cache memory */
  struct class *class;     /* the udev class */
  struct device *device;   /* the udev device node */
} asgn2_dev;

struct cbuf_t {
  char* buf;
  size_t head;
  size_t count;
  size_t capacity;
} cbuf;

file_node *incomplete_file;

asgn2_dev asgn2_device;

int asgn2_major = 0;                      /* major number of module */  
int asgn2_minor = 0;                      /* minor number of module */
int asgn2_dev_count = 1;                  /* number of devices */

u8 top_half_byte;
int second_half = 0;

DECLARE_WAIT_QUEUE_HEAD(wq);

DEFINE_MUTEX(file_list_mutex);


/* Allocate a new empty file */
file_node* allocate_empty_file_node(void) {
  file_node *node = kmalloc(sizeof(file_node), GFP_KERNEL);
  INIT_LIST_HEAD(&node->plist);
  node->tail = 0;
  node->head = 0;
  node->data_size = 0;
  node->num_pages = 0;
  return node;
}

/* Remove the file at the front of the list */
file_node* remove_first_file(void) {
  file_node *node = NULL;
  if (mutex_lock_interruptible(&file_list_mutex)) return NULL;
  /* if there are no files to read, or the ptr is not correct then
   * release the lock and return null */
  if (atomic_read(&asgn2_device.num_files) == 0 ||
      asgn2_device.file_list.next == NULL) {
    mutex_unlock(&file_list_mutex);
    return NULL;
  }
  node = list_entry(asgn2_device.file_list.next, file_node, flist);
  list_del(asgn2_device.file_list.next);
  atomic_dec(&asgn2_device.num_files);
  mutex_unlock(&file_list_mutex);
  return node;
}

/*
 * Adds the passed in file node to the end of the file node list 
 */
void add_to_file_list(file_node *node) {
  if (mutex_lock_interruptible(&file_list_mutex)) 
    return;
  list_add_tail(&(node->flist), &asgn2_device.file_list);
  atomic_inc(&asgn2_device.num_files);
  mutex_unlock(&file_list_mutex);
}


/*
 * Frees the passed in file node
 */
void free_file_node(file_node *node) {
  page_node *curr;
  if (node == NULL) return;
  while (!list_empty(&node->plist)) {
    curr = list_entry(node->plist.next, page_node, list);
    if (NULL != curr->page) __free_page(curr->page);
    list_del(node->plist.next);
    if (NULL != curr) kmem_cache_free(asgn2_device.cache, curr);
  }
  asgn2_device.num_pages -= node->num_pages;
  kfree(node);
}

/**
 * Frees all of the file nodes resets the variables tracking
 * the size of device and pages allocated.
 */
void free_file_nodes(void) {
  file_node *node;

  while (!list_empty(&asgn2_device.file_list)) {
    node = list_entry(asgn2_device.file_list.next, file_node, flist);
    free_file_node(node);
    list_del(asgn2_device.file_list.next);
  }
  asgn2_device.data_size = 0;
  asgn2_device.num_pages = 0;
  atomic_set(&asgn2_device.num_files, 0);
}

/**
 * This function opens the virtual disk, if it is opened in the write-only
 * mode, all memory pages will be freed.
 */
int asgn2_open(struct inode *inode, struct file *filp) {
  file_node *node;
  if (filp->f_flags & O_WRONLY) {
    printk(KERN_WARNING "%s: can't be opened for writing\n", MYDEV_NAME);
    return -EINVAL;
  }
  /* If there is already a reader operating, or there isn't a file for us to read 
   * yet then go to sleep until we can read */
  if (atomic_read(&asgn2_device.nprocs) >= atomic_read(&asgn2_device.max_nprocs)
      ||atomic_read(&asgn2_device.num_files) <= 0)
    if (wait_event_interruptible_exclusive(wq, atomic_read(&asgn2_device.num_files)))
      return -ERESTARTSYS;
  /* Set the private data field of the file to be a
   * pointer to the file from the queue, if we waited to get
   * a file then return an error, though this shouldn't happen */
  node = remove_first_file();
  if (node == NULL) {
    printk(KERN_WARNING "Couldn't get a page\n");
    return -EBUSY;
  }
  /* set the private data of this file to a unique file node */
  filp->private_data = node;
  atomic_inc(&asgn2_device.nprocs);
  return 0; /* success */
}

/**
 * This function releases the virtual disk, but nothing needs to be done
 * in this case. 
 */
int asgn2_release (struct inode *inode, struct file *filp) {
  atomic_dec(&asgn2_device.nprocs);
  free_file_node(filp->private_data);
  return 0;
}


/**
 * This function reads contents of the virtual disk and writes to the user 
 */
ssize_t asgn2_read(struct file *filp, char __user *buf, size_t count,
		 loff_t *f_pos) {
  size_t size_read = 0;     /* size read from virtual disk in this function */
  size_t begin_offset;      /* the offset from the beginning of a page to
			       start reading */
  int begin_page_no = *f_pos / PAGE_SIZE; /* the first page which contains
					     the requested data */
  int curr_page_no = 0;     /* the current page number */
  size_t curr_size_read;    /* size read from the virtual disk in this round */
  size_t size_to_be_read;   /* size to be read in the current round in 
			       while loop */

  struct list_head *ptr;
  page_node *curr;
  file_node *node = filp->private_data;
  if (node == NULL || node->plist.next == NULL) {
    /* In theory these two shouldn't occur, but just as a precaution */
    printk(KERN_WARNING "File is corrupted, exiting now\n");
    return 0;
  }
  ptr = node->plist.next;

  if (*f_pos >= node->data_size) return 0;
  count = min(node->data_size - (size_t)*f_pos, count);

  while (size_read < count) {
    curr = list_entry(ptr, page_node, list);
    if (ptr == &node->plist) {
      /* We have already passed the end of the data area of the
         ramdisk, so we quit and return the size we have read
         so far */
      printk(KERN_WARNING "invalid virtual memory access\n");
      return size_read;
    } else if (curr_page_no < begin_page_no) {
      /* haven't reached the page occupued by *f_pos yet, 
         so move on to the next page */
      ptr = ptr->next;
      curr_page_no++;
    } else {
      /* this is the page to read from */
      begin_offset = *f_pos % PAGE_SIZE;
      size_to_be_read = (size_t)min((size_t)(count - size_read), 
				    (size_t)(PAGE_SIZE - begin_offset));

      do {
        curr_size_read = size_to_be_read - 
	  copy_to_user(buf + size_read, 
	  	       page_address(curr->page) + begin_offset,
		       size_to_be_read);
        size_read += curr_size_read;
        *f_pos += curr_size_read;
        node->head += curr_size_read;
        begin_offset += curr_size_read;
        size_to_be_read -= curr_size_read;
      } while (curr_size_read > 0);

      curr_page_no++;
      ptr = ptr->next;
    }
  }
  /* Get the new datasize my adding the new size minus the old size of what
   * we just read */
  asgn2_device.data_size += (node->tail - node->head) - node->data_size;
  node->data_size = node->tail - node->head;
  return size_read;
}

/**
 * This function writes from the user buffer to the virtual disk of this
 * module
 */
ssize_t asgn2_write(char* to_write, int count) {
  size_t size_written = 0;  /* size written to virtual disk in this function */
  size_t begin_offset;      /* the offset from the beginning of a page to
			       start writing */
  int begin_page_no;
  /* the first page this finction
					      should start writing to */

  int curr_page_no = 0;     /* the current page number */
  size_t curr_size_written; /* size written to virtual disk in this round */
  size_t size_to_be_written;  /* size to be read in the current round in
				 while loop */
  
  struct list_head *ptr;
  page_node *curr;
  /* Use the currrently unfinished file to store all the pages */
  file_node *node = incomplete_file;
  ptr = node->plist.next;
  begin_page_no = node->tail / PAGE_SIZE;  

  while (size_written < count) {
    curr = list_entry(ptr, page_node, list);
    if (ptr == &node->plist) {
      /* not enough page, so add page */
      curr = kmem_cache_alloc(asgn2_device.cache, GFP_KERNEL);
      if (NULL == curr) {
        printk(KERN_WARNING "Not enough memory left\n");
        break;
      }
      curr->page = alloc_page(GFP_KERNEL);
      if (NULL == curr->page) {
        printk(KERN_WARNING "Not enough memory left\n");
              kmem_cache_free(asgn2_device.cache, curr);
        break;
      }
      list_add_tail(&(curr->list), &node->plist);
      node->num_pages++;
      asgn2_device.num_pages++;
      ptr = node->plist.prev;
    } else if (curr_page_no < begin_page_no) {
      /* move on to the next page */
      ptr = ptr->next;
      curr_page_no++;
    } else {
      do {
      /* this is the page to write to */
        begin_offset = node->tail % PAGE_SIZE;
        size_to_be_written = (size_t)min((size_t)(count - size_written),
				       (size_t)(PAGE_SIZE - begin_offset));
        curr_size_written = size_to_be_written;
        /* it is assumed that memmove was successful, shouldn't be 
         * in a situation where it can fail */
        memmove(page_address(curr->page) + begin_offset,
	  	         to_write + size_written, size_to_be_written);
        size_written += curr_size_written;
        begin_offset += curr_size_written;
        node->tail += curr_size_written;
        size_to_be_written -= curr_size_written;
      } while (size_to_be_written > 0);
      curr_page_no++;
      ptr = ptr->next;
    }
  }
  /* If the last character is a the null terminator then
   * we have written the last part of this file. We then 
   * decrement the tail by one so we don't include the null 
   * terminator as part of the file then add it to the list
   * of files */
  if (*(to_write + size_written - 1) == '\0') {
    node->tail--;
    add_to_file_list(node);
    incomplete_file = allocate_empty_file_node();
    //printk(KERN_WARNING "Waking up a reader\n");
    wake_up_interruptible_nr(&wq, 1);
  }
  /* Get the new datasize my adding the new size minus the old size of what
   * we just read */
  asgn2_device.data_size += (node->tail - node->head) - node->data_size;
  node->data_size = node->tail - node->head;
  return size_written;
}

#define SET_NPROC_OP 1
#define TEM_SET_NPROC _IOW(MYIOC_TYPE, SET_NPROC_OP, int) 

/**
 * The ioctl function, which nothing needs to be done in this case.
 */
long asgn2_ioctl (struct file *filp, unsigned cmd, unsigned long arg) {
  int nr;
  int new_nprocs;
  int result;

  if (_IOC_TYPE(cmd) != MYIOC_TYPE) {
    printk(KERN_WARNING "%s: magic number does not match\n", MYDEV_NAME);
    return -EINVAL;
  }

  nr = _IOC_NR(cmd);

  switch (nr) {
  case SET_NPROC_OP:
    result = get_user(new_nprocs, (int *)arg);

    if (result) {
      printk(KERN_WARNING "%s: failed to get new max nprocs\n", MYDEV_NAME);
      return -EINVAL;
    }

    if (new_nprocs < 1) {
      printk(KERN_WARNING "%s: invalid new max nprocs %d\n", MYDEV_NAME, new_nprocs);
      return -EINVAL;
    }

    atomic_set(&asgn2_device.max_nprocs, new_nprocs);

    printk(KERN_WARNING "%s: max_nprocs set to %d\n",
            __stringify (KBUILD_BASENAME), atomic_read(&asgn2_device.max_nprocs));
    return 0;
  } 

  return -ENOTTY;
}

void remove_from_cbuffer(unsigned long t_arg) {
  /* Get either the whole cbuf in one go, or pass it in two goes */
  int returned, count = 0;
  do {
    if (cbuf.head + cbuf.count < cbuf.capacity) count = cbuf.count;
    else count = cbuf.capacity - cbuf.head;

    returned = asgn2_write(&cbuf.buf[cbuf.head], count);
    if (returned < count) printk(KERN_WARNING "The write didn't do it all\n");
    cbuf.count -= returned;
    cbuf.head = (cbuf.head + returned) % cbuf.capacity;
  } while (cbuf.count > 0);
}

DECLARE_TASKLET(t_name, remove_from_cbuffer, (unsigned long) &cbuf);

int add_to_cbuffer(char to_add) {
  // check buffer is not full
  if (cbuf.capacity == cbuf.count) {
    return -ENOMEM;
  }
  cbuf.buf[(cbuf.head + cbuf.count) % cbuf.capacity] = to_add;
  cbuf.count++;
  /* if it's the last byte for that file or the buffer is 
   * more than half full then schedule the tasklet which 
   * writes to the file queue */
  if (to_add == '\0' || cbuf.count > (cbuf.capacity/2))
    tasklet_schedule(&t_name);
  return 0;
}

void get_half_byte(void){
  u8 this_half_byte = read_half_byte();
  char full_byte;

  if (second_half) {
    full_byte = (char) top_half_byte << 4 | this_half_byte;
    second_half = 0;
    /* Keep trying to add it to the buffer until it goes in */
    add_to_cbuffer(full_byte);
  } else {
    top_half_byte = this_half_byte;
    second_half = 1;
  }
}

irqreturn_t dummyport_interrupt(int irq, void *dev_id){
  //printk(KERN_WARNING "Got the interrupt\n");
  get_half_byte();
  return 0;
}

/**
 * Displays information about current status of the module,
 * which helps debugging.
 */
int asgn2_read_procmem(char *buf, char **start, off_t offset, int count,
		     int *eof, void *data) {
  int result;

  result = snprintf(buf, count,
	            "major = %d\nnumber of pages = %d\ndata size = %u\n"
                    "disk size = %d\nnprocs = %d\nmax_nprocs = %d\n",
	            asgn2_major, asgn2_device.num_pages, 
                    asgn2_device.data_size, 
                    (int)(asgn2_device.num_pages * PAGE_SIZE),
                    atomic_read(&asgn2_device.nprocs), 
                    atomic_read(&asgn2_device.max_nprocs)); 
  *eof = 1; /* end of file */
  return result;
}

struct file_operations asgn2_fops = {
  .owner = THIS_MODULE,
  .read = asgn2_read,
  .unlocked_ioctl = asgn2_ioctl,
  .open = asgn2_open,
  .release = asgn2_release,
};

#define IRQ_NUMBER 7
static int irq_number = IRQ_NUMBER;

/**
 * Initialise the module and create the master device
 */
int __init asgn2_init_module(void){
  int result; 

  /* START TRIM */
  atomic_set(&asgn2_device.nprocs, 0);
  atomic_set(&asgn2_device.max_nprocs, 1);

  incomplete_file = allocate_empty_file_node();

  INIT_LIST_HEAD(&asgn2_device.file_list);
  asgn2_device.num_pages = 0;
  atomic_set(&asgn2_device.num_files, 0);
  asgn2_device.data_size = 0;


  result = alloc_chrdev_region(&asgn2_device.dev, asgn2_minor,
                               asgn2_dev_count, MYDEV_NAME);

  if (result < 0) {
    printk(KERN_WARNING "asgn2: can't get major number\n");
    return -EBUSY;
  }

  asgn2_major = MAJOR(asgn2_device.dev);

  if (NULL == (asgn2_device.cdev = cdev_alloc())) {
    printk(KERN_WARNING "%s: can't allocate cdev\n", MYDEV_NAME);
    result = -ENOMEM;
    goto fail_cdev;
  }

  asgn2_device.cdev->ops = &asgn2_fops;
  asgn2_device.cdev->owner = THIS_MODULE;
  
  result = cdev_add(asgn2_device.cdev, asgn2_device.dev, asgn2_dev_count);
  if (result < 0) {
    printk(KERN_WARNING "%s: can't register chrdev_region to the system\n",
           MYDEV_NAME);
    goto fail_cdev;
  }

  if (NULL == create_proc_read_entry(MYDEV_NAME, 
				     0, /* default mode */ 
				     NULL, /* parent dir */
				     asgn2_read_procmem,
				     NULL /* client data */)) {
    printk(KERN_WARNING "%s: can't create procfs entry\n", MYDEV_NAME);
    result = -ENOMEM;
    goto fail_proc;
  }

  asgn2_device.cache = kmem_cache_create(MYDEV_NAME, sizeof(page_node), 
                                         0, 0, NULL); 
  
  if (NULL == asgn2_device.cache) {
    printk(KERN_WARNING "%s: can't create cache\n", MYDEV_NAME);
    result = -ENOMEM;
    goto fail_kmem_cache_create;
  }
  /* END TRIM */
 
  asgn2_device.class = class_create(THIS_MODULE, MYDEV_NAME);
  if (IS_ERR(asgn2_device.class)) {
    printk(KERN_WARNING "%s: can't create udev class\n", MYDEV_NAME);
    result = -ENOMEM;
    goto fail_class;
  }

  asgn2_device.device = device_create(asgn2_device.class, NULL, 
                                      asgn2_device.dev, "%s", MYDEV_NAME);
  if (IS_ERR(asgn2_device.device)) {
    printk(KERN_WARNING "%s: can't create udev device\n", MYDEV_NAME);
    result = -ENOMEM;
    goto fail_device;
  }

  if(gpio_dummy_init()<0){
    printk(KERN_WARNING "%s: can't initilise gpio pins\n", MYDEV_NAME);
    result = -ENOMEM;
    goto fail_gpio;
  }

  if(request_irq(irq_number, dummyport_interrupt, 0, MYDEV_NAME, asgn2_device.device)){
    printk(KERN_WARNING "%s: Unable to request IRQ for this device \n", MYDEV_NAME);
    result = -ENOMEM;
    goto fail_irq;
  }

  cbuf.head = 0;
  cbuf.count = 0;
  cbuf.capacity = PAGE_SIZE;
  if (NULL == (cbuf.buf = kmalloc(sizeof(char) * cbuf.capacity, GFP_KERNEL))) {
    printk(KERN_WARNING "%s: Unable allocate cicular buffer memory\n", MYDEV_NAME);
    result = -ENOMEM;
    goto fail_buffer;
  }
  //printk(KERN_WARNING "set up udev entry\n");
  printk(KERN_WARNING "Hello world from %s\n", MYDEV_NAME);

  return 0;

/* cleanup code called when any of the initialization steps fail */
fail_buffer:
  /* don't need to free because the allocation failed */
fail_irq:
  free_irq(irq_number, asgn2_device.device);
fail_gpio:
  gpio_dummy_exit();
fail_device:
  /* unregister device */
  unregister_chrdev_region(asgn2_device.dev, asgn2_dev_count);
fail_class:
  class_destroy(asgn2_device.class);
fail_kmem_cache_create:
   kmem_cache_destroy(asgn2_device.cache);  
fail_proc:
  remove_proc_entry(MYDEV_NAME, NULL);
fail_cdev:
  /* de-register the device */
  cdev_del(asgn2_device.cdev);

/*fail_device:
   class_destroy(asgn2_device.class);
fail_class:
fail_cdev:
  cdev_del(asgn2_device.cdev);
  unregister_chrdev_region(asgn2_device.dev, asgn2_dev_count);
fail_proc_entry:
  remove_proc_entry(MYDEV_NAME, NULL );*/
  
  return result;
}


/**
 * Finalise the module
 */
void __exit asgn2_exit_module(void){
  device_destroy(asgn2_device.class, asgn2_device.dev);
  class_destroy(asgn2_device.class);
  printk(KERN_WARNING "cleaned up udev entry\n");
  
  free_file_nodes();
  kmem_cache_destroy(asgn2_device.cache);
  remove_proc_entry(MYDEV_NAME, NULL /* parent dir */);
  cdev_del(asgn2_device.cdev);
  unregister_chrdev_region(asgn2_device.dev, asgn2_dev_count);
  gpio_dummy_exit();
  free_irq(irq_number, asgn2_device.device);
  kfree(cbuf.buf);
  printk(KERN_WARNING "Good bye from %s\n", MYDEV_NAME);
}


module_init(asgn2_init_module);
module_exit(asgn2_exit_module);


