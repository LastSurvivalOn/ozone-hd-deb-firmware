/*
 * fbump: wraps the memory of /dev/fb0 in a UMP buffer, so the Mali X11 driver
 * can render straight into the screen. The stock kernel has no
 * CONFIG_FB_AMLOGIC_UMP, which gives the same buffer through the fb ioctls
 * GET_UMP_SECURE_ID_BUF1/2. The secure id is in
 * /sys/module/fbump/parameters/secure_id.
 */
#include <linux/module.h>
#include <linux/fb.h>
#include <ump/ump_kernel_interface_ref_drv.h>

static ump_dd_handle handle = UMP_DD_HANDLE_INVALID;
static unsigned int secure_id = UMP_INVALID_SECURE_ID;
module_param(secure_id, uint, 0444);
MODULE_PARM_DESC(secure_id, "UMP secure id of the whole fb0 memory");

static int __init fbump_init(void)
{
	struct fb_info *info = num_registered_fb > 0 ? registered_fb[0] : NULL;
	ump_dd_physical_block block;

	if (!info)
		return -ENODEV;
	block.addr = info->fix.smem_start;
	block.size = info->fix.smem_len;
	handle = ump_dd_handle_create_from_phys_blocks(&block, 1);
	if (handle == UMP_DD_HANDLE_INVALID)
		return -ENOMEM;
	secure_id = ump_dd_secure_id_get(handle);
	pr_info("fbump: fb0 at 0x%lx, %lu bytes, UMP secure id %u\n",
		block.addr, block.size, secure_id);
	return 0;
}

static void __exit fbump_exit(void)
{
	ump_dd_reference_release(handle);
}

module_init(fbump_init);
module_exit(fbump_exit);
MODULE_DESCRIPTION("fb0 memory as a UMP buffer");
MODULE_LICENSE("GPL");
