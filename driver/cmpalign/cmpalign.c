// SPDX-License-Identifier: MIT
/*
 * cmpalign - backport of the v7.1 pci_align_resource() tail-remainder
 * shift to kernel v7.0, armed as a kprobe on pcibios_align_resource().
 *
 * Kernel v7.0 sizes bridge windows by starting the candidate at an
 * aligned offset and never moves it back down for a non-zero remainder,
 * so a window of (64 GiB BAR1 + 32 MiB BAR3) overflows the free MMIO gap
 * by exactly 32 MiB and NVRM falls back to a 32 GiB BAR1. v7.1 subtracts
 * remainder = size & (align - 1) from the candidate before the caller's
 * containment check; this probe reproduces that subtraction.
 *
 * Difference vs upstream: v7.1 also refuses a candidate that would start
 * below the free gap (empty_res->start > start_addr). That check is not
 * reproduced here (the caller's stack-local avail is not identifiable
 * from the probe). On this host every free-gap pad is far larger than
 * any 1 MiB-granular remainder, and boot-time PCI assignment completes
 * long before this module loads.
 *
 * Reversibility: rmmod cmpalign disarms the probe and restores stock
 * v7.0 behaviour exactly; the kernel image itself is never modified.
 */
#include <linux/module.h>
#include <linux/version.h>
#include <linux/kprobes.h>
#include <linux/ioport.h>
#include <linux/sizes.h>
#include <linux/log2.h>

static int cmpalign_pre(struct kprobe *p, struct pt_regs *regs)
{
	struct resource *res = (struct resource *)regs->si;
	resource_size_t size = (resource_size_t)regs->dx;
	resource_size_t align = (resource_size_t)regs->cx;
	resource_size_t rem;

	if (!res || !(res->flags & IORESOURCE_MEM))
		return 0;
	if (res->start <= SZ_4G)
		return 0;
	if (!align || !is_power_of_2(align))
		return 0;
	if (!(size & (align - 1)))
		return 0;
	rem = size & (align - 1);
	if (rem & (SZ_1M - 1))
		return 0;
	if (res->start < rem)
		return 0;
	res->start -= rem;
	pr_info("cmpalign: shifted window candidate down by 0x%llx (start now 0x%llx)\n",
		(unsigned long long)rem, (unsigned long long)res->start);
	return 0;
}

static struct kprobe cmpalign_kp = {
	.symbol_name = "pcibios_align_resource",
	.pre_handler = cmpalign_pre,
};

static int __init cmpalign_init(void)
{
	int ret;

	if (LINUX_VERSION_CODE >= KERNEL_VERSION(7, 1, 0)) {
		pr_info("cmpalign: kernel %d.%d already ships pci_align_resource; staying disarmed\n",
			LINUX_VERSION_MAJOR, LINUX_VERSION_PATCHLEVEL);
		return -ENODEV;
	}
	ret = register_kprobe(&cmpalign_kp);
	if (ret)
		pr_err("cmpalign: register_kprobe failed: %d\n", ret);
	else
		pr_info("cmpalign: armed kprobe on pcibios_align_resource\n");
	return ret;
}

static void __exit cmpalign_exit(void)
{
	unregister_kprobe(&cmpalign_kp);
	pr_info("cmpalign: disarmed\n");
}

module_init(cmpalign_init);
module_exit(cmpalign_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Backport of v7.1 pci_align_resource remainder shift (64 GB BAR1)");
MODULE_AUTHOR("cmpunlocker");
