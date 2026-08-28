// SPDX-License-Identifier: GPL-2.0+

#include <dm.h>
#include <sysreset.h>
#include <asm/scu.h>

static int cloverview_sysreset_request(struct udevice *dev, enum sysreset_t type)
{
	int value;

	switch (type) {
	case SYSRESET_WARM:
		value = IPCMSG_WARM_RESET;
		break;
	case SYSRESET_COLD:
		value = IPCMSG_COLD_RESET;
		break;
	default:
		return -ENOSYS;
	}

	scu_ipc_simple_command(value, 0);

	return -EINPROGRESS;
}

static const struct udevice_id cloverview_sysreset_ids[] = {
	{ .compatible = "intel,reset-cloverview" },
	{ }
};

static struct sysreset_ops cloverview_sysreset_ops = {
	.request = cloverview_sysreset_request,
};

U_BOOT_DRIVER(cloverview_sysreset) = {
	.name = "cloverview-sysreset",
	.id = UCLASS_SYSRESET,
	.of_match = cloverview_sysreset_ids,
	.ops = &cloverview_sysreset_ops,
};
