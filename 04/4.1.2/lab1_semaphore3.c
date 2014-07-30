/* **************** LDD:1.0 s_12/lab1_semaphore2.c **************** */
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
 * Semaphore Contention
 *
 * second and third module to test semaphores
 @*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/semaphore.h>
#include <asm/atomic.h>
#include <linux/errno.h>
#include <linux/semaphore.h>

extern struct semaphore my_semaphore;

static char *modname = __stringify(KBUILD_BASENAME);

static int __init my_init(void)
{
	printk(KERN_INFO "Trying to load module %s\n", modname);
	/*printk(KERN_INFO "\n%s start count=%d:\n", modname,
	       atomic_read(&my_semaphore.count));*/

	/* COMPLETE ME */
	if (down_trylock(&my_semaphore)) {
		printk(KERN_INFO "semaphore unlocked - wake up \n");
		return -1;
	}

	/*printk(KERN_INFO "\n%s semaphore put semaphore, count=%d:\n",
	       modname, atomic_read(&my_semaphore.count));*/

	return 0;
}

static void __exit my_exit(void)
{
	/* COMPLETE ME */
	up(&my_semaphore);

	/*printk(KERN_INFO "\n%s semaphore end count=%d:\n",
	       modname, atomic_read(&my_semaphore.count));*/
}

module_init(my_init);
module_exit(my_exit);

MODULE_AUTHOR("Tatsuo Kawasaki");
MODULE_DESCRIPTION("LDD:1.0 s_12/lab1_semaphore2.c");
MODULE_LICENSE("GPL v2");
