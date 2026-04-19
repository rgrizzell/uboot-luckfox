# Luckfox Pico Boot Chain

This document captures what we know about booting the Luckfox Pico family
(RV1103 / RV1106, Cortex-A7) with this U-Boot tree. Two boot flows are
supported, each with its own rkbin blob set, defconfig, and gotchas.

## Boards

| Board                 | SoC     | Boot media        | Defconfig                               |
| --------------------- | ------- | ----------------- | --------------------------------------- |
| Luckfox Pico Mini A   | RV1103  | SD card           | `luckfox_rv1106_uboot_defconfig`        |
| Luckfox Pico Mini B   | RV1103  | 128 MB SPI NAND   | `luckfox_rv1106_spi_nand_tb_defconfig`  |
| Luckfox Pico Mini B   | RV1103  | SD card (fallback)| `luckfox_rv1106_uboot_defconfig`        |

The Mini B has onboard W25N01GV SPI NAND; the Mini A does not. Both boards
can boot from an SD card with the same image.

---

## SD boot chain (Mini A / Mini B)

```
BROM
  -> DDR init blob        (rkbin: rv1106_ddr_924MHz_v1.15.bin)
  -> SD SPL               (rkbin: rv1106_spl_v1.02.bin)
  -> U-Boot proper        (this tree: u-boot.img, FIT with firmware@1 at 0x200000)
  -> RKIMG_BOOTCOMMAND
  -> kernel FIT from "boot" partition
```

### Partition layout (SD)

```
32K(env), 512K@32K(idblock), 256K(uboot), 32M(boot), ... rootfs ...
```

### Notes

- The SD rkbin SPL (`rv1106_spl_v1.02.bin`) is TB-capable and expects
  **FDT_MAGIC (`d00dfeed`)** in the `uboot` partition. Stage the U-Boot FIT
  (`u-boot.img`) directly. **Do not** use `loaderimage --pack --uboot u-boot.bin` —
  it produces `"LOADER  "` magic that this SPL cannot parse.
- `u-boot.img` is ~256 KB with `firmware@1` at load 0x200000.
- `CONFIG_BOOTDELAY=3` gives a 3-second autoboot interrupt window on the
  serial console.

---

## SPI NAND Thunder-Boot chain (Mini B)

```
BROM
  -> DDR init blob        (rkbin: rv1106_ddr_924MHz_tb_v1.15.bin)
  -> HPMCU blob           (rkbin: rv1106_hpmcu_tb_v1.01.bin, at offset 0x40000)
  -> NAND SPL             (rkbin: rv1106_spl_spi_nand_tb_v1.00.bin)
  -> U-Boot proper        (this tree: u-boot.img, FIT)
  -> boot_fit             (loads kernel FIT from "boot" partition via ENVF mtdparts)
  -> kernel
```

### Partition layout (NAND)

```
256K(env), 256K@256K(idblock), 512K(uboot), 6M(boot), -(ubi)
```

### Notes

- The NAND SPL is a U-Boot SPL built with `CONFIG_SPL_KERNEL_BOOT=y`. The
  vendor's intent is that it boots the kernel directly from the `boot`
  partition, and U-Boot proper is only used for recovery/USB download.
- In practice, the rkbin SPL's direct-kernel-boot attempt fails with
  `Missing FDT description in DTB` (the FIT config node has no `description`
  property), so it **falls through to U-Boot proper**. This tree sets
  `CONFIG_BOOTCOMMAND "boot_fit"` under `#ifdef CONFIG_SPL_KERNEL_BOOT` so
  the fallthrough boots the kernel instead of resetting.
- Boot1 (early chain) requires `uboot-ignore = <1>` in every `hash {}` node
  of the kernel FIT. Without it, Boot1 attempts RSA verification of the
  unsigned image, fails, and falls back to USB download mode.
- The `boot` partition is 6 MB; current kernel FIT is ~4.6 MB.
- `CONFIG_BOOTDELAY=3` matches the SD defconfig.

---

## Key findings (things to NOT learn the hard way)

### 1. Never `loaderimage`-wrap U-Boot for either SPL

Both the SD SPL and the NAND TB SPL parse the `uboot` payload as a U-Boot
FIT (FDT_MAGIC). The `loaderimage` tool writes a `"LOADER  "` header that
neither SPL understands. Stage `u-boot.img` from this tree.

### 2. `CONFIG_BOOTCOMMAND "reset"` under `CONFIG_SPL_KERNEL_BOOT` is a boot-loop trap

