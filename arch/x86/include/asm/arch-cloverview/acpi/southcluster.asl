/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Partially based on southcluster.asl for other x86 platforms
 */

Device (PCI0)
{
    Name (_HID, EISAID("PNP0A08"))    /* PCIe */
    Name (_CID, EISAID("PNP0A03"))    /* PCI */

    Name (_UID, Zero)
    Name (_BBN, Zero)

    Name (MCRS, ResourceTemplate()
    {
        /* Bus Numbers */
        WordBusNumber(ResourceProducer, MinFixed, MaxFixed, PosDecode,
                0x0000, 0x0000, 0x00ff, 0x0000, 0x0100, , , PB00)

        /* IO Region 0 */
        WordIO(ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
                0x0000, 0x0000, 0x0cf7, 0x0000, 0x0cf8, , , PI00)

        /* PCI Config Space */
        IO(Decode16, 0x0cf8, 0x0cf8, 0x0001, 0x0008)

        /* IO Region 1 */
        WordIO(ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
                0x0000, 0x0d00, 0xffff, 0x0000, 0xf300, , , PI01)

        /* Prefetchable PCI Memory Region (64-bit) */
        QWordMemory(ResourceProducer, PosDecode, MinFixed, MaxFixed,
                Prefetchable, ReadWrite,
                0x0000000000000000, 0x0000000040000000, 0x000000004fffffff,
                0x0000000000000000, 0x0000000010000000, , , PM64)

        /* PCI Memory Region */
        DWordMemory(ResourceProducer, PosDecode, MinFixed, MaxFixed,
                NonCacheable, ReadWrite,
                0x00000000, 0xd0000000, 0xffffffff, 0x00000000,
                0x30000000, , , PMEM)
    })

    Method (_CRS, 0, Serialized)
    {
        Return (MCRS)
    }

    /* Device Resource Consumption */
    Device (PDRC)
    {
        Name (_HID, EISAID("PNP0C02"))
        Name (_UID, One)

        Name (PDRS, ResourceTemplate()
        {
            Memory32Fixed(ReadWrite, MCFG_BASE_ADDRESS, MCFG_BASE_SIZE)
        })

        Method (_CRS, 0, Serialized)
        {
            Return (PDRS)
        }
    }

    Method (_OSC, 4)
    {
        /* Check for proper GUID */
        If (LEqual(Arg0, ToUUID("33db4d5b-1ff7-401c-9657-7441c03dd766"))) {
            /* Let OS control everything */
            Return (Arg3)
        } Else {
            /* Unrecognized UUID */
            CreateDWordField(Arg3, 0, CDW1)
            Or(CDW1, 4, CDW1)
            Return (Arg3)
        }
    }

    /*
     * SD Card Slot - removable SD/SDHC card, PCI 00:04.0 [8086:08f9]
     * Card detect via GPIO 69 (sd_cd_pin, edge, active both, shared, wake)
     */
    Device (SDHB)
    {
        Name (_ADR, 0x00040000)
        Name (_DEP, Package ()
        {
            GPIO
        })
        Name (_STA, STA_VISIBLE)

        Name (RBUF, ResourceTemplate()
        {
            GpioInt(Edge, ActiveBoth, SharedAndWake, PullNone, 10000,
                "\\_SB.PCI0.GPIO", 0, ResourceConsumer, , ) { 69 }
        })

        Method (_CRS, 0, Serialized)
        {
            Return (RBUF)
        }

        Name (_DSD, Package () {
            ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
            Package () {
                Package () { "cd-gpios", Package () { ^SDHB, 0, 0, 0 } },
            }
        })
    }

    /*
     * eMMC - SanDisk HAG2e, PCI 00:01.0 [8086:08e5]
     * Populated on this board but intentionally disabled: _STA returns
     * Zero so firmware-visible consumers ignore it. Note the kernel
     * additionally skips its PCI ID in the SDHCI driver table.
     */
    Device (EMMC)
    {
        Name (_ADR, 0x00010000)
        Name (_DEP, Package ()
        {
            GPIO
        })
        Name (_STA, Zero)

        Name (RBUF, ResourceTemplate()
        {
            GpioIo(Exclusive, PullDefault, 0, 0, IoRestrictionOutputOnly,
                "\\_SB.PCI0.GPIO", 0, ResourceConsumer, , ) { 117 }
        })

        Method (_CRS, 0, Serialized)
        {
            Return (RBUF)
        }
    }

    /*
     * SDIO - Broadcom BCM4330 WiFi, PCI 00:04.1 [8086:08fa]
     * Non-removable, SDIO function 0x01. WLAN-enable (GPIO 170, CORE
     * bank offset 74) is owned and asserted by the kernel's gpio-intel-mid
     * driver as soon as the CORE controller registers, so no ACPI power
     * method / opregion sequencing is needed here.
     */
    Device (SDIO)
    {
        Name (_ADR, 0x00040001)
        Name (_STA, STA_VISIBLE)

        Device (BRC1)
        {
            Name (_ADR, 0x01)
            Name (_STA, STA_VISIBLE)

            Method (_RMV, 0, NotSerialized)
            {
                Return (Zero)
            }
        }
    }

    /*
     * SPI controllers (DesignWare) - PCI 00:00.1, 00:00.2 and 00:02.4.
     * Chip-select handling is internal to the SoC; unlike Tangier there
     * are no GPIO chip-selects to describe.
     */
    Device (SPI0)
    {
        Name (_ADR, 0x00000001)
        Name (_STA, STA_VISIBLE)
    }

    Device (SPI1)
    {
        Name (_ADR, 0x00000002)
        Name (_STA, STA_VISIBLE)
    }

    Device (SPI2)
    {
        Name (_ADR, 0x00020004)
        Name (_STA, STA_VISIBLE)
    }

    /*
     * I2C controllers (DesignWare) - PCI 00:00.3 through 00:00.5 and
     * 00:03.1 through 00:03.4. Unlike Tangier no SSCN/FMCN/HSCN timing
     * methods are provided: those are only consumed by the platform
     * driver, while these controllers bind through the PCI glue.
     */
    Device (I2C0)
    {
        Name (_ADR, 0x00000003)
        Name (_STA, STA_VISIBLE)
    }

    Device (I2C1)
    {
        Name (_ADR, 0x00000004)
        Name (_STA, STA_VISIBLE)
    }

    Device (I2C2)
    {
        Name (_ADR, 0x00000005)
        Name (_STA, STA_VISIBLE)

        /*
         * Fuel gauge (Maxim MAX17047/MAX17050) at address 0x36.
         * Binds natively via ACPI id "MAX17047" to the
         * max17042_battery driver. ALERT line is open-drain, active-low,
         * wired to Langwell GPIO 94 (max_fg_alert).
         */
        Device (FG0)
        {
            Name (_HID, "MAX17047")
            Name (_DEP, Package (0x01)
            {
                GPIO
            })
            Name (_CRS, ResourceTemplate ()
            {
                /*
                 * ALERT (max_fg_alert) is open-drain, active-low, wired to
                 * Langwell GPIO 94 (AON bank, PCI 00:02.1, 8086:08eb).
                 * The gpio-intel-mid driver routes this bank's parent GSI
                 * (21) and provides the line IRQ domain, so a GpioInt here
                 * makes max17042_battery interrupt-driven instead of polled.
                 */
                GpioInt (Level, ActiveLow, ExclusiveAndWake, PullUp, 0,
                    "\\_SB.PCI0.GPIO", 0, ResourceConsumer, , )
                {
                    94
                }
                I2cSerialBus (0x0036, ControllerInitiated, 400000,
                    AddressingMode7Bit, "\\_SB.PCI0.I2C2", 0,
                    ResourceConsumer, ,)
            })
            Name (_STA, STA_VISIBLE)
        }

        /*
         * Battery charger (Summit SMB347) at address 0x6a. The driver has
         * no ACPI id table but does carry a "summit,smb347" OF compatible,
         * so instantiate through the generic PRP0001/DSD compatible route
         * of the smb347-charger driver. STAT/INT is open-drain, active-low,
         * wired to Langwell GPIO 93 (chgr_int_n).
         */
        Device (CHR0)
        {
            Name (_HID, "PRP0001")
            Name (_UID, Zero)
            Name (_DEP, Package (0x01)
            {
                GPIO
            })
            Name (_DSD, Package ()
            {
                ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
                Package ()
                {
                    Package () { "compatible", "summit,smb347" },
                    Package () { "summit,enable-usb-charging", 1 },
                    Package () { "summit,enable-mains-charging", 1 },
                    Package () { "summit,usb-current-limit-microamp", 1500000 },
                }
            })
            Name (_CRS, ResourceTemplate ()
            {
                /*
                 * STAT/INT (chgr_int_n) is open-drain, active-low, wired to
                 * Langwell GPIO 93 (AON bank). As with FG0, the gpio-intel-mid
                 * IRQ domain (parent GSI 21) makes this functional, so smb347
                 * is interrupt-driven rather than polled.
                 */
                GpioInt (Level, ActiveLow, ExclusiveAndWake, PullUp, 0,
                    "\\_SB.PCI0.GPIO", 0, ResourceConsumer, , )
                {
                    93
                }
                I2cSerialBus (0x006a, ControllerInitiated, 400000,
                    AddressingMode7Bit, "\\_SB.PCI0.I2C2", 0,
                    ResourceConsumer, ,)
            })
            Name (_STA, STA_VISIBLE)
        }

        /*
         * FocalTech FT6236 touchscreen at address 0x38 (ft5x0x from the SFI
         * DEVS table, confirmed present on the bus). The edt-ft5x06 driver
         * binds via the PRP0001/DSD compatible route. INT (ts_int) is
         * active-low, wired to Langwell GPIO 62 (AON bank). The native
         * Cloverview firmware powers the touch from an always-on panel/MSIC
         * rail with no OS-visible regulator, so the driver's vcc/iovcc
         * regulator requests are optional (patched in the kernel).
         * The reset (ts_rst) is Langwell GPIO 58 (AON bank), active-low,
         * exposed to the driver via the _DSD GPIO descriptor "reset-gpios"
         * so it can pulse the line out of reset itself (EDT_PMODE_POWEROFF).
         */
        Device (TCH0)
        {
            Name (_HID, "PRP0001")
            Name (_UID, One)
            Name (_DEP, Package (0x01)
            {
                GPIO
            })
            Name (_DSD, Package ()
            {
                ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
                Package ()
                {
                    Package () { "compatible", "focaltech,ft6236" },
                    Package () { "reset-gpios",
                        Package () { ^TCH0, 1, 0, 1 } },
                    Package () { "touchscreen-size-x", 768 },
                    Package () { "touchscreen-size-y", 1024 },
                }
            })
            Name (_CRS, ResourceTemplate ()
            {
                GpioInt (Edge, ActiveLow, ExclusiveAndWake, PullUp, 0,
                    "\\_SB.PCI0.GPIO", 0, ResourceConsumer, , )
                {
                    62
                }
                GpioIo (Exclusive, PullDefault, 0, 0, IoRestrictionOutputOnly,
                    "\\_SB.PCI0.GPIO", 0, ResourceConsumer, , )
                {
                    58
                }
                I2cSerialBus (0x0038, ControllerInitiated, 400000,
                    AddressingMode7Bit, "\\_SB.PCI0.I2C2", 0,
                    ResourceConsumer, ,)
            })
            Name (_STA, STA_VISIBLE)
        }
    }

    Device (I2C3)
    {
        Name (_ADR, 0x00030001)
        Name (_STA, STA_VISIBLE)
    }

    Device (I2C4)
    {
        Name (_ADR, 0x00030002)
        Name (_STA, STA_VISIBLE)
    }

    Device (I2C5)
    {
        Name (_ADR, 0x00030003)
        Name (_STA, STA_VISIBLE)
    }

    Device (I2C6)
    {
        Name (_ADR, 0x00030004)
        Name (_STA, STA_VISIBLE)

        /*
         * Bosch BMA250E accelerometer at address 0x18 (bma250 from the SFI
         * DEVS table, confirmed on I2C bus 5 == PCI 00:03.4 == I2C6).
         * Binds natively via ACPI id "BMA250E" to the bmc150_accel IIO
         * driver. INT (accel_int) is wired to Langwell GPIO 60 (AON bank).
         * The interrupt is optional for this driver (it probes fine without
         * one), but providing it makes the data-ready line IRQ-driven.
         */
        Device (ACC0)
        {
            Name (_HID, "BMA250E")
            Name (_DEP, Package (0x01)
            {
                GPIO
            })
            Name (_CRS, ResourceTemplate ()
            {
                GpioInt (Edge, ActiveLow, ExclusiveAndWake, PullUp, 0,
                    "\\_SB.PCI0.GPIO", 0, ResourceConsumer, , )
                {
                    60
                }
                I2cSerialBus (0x0018, ControllerInitiated, 400000,
                    AddressingMode7Bit, "\\_SB.PCI0.I2C6", 0,
                    ResourceConsumer, ,)
            })
            Name (_STA, STA_VISIBLE)
        }
    }

    /*
     * GPIO controller bank 0 (Langwell) - PCI 00:02.1 [8086:08eb].
     * Bank 1 lives at PCI 00:03.5 and is modelled as Device (LGC1)
     * below so the Bluetooth "shutdown" line (global GPIO 109, i.e.
     * CORE bank offset 13) can resolve through it.
     *
     * The controller is bound by the gpio-intel-mid PCI driver, whose
     * parent interrupt is GSI 21 (routed via clv_gsi_map); the driver
     * registers the line IRQ domain that GpioInt consumers (SDHB card
     * detect on GPIO 69) resolve through.
     */
    Device (GPIO)
    {
        Name (_ADR, 0x00020001)
        Name (_STA, STA_VISIBLE)

        /*
         * GpioIo resources for the AON GPIO bank (pins 0-95). These let the
         * kernel's _DSD GPIO descriptor references (e.g. the touchscreen's
         * "reset-gpios" pointing at pin 58) resolve a GpioIo resource by
         * crs_entry_index, matching a specific line_index within its pin
         * table, and translate back to the Langwell AON gpiochip.
         * The touch reset, ts_rst, is pin 58 (index 58 in the table).
         */
        Name (_CRS, ResourceTemplate ()
        {
            GpioIo (Exclusive, PullDefault, 0, 0, IoRestrictionOutputOnly,
                "\\_SB.PCI0.GPIO", 0, ResourceConsumer, , )
            {
                0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
                30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43,
                44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57,
                58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71,
                72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85,
                86, 87, 88, 89, 90, 91, 92, 93, 94, 95
            }
        })

    }

    /*
     * GPIO controller bank 1 (cloverview_core) - PCI 00:03.5
     * [8086:08f7]. No on-board GpioInt consumers, but the Bluetooth
     * "shutdown" line is global GPIO 109, i.e. CORE bank offset 13
     * (global 96 + 13). Providing an ACPI companion (_ADR) lets the
     * consumer's _CRS GpioIo resource reference "\\_SB.PCI0.LGC1"
     * with pin 13 and resolve to this gpiochip. acpi_get_gpiod()
     * treats the pin as a controller-relative offset, hence 13, not 109.
     *
     * The WLAN-enable pad (global 170 = CORE offset 74) is owned and
     * asserted by the kernel's gpio-intel-mid driver at probe time, so it
     * is not described here.
     */
    Device (LGC1)
    {
        Name (_ADR, 0x00030005)
        Name (_STA, STA_VISIBLE)
    }

    /*
     * USB device controller (Langwell/Penwell OTG) - PCI 00:02.3
     * [8086:e006]. Peripheral only: there is no xHCI root hub to model,
     * unlike the Tangier DWC3.
     */
    Device (OTG)
    {
        Name (_ADR, 0x00020003)
        Name (_STA, STA_VISIBLE)
    }

    /*
     * High Speed UART complex - PCI 00:05.0 through 00:05.3 (three
     * serial ports plus the HSU DMA engine). Bluetooth (BCM4330) hangs
     * off HSU port 0 (PCI 00:05.0). The BT0 device is a serdev slave:
     * its UartSerialBus resource names this controller so the kernel
     * serdev core binds it to hci_bcm, which then owns the module's
     * GPIO lines directly (no userspace btattach needed).
     *
     * GPIOs, via _DSD references into BT0's own _CRS (crs_entry_index):
     *   [0] device-wakeup -> Langwell AON GPIO 45 (\_SB.PCI0.GPIO)
     *   [1] shutdown      -> cloverview_core GPIO 13, i.e. global 109
     *                       (\_SB.PCI0.LGC1; 13 is the core-bank offset)
     * host-wakeup is not wired on this board, so it is left out (the
     * hci_bcm host-wakeup request is optional).
     */
    Device (HSU0)
    {
        Name (_ADR, 0x00050000)
        Name (_STA, STA_VISIBLE)

        Device (BT0)
        {
            Name (_HID, "BCM2E01")
            Name (_DEP, Package ()
            {
                GPIO,
                LGC1
            })

            Name (_STA, STA_VISIBLE)

            Name (RBUF, ResourceTemplate()
            {
                UartSerialBus(0x0001C200, DataBitsEight, StopBitsOne,
                    0xFC, LittleEndian, ParityTypeNone, FlowControlHardware,
                    0x20, 0x20, "\\_SB.PCI0.HSU0", 0, ResourceConsumer, , )
                GpioIo(Exclusive, PullDefault, 0, 0, IoRestrictionOutputOnly,
                    "\\_SB.PCI0.GPIO", 0, ResourceConsumer, , ) { 45 }
                GpioIo(Exclusive, PullDefault, 0, 0, IoRestrictionOutputOnly,
                    "\\_SB.PCI0.LGC1", 0, ResourceConsumer, , ) { 13 }
            })

            Method (_CRS, 0, Serialized)
            {
                Return (RBUF)
            }

            Name (_DSD, Package () {
                ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
                Package () {
                    Package () { "device-wakeup-gpios", Package () { ^BT0, 0, 0, 0 } },
                    Package () { "shutdown-gpios", Package () { ^BT0, 1, 0, 0 } },
                }
            })
        }
    }

    /*
     * SCU IPC - PCI 00:01.7 [8086:08ea]. Gateway to the MSIC PMIC and
     * the other IPC-attached devices (ADC, thermal, power button),
     * instantiated by the kernel's Intel SCU PCI driver.
     */
    Device (IPCP)
    {
        Name (_ADR, 0x00010007)
        Name (_STA, STA_VISIBLE)
    }

    /*
     * DMA engines (Intel MID DMA) - PCI 00:02.5 and 00:02.6.
     */
    Device (DMA0)
    {
        Name (_ADR, 0x00020005)
        Name (_STA, STA_VISIBLE)
    }

    Device (DMA1)
    {
        Name (_ADR, 0x00020006)
        Name (_STA, STA_VISIBLE)
    }

    /*
     * Power Management Unit - PCI 00:02.2 [8086:08ec].
     */
    Device (PMUP)
    {
        Name (_ADR, 0x00020002)
        Name (_STA, STA_VISIBLE)
    }

    /*
     * Image Signal Processor (camera) - PCI 00:03.0 [8086:08d0].
     */
    Device (ISPX)
    {
        Name (_ADR, 0x00030000)
        Name (_STA, STA_VISIBLE)
    }
}
