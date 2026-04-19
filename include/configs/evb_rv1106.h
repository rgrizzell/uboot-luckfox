/*
 * (C) Copyright 2022 Rockchip Electronics Co., Ltd
 *
 * SPDX-License-Identifier:     GPL-2.0+
 */

#ifndef __EVB_RV1106_H
#define __EVB_RV1106_H

#include <configs/rv1106_common.h>

#define CONFIG_SYS_UBOOT_START		CONFIG_SYS_TEXT_BASE

#define CONFIG_SYS_MMC_ENV_DEV 0

#define ROCKCHIP_DEVICE_SETTINGS \
			"stdout=serial,vidconsole\0" \
			"stderr=serial,vidconsole\0"
#undef CONFIG_CONSOLE_SCROLL_LINES
#define CONFIG_CONSOLE_SCROLL_LINES            10

#ifndef CONFIG_SPL_BUILD
#undef CONFIG_BOOTCOMMAND

/*
 * We made a deal: Not allow U-Boot to bring up thunder-boot kernel.
 *
 * Because the thunder-boot feature may require special memory layout
 * or other appointments, U-Boot can't handle all that. Let's go back
 * to SPL to bring up kernel.
 *
 * Note: bootcmd is only called in normal boot sequence, that means
 * we allow user to boot what they want in U-Boot shell mode.
 */
#ifdef CONFIG_SPL_KERNEL_BOOT
/* Vendor assumption: SPL-direct-kernel-boot handles the kernel, so U-Boot
 * proper should never get here; "reset" restarts the cycle. On the Luckfox
 * Pico Mini B, the rkbin SPL falls through to U-Boot proper instead, so
 * "reset" produces a boot loop. Use boot_fit to load the FIT kernel from
 * the "boot" partition via ENVF mtdparts. */
#define CONFIG_BOOTCOMMAND "boot_fit"
#else
#define CONFIG_BOOTCOMMAND RKIMG_BOOTCOMMAND
#endif

#endif /* !CONFIG_SPL_BUILD */
#endif /* __EVB_RV1106_H */
