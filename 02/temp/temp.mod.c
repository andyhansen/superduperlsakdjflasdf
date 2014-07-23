#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0xda9e78e9, "module_layout" },
	{ 0x15692c87, "param_ops_int" },
	{ 0x37a0cba, "kfree" },
	{ 0x4b8b420c, "device_destroy" },
	{ 0xeed38436, "malloc_sizes" },
	{ 0x85305f03, "class_destroy" },
	{ 0x148887b7, "device_create" },
	{ 0x74738f6d, "cdev_del" },
	{ 0xb4c75863, "__class_create" },
	{ 0xe3321d40, "cdev_add" },
	{ 0x60c2503e, "cdev_init" },
	{ 0x7485e15e, "unregister_chrdev_region" },
	{ 0xd197d610, "kmem_cache_alloc" },
	{ 0x29537c9e, "alloc_chrdev_region" },
	{ 0x27e1a049, "printk" },
	{ 0xd8e484f0, "register_chrdev_region" },
	{ 0x67c2fa54, "__copy_to_user" },
	{ 0xfbc74f64, "__copy_from_user" },
	{ 0xfa2a45e, "__memzero" },
	{ 0x364b3fff, "up" },
	{ 0x4fe38dbd, "down_interruptible" },
	{ 0xefd6cf06, "__aeabi_unwind_cpp_pr0" },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";


MODULE_INFO(srcversion, "8C13945B30A0DEE6E7A8BED");
