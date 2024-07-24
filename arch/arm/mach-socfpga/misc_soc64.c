// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2016-2023 Intel Corporation <www.intel.com>
 *
 */

#include <common.h>
#include <altera.h>
#include <env.h>
#include <errno.h>
#include <init.h>
#include <log.h>
#include <asm/arch/mailbox_s10.h>
#include <asm/arch/misc.h>
#include <asm/arch/reset_manager.h>
#include <asm/arch/smc_api.h>
#include <asm/arch/smmuv3_dv.h>
#include <asm/arch/system_manager.h>
#include <asm/io.h>
#include <asm/global_data.h>
#include <linux/bitfield.h>
#include <mach/clock_manager.h>

#define RSU_DEFAULT_LOG_LEVEL  7

DECLARE_GLOBAL_DATA_PTR;

u8 socfpga_get_board_id(void);

/*
 * FPGA programming support for SoC FPGA Stratix 10
 */
static Altera_desc altera_fpga[] = {
	{
		/* Family */
		Intel_FPGA_SDM_Mailbox,
		/* Interface type */
		secure_device_manager_mailbox,
		/* No limitation as additional data will be ignored */
		-1,
		/* No device function table */
		NULL,
		/* Base interface address specified in driver */
		NULL,
		/* No cookie implementation */
		0
	},
};


/*
 * Print CPU information
 */
#if defined(CONFIG_DISPLAY_CPUINFO)
int print_cpuinfo(void)
{
#if IS_ENABLED(CONFIG_TARGET_SOCFPGA_AGILEX5)
	puts("CPU:   Intel FPGA SoCFPGA Platform (ARMv8 64bit Cortex-A55/A76)\n");
#else
	puts("CPU:   Intel FPGA SoCFPGA Platform (ARMv8 64bit Cortex-A53)\n");
#endif
	return 0;
}
#endif

#ifdef CONFIG_ARCH_MISC_INIT
int arch_misc_init(void)
{
#if !(IS_ENABLED(CONFIG_TARGET_SOCFPGA_AGILEX5_EMU))
	char qspi_string[13];
	char level[4];
	char id[3];

	snprintf(level, sizeof(level), "%u", RSU_DEFAULT_LOG_LEVEL);
	sprintf(qspi_string, "<0x%08x>", cm_get_qspi_controller_clk_hz());
	env_set("qspi_clock", qspi_string);

	/* for RSU, set log level to default if log level is not set */
	if (!env_get("rsu_log_level"))
		env_set("rsu_log_level", level);

	/* Export board_id as environment variable */
	sprintf(id, "%u", socfpga_get_board_id());
	env_set("board_id", id);
#endif

	return 0;
}
#endif

int arch_early_init_r(void)
{
	socfpga_fpga_add(&altera_fpga[0]);

	return 0;
}

#if IS_ENABLED(CONFIG_TARGET_SOCFPGA_AGILEX5)
bool is_agilex5_reva_workaround_required(void)
{
	u32 reg;
	bool status;

	reg = readl(socfpga_get_sysmgr_addr() + SYSMGR_SOC64_BOOT_SCRATCH_POR1);
	debug("%s: SYSMGR_SOC64_BOOT_SCRATCH_POR1: 0x%x\n", __func__, reg);

	status = FIELD_GET(ALT_SYSMGR_SCRATCH_REG_POR_1_REVA_WORKAROUND_MASK, reg);
	debug("%s: Agilex 5 Rev A workaround status: 0x%x\n", __func__, status);

	return status;
}
#endif

/* Return 1 if FPGA is ready otherwise return 0 */
int is_fpga_config_ready(void)
{
#if IS_ENABLED(CONFIG_TARGET_SOCFPGA_AGILEX5)
	if (is_agilex5_reva_workaround_required()) {
		return (readl(socfpga_get_sysmgr_addr() +
				SYSMGR_SOC64_BOOT_SCRATCH_POR1) &
				ALT_SYSMGR_SCRATCH_REG_POR_1_REVA_WORKAROUND_USER_MODE_MASK);
	}
#endif

	return (readl(socfpga_get_sysmgr_addr() + SYSMGR_SOC64_FPGA_CONFIG) &
		SYSMGR_FPGACONFIG_READY_MASK) == SYSMGR_FPGACONFIG_READY_MASK;
}

void do_bridge_reset(int enable, unsigned int mask)
{
	/* Check FPGA status before bridge enable */
	if (!is_fpga_config_ready()) {
		puts("FPGA not ready. Bridge reset aborted!\n");
		return;
	}

	socfpga_bridges_reset(enable, mask);
}

void do_qspi_ownership_quirk(void)
{
	if (IS_ENABLED(CONFIG_CADENCE_QSPI) && IS_ENABLED(CONFIG_SPL_ATF)) {
		int ret = 0;

		ret = env_get_yesno("returnQSPI");
		if (ret == 1) {
			/* FCS Attestation:return QSPI ownership to SDM if needed */
			ret = smc_send_mailbox(MBOX_QSPI_CLOSE, 0, NULL,
					       0, 0, NULL);
			if (ret)
				printf("close QSPI failed, (err=%d)\n", ret);
		}
	}
}


void arch_preboot_os(void)
{
	do_qspi_ownership_quirk();
	mbox_hps_stage_notify(HPS_EXECUTION_STATE_OS);
}

int misc_init_r(void)
{
#if IS_ENABLED(CONFIG_TARGET_SOCFPGA_AGILEX5)
	if (is_agilex5_reva_workaround_required())
		return smmu_sdm_init();
#endif

	return 0;
}

