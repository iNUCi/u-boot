# SPDX-License-Identifier: GPL-2.0+
#
# Assembly of the Intel MID OSIP boot image used by the Acer Iconia A1-830
# firmware: U-Boot takes the place of the stock SFI bootstub.

quiet_cmd_mk_osip = OSIP    $@
      cmd_mk_osip = python3 $(srctree)/board/$(BOARDDIR)/mkosip.py \
		-o $@ -u $< -d $(srctree)/board/$(BOARDDIR)/osip

INPUTS-y += u-boot-osip.img
u-boot-osip.img: u-boot.bin FORCE
	$(call if_changed,mk_osip)
