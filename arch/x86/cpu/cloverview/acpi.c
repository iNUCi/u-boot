// SPDX-License-Identifier: GPL-2.0+
/*
 * ACPI tables for Intel Cloverview (Intel MID family)
 *
 * Partially based on acpi.c for other x86 platforms
 */

#include <cpu.h>
#include <dm.h>
#include <mapmem.h>
#include <acpi/acpi_table.h>
#include <asm/ioapic.h>
#include <asm/lapic.h>
#include <asm/mpspec.h>
#include <asm/tables.h>
#include <asm/arch/global_nvs.h>
#include <asm/arch/iomap.h>
#include <dm/uclass-internal.h>

void acpi_fill_fadt(struct acpi_fadt *fadt)
{
	fadt->preferred_pm_profile = ACPI_PM_UNSPECIFIED;

	fadt->iapc_boot_arch = ACPI_FADT_VGA_NOT_PRESENT |
			       ACPI_FADT_NO_PCIE_ASPM_CONTROL;
	fadt->flags =
		ACPI_FADT_WBINVD |
		ACPI_FADT_POWER_BUTTON | ACPI_FADT_SLEEP_BUTTON |
		ACPI_FADT_SEALED_CASE | ACPI_FADT_HEADLESS |
		ACPI_FADT_HW_REDUCED_ACPI;
}

void *acpi_fill_madt(struct acpi_madt *madt, struct acpi_ctx *ctx)
{
	void *current = ctx->current;

	madt->lapic_addr = LAPIC_DEFAULT_BASE;
	madt->flags = ACPI_MADT_PCAT_COMPAT;

	current += acpi_create_madt_lapics(current);

	current += acpi_create_madt_ioapic((struct acpi_madt_ioapic *)current,
			io_apic_read(IO_APIC_ID) >> 24, IO_APIC_ADDR, 0);

	return current;
}

int acpi_fill_mcfg(struct acpi_ctx *ctx)
{
	size_t size;

	size = acpi_create_mcfg_mmconfig
		((struct acpi_mcfg_mmconfig *)ctx->current,
		MCFG_BASE_ADDRESS, 0x0, 0x0, 0x0);
	acpi_inc(ctx, size);

	return 0;
}

int acpi_create_gnvs(struct acpi_global_nvs *gnvs)
{
	struct udevice *dev;
	int ret;

	/* at least we have one processor */
	gnvs->pcnt = 1;

	/* override the processor count with actual number */
	ret = uclass_find_first_device(UCLASS_CPU, &dev);
	if (!ret && dev) {
		ret = cpu_get_count(dev);
		if (ret > 0)
			gnvs->pcnt = ret;
	}

	return 0;
}
