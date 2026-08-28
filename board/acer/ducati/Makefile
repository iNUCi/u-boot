obj-y	+= ducati.o
obj-$(CONFIG_GENERATE_ACPI_TABLE) += dsdt_generated.o

# SeaVGABIOS embedding for the SeaBIOS payload. Re-enable together with
# board_late_init()'s framebuffer/romfile hooks when needed again.
#ifneq ($(wildcard $(srctree)/board/$(BOARDDIR)/vgabios-cb.bin),)
#AFLAGS_vgabios.o := -I$(srctree)/board/$(BOARDDIR)
#obj-y	+= vgabios.o
#endif