The vendor `include/configs/evb_rv1106.h` originally hardcoded
`CONFIG_BOOTCOMMAND "reset"` under `#ifdef CONFIG_SPL_KERNEL_BOOT`, on the
assumption that SPL handles the kernel. When the rkbin NAND SPL falls
through (see above), U-Boot proper runs `reset`, which restarts the boot
loop forever. This tree sets it to `"boot_fit"` instead.

### 3. ENVF does not override the compiled-in `bootcmd`

Writing `bootcmd=boot_fit` into the NAND env partition is **not** enough.
U-Boot prints `Using default environment` for `bootcmd` even when ENVF
imports other vars (`mtdparts`, `sys_bootargs`). The compiled-in
`CONFIG_BOOTCOMMAND` wins. Patch the config header.

### 4. MTD partition naming differs between U-Boot and Linux

- U-Boot: parent SPI NAND is hardcoded `spi-nand0`
  (`drivers/mtd/nand/spi/core.c`)
- Linux: parent SPI NAND is named from the SPI parent's `dev_name()` —
  `spi2.0` on RV1103 (SFC = spi2), `spi1.0` on RV1103B (FSPI)

A single `mtdparts=<mtd_id>:...` on the kernel cmdline can only match one
name; the other silently ends up with zero partitions. **Fix:** declare
partitions via `fixed-partitions` OF nodes under `flash@0` in the kernel
DTS, then `ubi.mtd=ubi` matches by label regardless of parent name.

### 5. Vendor `Failed to get/load DTB` / `saradc` warnings are benign

`boot_rkimg.c` and `kernel_dtb.c` scan for a vendor-style `resource`
partition / SARADC that our layouts don't have. Harmless — `boot_fit`
finds the DTB inside the FIT. Downgrade these to `debug()` to silence the
noise.

### 6. USB-C to a data host forces USB download mode

BROM enters USB download mode whenever USB-C is connected to a data host.
`rkdeveloptool rd` (reset) while still connected just re-enters download
mode — it does **not** perform a normal boot. To test a real boot, power
the board from a USB charger (no data host) and use a **separate** UART
adapter for the serial console.

### 7. HPMCU UART interleaves with the debug UART

On the RV1103 Mini B, HPMCU RT-Thread prints on the same debug UART pins
at 115200 baud. A `RKUART` banner at 115200 means HPMCU is alive. The
kernel UART runs at 1500000 on NAND (matches the TB SPL), so interleaved
output garbles at either baud rate during early boot — expected, not a
fault.

### 8. Thunder-Boot CODE472 banner

`UsbBoot ...NNNN` in the TB boot log is CODE472 (usbplug) printing its
startup banner as part of normal TB init — not a download-mode failure.
Tick count is consistent across boots.

### 9. `sd_update` / `sd-update-*` etc. expect a vendor layout

This tree inherits Rockchip's `sd_update` command family. Those depend on
vendor partition names (`resource`, `trust`, etc.) that the OpenWRT SD
layout does not reproduce. Use `boot_fit` for kernel boot.

---

## Source files of interest in this tree

| Path                                           | Purpose                                       |
| ---------------------------------------------- | --------------------------------------------- |
| `configs/luckfox_rv1106_uboot_defconfig`       | SD defconfig                                  |
| `configs/luckfox_rv1106_spi_nand_tb_defconfig` | NAND TB defconfig                             |
| `include/configs/evb_rv1106.h`                 | `CONFIG_BOOTCOMMAND` override lives here      |
| `include/configs/rv1106_common.h`              | Shared RV1106 config defines                  |
| `common/spl/spl_nand_rockchip.c`               | NAND SPL kernel-boot code (rkbin-compiled)    |
| `cmd/boot_fit.c`                               | `boot_fit` command implementation             |
| `common/boot_rkimg.c`, `boot/image-fit.c`      | Vendor DTB lookup / FIT parsing (noisy)       |

For generic U-Boot documentation (build, command reference, porting), see
the upstream [README](README).

---

## OpenWRT integration

The Luckfox OpenWRT tree consumes this source via
`package/boot/uboot-luckfox/Makefile`, which bumps `PKG_SOURCE_VERSION` to
a commit hash in this repo. Related OpenWRT files:

- `package/boot/rkbin/Makefile` — stages rkbin DDR/SPL/HPMCU blobs and
  builds `idblock.img` via `boot_merger`
- `target/linux/rockchip/image/rv1106-uboot.env.sd.txt` — SD env
  (mtdparts, bootargs)
- `target/linux/rockchip/image/rv1103-uboot.env.spi-nand.txt` — NAND env
  (mtdparts, bootargs; fed to ENVF)
- `target/linux/rockchip/dts/rv1103g-luckfox-pico-mini*.dts` — kernel DTS
  with `fixed-partitions` OF nodes
