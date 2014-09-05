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
	{ 0x37a0cba, "kfree" },
	{ 0xff58149e, "misc_deregister" },
	{ 0xeed38436, "malloc_sizes" },
	{ 0xdb7e82df, "misc_register" },
	{ 0xd197d610, "kmem_cache_alloc" },
	{ 0xfaef0ed, "__tasklet_schedule" },
	{ 0x7d11c268, "jiffies" },
	{ 0x67c2fa54, "__copy_to_user" },
	{ 0x2e5810c6, "__aeabi_unwind_cpp_pr1" },
	{ 0xefd6cf06, "__aeabi_unwind_cpp_pr0" },
	{ 0xf623f316, "module_refcount" },
	{ 0x27e1a049, "printk" },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";


MODULE_INFO(srcversion, "D7277CB0EC4CFC673F599DD");
