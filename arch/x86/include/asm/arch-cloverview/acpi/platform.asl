/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Partially based on platform.asl for other x86 platforms
 */

#include <asm/acpi/statdef.asl>
#include <asm/arch/iomap.h>

/*
 * The _PTS method (Prepare To Sleep) is called before the OS is
 * entering a sleep state. The sleep state number is passed in Arg0.
 */
Method(_PTS, 1)
{
}

/* The _WAK method is called on system wakeup */
Method(_WAK, 1)
{
    Return (Package() { Zero, Zero })
}

Scope (_SB)
{
    /*
     * No PC-style CMOS RTC on Cloverview (Intel MID). The platform's real
     * clock source is the SFI MRTC / virtual RTC, which mainline no longer
     * supports (rtc_mrst was removed). The old PNP0B00 device here made
     * rtc_cmos probe non-existent CMOS I/O ports 0x70-0x77, so it is removed.
     */
}

/* ACPI global NVS */
#include "global_nvs.asl"

Scope (\_SB)
{
    #include "southcluster.asl"

    /*
     * Cloverview (Saltwell/Intel MID) CPU power management.
     *
     * P-states: not exposed via _PSS/_PCT/_PSD. This SoC is fully SCU-gated:
     * empirically, the kernel reports MSR 0x199 as an "unrecognized MSR" and
     * writing PERF_CTL (0x199) powers the SoC off immediately. There is no
     * MSR-based DVFS. P-state control on Linux would require a custom
     * SCU-IPC cpufreq driver (the Linux-native equivalent of the Windows
     * PEP driver). See git history for the attempted _PSS experiments.
     *
     * C-states: exposed via _CST on each logical CPU (modelled as a modern
     * Device with _HID "ACPI0007", per the currently-shipping ACPI spec).
     * The values below are the authoritative firmware-verified
     * latencies/hints from the real Z2760 ACPI dump (Cpu0Cst.aml, CSTA&0x200
     * branch, full 4-state ladder).
     * MWAIT hints: C1=0x00, C2=0x10, C3=0x30, C6=0x52.
     * The kernel's acpi_idle driver (not intel_idle) consumes these directly.
     */
    Device (CPU0)
    {
        Name (_HID, "ACPI0007")
        Name (_UID, Zero)
        Name (_STA, 0x0F)
        Method (_CST, 0, NotSerialized)
        {
            Return (Package ()
            {
                0x04,
                Package ()
                {
                    ResourceTemplate () { Register (FFixedHW, 0x01, 0x02, 0x0000000000000000, 0x01) },
                    One,
                    One,
                    0x03E8
                },
                Package ()
                {
                    ResourceTemplate () { Register (FFixedHW, 0x01, 0x02, 0x0000000000000010, 0x01) },
                    0x02,
                    0x14,
                    0x01F4
                },
                Package ()
                {
                    ResourceTemplate () { Register (FFixedHW, 0x01, 0x02, 0x0000000000000030, 0x03) },
                    0x03,
                    0x64,
                    0x64
                },
                Package ()
                {
                    ResourceTemplate () { Register (FFixedHW, 0x01, 0x02, 0x0000000000000052, 0x03) },
                    0x03,
                    0x8C,
                    0x0A
                }
            })
        }
    }

    Device (CPU1)
    {
        Name (_HID, "ACPI0007")
        Name (_UID, One)
        Name (_STA, 0x0F)
        Method (_CST, 0, NotSerialized)
        {
            Return (Package ()
            {
                0x04,
                Package ()
                {
                    ResourceTemplate () { Register (FFixedHW, 0x01, 0x02, 0x0000000000000000, 0x01) },
                    One,
                    One,
                    0x03E8
                },
                Package ()
                {
                    ResourceTemplate () { Register (FFixedHW, 0x01, 0x02, 0x0000000000000010, 0x01) },
                    0x02,
                    0x14,
                    0x01F4
                },
                Package ()
                {
                    ResourceTemplate () { Register (FFixedHW, 0x01, 0x02, 0x0000000000000030, 0x03) },
                    0x03,
                    0x64,
                    0x64
                },
                Package ()
                {
                    ResourceTemplate () { Register (FFixedHW, 0x01, 0x02, 0x0000000000000052, 0x03) },
                    0x03,
                    0x8C,
                    0x0A
                }
            })
        }
    }

    Device (CPU2)
    {
        Name (_HID, "ACPI0007")
        Name (_UID, 0x02)
        Name (_STA, 0x0F)
        Method (_CST, 0, NotSerialized)
        {
            Return (Package ()
            {
                0x04,
                Package ()
                {
                    ResourceTemplate () { Register (FFixedHW, 0x01, 0x02, 0x0000000000000000, 0x01) },
                    One,
                    One,
                    0x03E8
                },
                Package ()
                {
                    ResourceTemplate () { Register (FFixedHW, 0x01, 0x02, 0x0000000000000010, 0x01) },
                    0x02,
                    0x14,
                    0x01F4
                },
                Package ()
                {
                    ResourceTemplate () { Register (FFixedHW, 0x01, 0x02, 0x0000000000000030, 0x03) },
                    0x03,
                    0x64,
                    0x64
                },
                Package ()
                {
                    ResourceTemplate () { Register (FFixedHW, 0x01, 0x02, 0x0000000000000052, 0x03) },
                    0x03,
                    0x8C,
                    0x0A
                }
            })
        }
    }

    Device (CPU3)
    {
        Name (_HID, "ACPI0007")
        Name (_UID, 0x03)
        Name (_STA, 0x0F)
        Method (_CST, 0, NotSerialized)
        {
            Return (Package ()
            {
                0x04,
                Package ()
                {
                    ResourceTemplate () { Register (FFixedHW, 0x01, 0x02, 0x0000000000000000, 0x01) },
                    One,
                    One,
                    0x03E8
                },
                Package ()
                {
                    ResourceTemplate () { Register (FFixedHW, 0x01, 0x02, 0x0000000000000010, 0x01) },
                    0x02,
                    0x14,
                    0x01F4
                },
                Package ()
                {
                    ResourceTemplate () { Register (FFixedHW, 0x01, 0x02, 0x0000000000000030, 0x03) },
                    0x03,
                    0x64,
                    0x64
                },
                Package ()
                {
                    ResourceTemplate () { Register (FFixedHW, 0x01, 0x02, 0x0000000000000052, 0x03) },
                    0x03,
                    0x8C,
                    0x0A
                }
            })
        }
    }

    /*
     * Acer Iconia A1-830 side button array (PNP0C40 "Standard Button
     * Controller"). Pins are on the Langwell AON GPIO bank (\\_SB.PCI0.GPIO,
     * PCI 00:02.1, 8086:08eb).
     *
     * soc_button_array binds PNP0C40 and maps each GpioInt index to a fixed
     * KEY_* (per the Windows "SoC Platforms" ACPI guide): index 0=power,
     * 1=home, 2=volume-up, 3=volume-down, 4=rotation-lock. A slot whose GPIO
     * fails to resolve (out-of-range pin) is silently skipped.
     *
     * Empirically on the A1-830 the physical volume rocker is:
     *   volume-up   -> AON line 31
     *   volume-down -> AON line 30
     * so these sit at index 2 / index 3 respectively. The power, home and
     * rotation slots point at out-of-range pins (>= 96, beyond the AON bank)
     * so they are skipped: this SoC's power button is EC/MSIC-routed, not a
     * GPIO, and there is no home/rotation hardware on this board.
     */
    Device (TBAD)
    {
        Name (_HID, "INTCFD9")
        Name (_CID, "PNP0C40")
        Name (_DDN, "Keyboard less system - 5 Button Array Device")

        Name (RBUF, ResourceTemplate ()
        {
            GpioInt (Edge, ActiveBoth, ExclusiveAndWake, PullNone, 0x1770,
                "\\_SB.PCI0.GPIO", 0, ResourceConsumer, , )
                { 120 }
            GpioInt (Edge, ActiveBoth, ExclusiveAndWake, PullNone, 0x1770,
                "\\_SB.PCI0.GPIO", 0, ResourceConsumer, , )
                { 121 }
            GpioInt (Edge, ActiveBoth, ExclusiveAndWake, PullNone, 0x1770,
                "\\_SB.PCI0.GPIO", 0, ResourceConsumer, , )
                { 31 }
            GpioInt (Edge, ActiveBoth, ExclusiveAndWake, PullNone, 0x1770,
                "\\_SB.PCI0.GPIO", 0, ResourceConsumer, , )
                { 30 }
            GpioInt (Edge, ActiveBoth, Exclusive, PullNone, 0x1770,
                "\\_SB.PCI0.GPIO", 0, ResourceConsumer, , )
                { 122 }
        })

        Method (_CRS, 0, Serialized)
        {
            Return (RBUF)
        }
        Method (_STA, 0, NotSerialized)
        {
            Return (0x0F)
        }
    }

    Device (PWRB)
    {
        Name (_HID, EisaId ("PNP0C0C"))
    }
}
