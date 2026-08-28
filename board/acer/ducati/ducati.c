// SPDX-License-Identifier: GPL-2.0+

#include <dm.h>
#include <init.h>
#include <linux/delay.h>
#include <pci.h>
#include <wdt.h>
#include <asm/arch/iomap.h>
#include <asm/global_data.h>
#include <asm/io.h>

int board_early_init_r(void)
{
	struct udevice *dev;

	/*
	 * The SCU watchdog is already running when U-Boot starts (the
	 * firmware starts it with a default of 90 seconds). Stop it here;
	 * with CONFIG_WATCHDOG_AUTOSTART disabled nothing restarts it
	 * afterwards, neither in U-Boot nor when booting an OS.
	 */
	if (!uclass_first_device_err(UCLASS_WDT, &dev))
		wdt_stop(dev);

	if (CONFIG_IS_ENABLED(VIDEO)) {
		struct udevice *vdev;

		/*
		 * Probe the video device now so its "vidconsole" stdio
		 * device is registered before console_init_r() sets up
		 * stdout/stderr. The lazy name-based probe used at
		 * console_init_r() is fragile: it insists the console is
		 * the last registered stdio device with an exact name
		 * match, so any mismatch silently drops "vidconsole" from
		 * the console mux (leaving just the logo on the panel).
		 */
		uclass_first_device_err(UCLASS_VIDEO, &vdev);
	}

	return 0;
}

/*
 * Program PCI_INTERRUPT_LINE for the devices whose interrupts the OS
 * routes 1:1 onto IOAPIC entries: intel_mid_pci_irq_enable() in Linux
 * reads this byte as the GSI, and the stock firmware programmed it with
 * these values (verified against the stock kernel's /proc/interrupts).
 * Without it the kernel maps GSI 0 for every device and e.g. the SDHCI
 * controllers time out on their first command.
 */
static const struct {
	u16 device;
	u8 devfn;
	u8 gsi;
} ducati_pci_irq_table[] = {
	{ 0x08e5, 0x08,	27 },	/* 00:01.0 SDHCI (eMMC) */
	{ 0x08ea, 0x0f,	23 },	/* 00:01.7 SCU IPC */
	{ 0x08f9, 0x20,	41 },	/* 00:04.0 SDHCI (SD card) */
	{ 0x08fa, 0x21,	42 },	/* 00:04.1 SDHCI (SDIO WiFi) */
	{ 0x08fc, 0x28,	60 },	/* 00:05.0 HSU UART 0 (Bluetooth) */
	{ 0x08fd, 0x29,	61 },	/* 00:05.1 HSU UART 1 (modem/GPS) */
	{ 0x08fe, 0x2a,	62 },	/* 00:05.2 HSU UART 2 (debug) */
};

static void ducati_pci_assign_irqs(void)
{
	struct udevice *dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(ducati_pci_irq_table); i++) {
		int ret = dm_pci_find_device(0x8086,
					     ducati_pci_irq_table[i].device,
					     0, &dev);
		if (ret)
			continue;

		dm_pci_write_config8(dev, PCI_INTERRUPT_LINE,
				     ducati_pci_irq_table[i].gsi);
		dm_pci_write_config8(dev, PCI_INTERRUPT_PIN, 1);

		if (IS_ENABLED(CONFIG_PCI)) {
			u8 line = 0;

			dm_pci_read_config8(dev, PCI_INTERRUPT_LINE, &line);
			printf("PCI 00:%02x.%x [%04x] INT_LINE=%u%s\n",
			       ducati_pci_irq_table[i].devfn >> 3,
			       ducati_pci_irq_table[i].devfn & 7,
			       ducati_pci_irq_table[i].device, line,
			       line != ducati_pci_irq_table[i].gsi ?
				       " (WRITE DID NOT STICK)" : "");
		}
	}
}

/*
 * Drive a Cloverview GPIO pad to a level before the OS runs. The registers
 * per bank (AON 0-95 at 0xff119000, CORE 96-191 at 0xff13f000) are:
 *     GPLR = base + 0  (level),  GPDR = base + 12 (direction),
 *     GPSR = base + 24 (set),    GPCR = base + 36 (clear),
 * each indexed by (offset / 32) * 4 with the bit at offset % 32.
 */
static void ducati_gpio_set(u32 base, u32 off, bool hi)
{
	u32 gplr = base +  0 + (off / 32) * 4;
	u32 gpdr = base + 12 + (off / 32) * 4;
	u32 gpsr = base + 24 + (off / 32) * 4;
	u32 gpcr = base + 36 + (off / 32) * 4;

	setbits_le32((void *)(uintptr_t)gpdr, BIT(off % 32));
	writel(BIT(off % 32), (void *)(uintptr_t)(hi ? gpsr : gpcr));

	printf("gpio: base=%08x off=%u -> %u (GPLR=%08x GPDR=%08x)\n",
	       base, off, hi, readl((void *)(uintptr_t)gplr),
	       readl((void *)(uintptr_t)gpdr));
}

int board_late_init(void)
{
#define CLV_GPIO_BANK0_BASE	0xff119000	/* AON  (pins 0-95) */

	/*
	 * Bring the FocalTech FT6236 touchscreen out of reset before the OS
	 * boots, mirroring what stock Android's touch driver did. This must
	 * happen in firmware (not the kernel): while the ts_rst pad (58, AON,
	 * active-low) is asserted the controller can hold the I2C2 bus in a bad
	 * state, so it has to be released before any kernel I2C activity. It is
	 * a timed pulse, unlike the always-on module rails (WLAN-enable 170,
	 * bt_reg_on 110, gpio_vbenable 40) which the kernel's gpio-intel-mid
	 * driver now owns.
	 *
	 *     ts_rst = 58 (AON, active-low; pulsed 0->1)
	 */
	ducati_gpio_set(CLV_GPIO_BANK0_BASE, 58, false);	/* assert reset */
	mdelay(10);
	ducati_gpio_set(CLV_GPIO_BANK0_BASE, 58, true);		/* release reset */

	if (IS_ENABLED(CONFIG_PCI))
		ducati_pci_assign_irqs();

	return 0;
}
