/* SPDX-License-Identifier: GPL-2.0+ */

	.section .rodata.ducati_vgabios, "a"
	.balign 16
	.globl ducati_vgabios_start
ducati_vgabios_start:
	.incbin "vgabios-cb.bin"
	.globl ducati_vgabios_end
ducati_vgabios_end:
