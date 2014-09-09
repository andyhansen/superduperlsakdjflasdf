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
	{ 0x4b8b420c, "device_destroy" },
	{ 0x7485e15e, "unregister_chrdev_region" },
	{ 0x74738f6d, "cdev_del" },
	{ 0x620805df, "remove_proc_entry" },
	{ 0x263940d7, "kmem_cache_destroy" },
	{ 0x85305f03, "class_destroy" },
	{ 0x148887b7, "device_create" },
	{ 0xb4c75863, "__class_create" },
	{ 0xf8efc666, "kmem_cache_create" },
	{ 0x39c19d5, "create_proc_entry" },
	{ 0xe3321d40, "cdev_add" },
	{ 0x9d0d02cf, "cdev_alloc" },
	{ 0x29537c9e, "alloc_chrdev_region" },
	{ 0x2875e14d, "kmem_cache_free" },
	{ 0x969267bd, "__free_pages" },
	{ 0xf20dabd8, "free_irq" },
	{ 0x9dfdf722, "gpio_free_array" },
	{ 0xd6b8e852, "request_threaded_irq" },
	{ 0x8574ca6c, "gpio_request_array" },
	{ 0x4ff229c7, "gpiochip_find" },
	{ 0x2e5810c6, "__aeabi_unwind_cpp_pr1" },
	{ 0xe2d5255a, "strcmp" },
	{ 0x7ec1c780, "mem_map" },
	{ 0x67c2fa54, "__copy_to_user" },
	{ 0xb81960ca, "snprintf" },
	{ 0x27e1a049, "printk" },
	{ 0x353e3fa5, "__get_user_4" },
	{ 0xefd6cf06, "__aeabi_unwind_cpp_pr0" },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";


MODULE_INFO(srcversion, "8FD9E246594E038B3DC6A13");
