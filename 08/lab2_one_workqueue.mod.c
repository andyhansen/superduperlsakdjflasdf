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
	{ 0xd6b8e852, "request_threaded_irq" },
	{ 0x27e1a049, "printk" },
	{ 0xf20dabd8, "free_irq" },
	{ 0xe523ad75, "synchronize_irq" },
	{ 0xefd6cf06, "__aeabi_unwind_cpp_pr0" },
	{ 0xe6b3b90a, "arm_delay_ops" },
	{ 0x7d11c268, "jiffies" },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";


MODULE_INFO(srcversion, "5B99BCD55014ADFC98B36B5");
