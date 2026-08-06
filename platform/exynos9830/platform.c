/*
 * Copyright@ Samsung Electronics Co. LTD
 *
 * This software is proprietary of Samsung Electronics.
 * No part of this software, either material or conceptual may be copied or distributed, transmitted,
 * transcribed, stored in a retrieval system or translated into any human or computer language in any form by any means,
 * electronic, mechanical, manual or otherwise, or disclosed
 * to third parties without the express written permission of Samsung Electronics.
 */

#include <lk/reg.h>
#include <dev/ufs.h>
#include <dev/boot.h>
#include <dev/rpmb.h>
#include <pit.h>
#include <part.h>
#include <dev/interrupt/arm_gic.h>
#include <dev/timer/arm_generic.h>
#include <platform/device_info.h>
#include <platform/interrupts.h>
#include <platform/sfr.h>
#include <platform/uart.h>
#include <platform/smc.h>
#include <dev/pmic_s2mps_19_22.h>
#ifdef CONFIG_SUB_PMIC_S2DOS05
#include <dev/pmic_s2dos05.h>
#else
#include <dev/sub_pmic_s2mpb02.h>
#endif
#include <dev/if_pmic_s2mu106.h>
#include <dev/fg_s2mu106.h>
#include <dev/debug/dss.h>
#include <platform/ldfw.h>
#include <platform/secure_boot.h>
#include <platform/h-arx.h>
#include <platform/power/flexpmu_dbg.h>
#include <platform/tmu.h>
#include <dev/chg_max77705.h>
#include <platform/dvfs_info.h>
#include <platform/mmu/mmu_func.h>
#include <dev/mmc.h>

#include <lib/font_display.h>
#include <lib/logo_display.h>
#include <lib/fdtapi.h>
#include <target/dpu_config.h>
#include <stdio.h>
#include <ctype.h>
#include <platform/b_rev.h>

#include <lk3rd/boot_reason.h>
#include <lk3rd/kaslr_status.h>
#include <lk3rd/mainline_quirks.h>

#ifdef CONFIG_GET_B_REV_FROM_ADC
#include <dev/exynos_adc.h>
#include <target/b_rev_adc.h>
#endif

#ifdef CONFIG_GET_B_REV_FROM_GPIO
#include <platform/gpio.h>
#include <target/b_rev_gpio.h>
#endif

#ifdef EXYNOS_ACPM_BASE
#include <platform/power/acpm.h>
#endif

#include <platform/chip_rev.h>

#define ARCH_TIMER_IRQ 30

void speedy_gpio_init(void);
void xbootldo_gpio_init(void);

unsigned int s5p_chip_id[4] = { 0x0, 0x0, 0x0, 0x0 };
struct chip_rev_info s5p_chip_rev;
unsigned int charger_mode = 0;
unsigned int board_id = CONFIG_BOARD_ID;
int board_rev = -1;
unsigned int secure_os_loaded = 0;

char enter_reason_c[ENTER_REASON_SIZE];
char* enter_reason = &enter_reason_c[0];

struct ufs_device_info ufs_info = {0, (char *)"UNKNOWN UFS MANUFACTURER"};
struct ram_info dram_info = {0, (char *)"UNKNOWN DRAM MANUFACTURER", (char *)"UNKNOWN DRAM TYPE"};

volatile char *bootloader_cmdline;

typedef struct {
	const char *node;
	char compatible[65];
	char reg[65];
} bootloader_reserved_region;

volatile bootloader_reserved_region bootloader_reserved_regions[] = {
	{"kaslr", "kernel-kaslr", "<0x00000000 0x80001000 0x1000>"},
	{"el2_earlymem", "el2,uh", "<0x0000000a 0xfe800000 0x1800000>"},
	{"el2_code", "el2,uh", "<0x00000000 0xc1400000 0x200000>"},
};

volatile int bootloader_reserved_region_count = sizeof(bootloader_reserved_regions) /
										sizeof(bootloader_reserved_regions[0]);

extern char tima_smc_handler[];
extern char spi_handler[];

#ifdef CONFIG_GET_B_REV_FROM_ADC
int get_board_rev_adc(int *sh)
{
	int i, j;
	int rev = 0;
	*sh = 0;
	int hit_cnt = 0;
	for (i = 0; i < B_REV_ADC_LINES; i++) {
		int adc_v = exynos_adc_read_raw(b_rev_adc[i].ch);
		for (j = 0; j < b_rev_adc[i].levels;j++) {
			int min = b_rev_adc[i].table[j] - b_rev_adc[i].dt;
			int max = b_rev_adc[i].table[j] + b_rev_adc[i].dt;
			if (adc_v >= min && adc_v <= max) {
				rev = j <<  *sh;
				*sh += b_rev_adc[i].bits;
				hit_cnt++;
				continue;
			}
		}
	}

	if (hit_cnt != B_REV_ADC_LINES)
		return -1;
	return rev;
}
#endif

#ifdef CONFIG_GET_B_REV_FROM_GPIO
int get_board_rev_gpio(void)
{
	int i;
	int rev = 0;
	struct exynos_gpio_bank *bank;
	for (i = 0; i < B_REV_GPIO_LINES; i++) {
		int gpio = b_rev_gpio[i].bit;
		bank = b_rev_gpio[i].bank;
		exynos_gpio_cfg_pin(bank, gpio, GPIO_INPUT);
		exynos_gpio_set_pull(bank, gpio, GPIO_PULL_NONE);
		rev |= (exynos_gpio_get_value(bank, gpio) & 0x1) << i;
	}
	return rev;

}
#endif

void get_bootloader_cmdline(void)
{
        int offset;
        int len, ret = 0;

        u32 bootloader_fdt_location = readl(FDT_POINTER_ADDRESS);
        void *bootloader_fdt = (void*)(uintptr_t)bootloader_fdt_location;

        ret = fdt_check_header(bootloader_fdt);
        if (ret) {
                printf("libfdt fdt_check_header(): %s\n", fdt_strerror(ret));
        }

        offset = fdt_path_offset(bootloader_fdt, "/chosen");
        if (offset < 0) {
                printf("libfdt fdt_path_offset(): %s\n", fdt_strerror(offset));
        }

        bootloader_cmdline = (char*)fdt_getprop(bootloader_fdt, offset, "bootargs", &len);
        if (len <= 0) {
                printf("libfdt fdt_getprop(): %s\n", fdt_strerror(len));
        }
}

void get_bootloader_reserved_memory(void)
{
	const char *compatible;
	const fdt32_t *reg;
	int offset;
	int len, ret = 0;

	u32 bootloader_fdt_location = readl(FDT_POINTER_ADDRESS);
	void *bootloader_fdt = (void *)(uintptr_t)bootloader_fdt_location;

	ret = fdt_check_header(bootloader_fdt);
	if (ret) {
		printf("libfdt fdt_check_header(): %s\n", fdt_strerror(ret));
	}

	offset = fdt_path_offset(bootloader_fdt, "/reserved-memory");
	if (offset < 0) {
		printf("libfdt fdt_path_offset(): %s\n", fdt_strerror(offset));
	}

	for (int subnode = fdt_first_subnode(bootloader_fdt, offset); subnode >= 0;
				subnode = fdt_next_subnode(bootloader_fdt, subnode)) {
		for(int i = 0; i < bootloader_reserved_region_count; i++) {
			const char *name = fdt_get_name(bootloader_fdt, subnode, NULL);

			if(!strcmp(name, bootloader_reserved_regions[i].node)) {
				compatible = fdt_getprop(bootloader_fdt, subnode, "compatible", &len);

				if(compatible)
					strcpy((char *)bootloader_reserved_regions[i].compatible, compatible);

				reg = fdt_getprop(bootloader_fdt, subnode, "reg", &len);

				if(reg) {
					char reg_value[64] = {};

					sprintf(reg_value, "<0x%llx 0x%llx 0x%x>", (u64)fdt32_to_cpu(reg[0]), (u64)fdt32_to_cpu(reg[1]), fdt32_to_cpu(reg[2]));
					strcpy((char *)bootloader_reserved_regions[i].reg, reg_value);
				}
			}
		}
	}

	// Validation
	for (int i = 0; i < bootloader_reserved_region_count; i++) {
		if(!strcmp((char *)bootloader_reserved_regions[i].compatible, "PANIC") || !strcmp((char *)bootloader_reserved_regions[i].reg, "PANIC"))
			panic("Unable to find reserved memory regions!");
	}
}

void get_board_rev(void)
{
	char *np;
	int offset;
	int len, ret = 0;

	u32 bootloader_fdt_location = readl(FDT_POINTER_ADDRESS);
	void *bootloader_fdt = (void*)(uintptr_t)bootloader_fdt_location;

	int count = 0;

	ret = fdt_check_header(bootloader_fdt);
	if (ret) {
		printf("libfdt fdt_check_header(): %s\n", fdt_strerror(ret));
	}

	offset = fdt_path_offset(bootloader_fdt, "/");
	if (offset < 0) {
		printf("libfdt fdt_path_offset(): %s\n", fdt_strerror(offset));
	}

	np = (char*)fdt_getprop(bootloader_fdt, offset, "model", &len);
	if (len <= 0) {
		printf("libfdt fdt_getprop(): %s\n", fdt_strerror(len));
	}

	char *token = strtok(np, " ");

	while (token) {
		if (isdigit(token[count])) {
			count++;
			if (count == 1) {
				board_rev = atoi(token);
				break;
			}
		}
		token = strtok(NULL, " ");
	}

	if(board_rev == -1)
		panic("Failed to find board_rev!");
}

unsigned int get_charger_mode(void)
{
	return charger_mode;
}

static void read_chip_id(void)
{
	s5p_chip_id[0] = readl(EXYNOS9830_PRO_ID + CHIPID0_OFFSET);
	s5p_chip_id[1] = readl(EXYNOS9830_PRO_ID + CHIPID1_OFFSET) & 0xFFFF;
}

static void read_chip_rev(void)
{
	unsigned int val = readl(EXYNOS9830_PRO_ID + CHIPID_REV_OFFSET);
	s5p_chip_rev.main = (val >> 20) & 0xf;
	s5p_chip_rev.sub = (val >> 16) & 0xf;
}


static void display_rst_stat(u32 rst_stat)
{
	u32 temp = rst_stat & (WARM_RESET | LITTLE_WDT_RESET | BIG_WDT_RESET | PIN_RESET);

	switch(temp) {
	case WARM_RESET:
		printf("rst_stat:0x%x / WARMRESET\n", rst_stat);
		break;
	case LITTLE_WDT_RESET:
		printf("rst_stat:0x%x / CL0_WDTRESET\n", rst_stat);
		break;
	case BIG_WDT_RESET:
		printf("rst_stat:0x%x / CL2_WDTRESET\n", rst_stat);
		break;
	case PIN_RESET:
		printf("rst_stat:0x%x / PINRESET\n", rst_stat);
		break;
	default:
		printf("rst_stat:0x%x\n", rst_stat);
		break;
	}
}

static void read_dram_info(void)
{
        u64 dram_manufacturer_info = readq(0x206CC00); // Got address from reverse engineering S-LK
        dram_manufacturer_info >>= 8;
        dram_manufacturer_info &= 0xFF;

	dram_info.ram_size = (readq(DRAM_SIZE_INFO) >> 20) / 1024;

	u32 dram_information = readl(0x02062C00); // Got address from reverse engineering S-LK
	u32 dram_ddr_type = dram_information & 0xF; // Isolate type

	switch(dram_ddr_type)
	{
		case 1:
			dram_info.ram_type = (char *)"LPDDR4";
			break;

		case 2:
			dram_info.ram_type = (char *)"LPDDR4X";
			break;

		case 4:
			dram_info.ram_type = (char *)"LPDDR5";
			break;

		default:
			break;
	}

	switch(dram_manufacturer_info)
	{
		case 0x01:
			dram_info.ram_manufacturer = (char *)"Samsung";
			break;

		case 0x06:
			dram_info.ram_manufacturer = (char *)"SKHynix";
			break;

		case 0xFF:
			dram_info.ram_manufacturer = (char *)"Micron";
			break;

		default:
			break;
	}
}

#define EL3_MON_VERSION_STR_SIZE (180)

static void print_el3_monitor_version(void)
{
	char el3_mon_ver[EL3_MON_VERSION_STR_SIZE] = { 0, };

	if (*(unsigned int *)DRAM_BASE == 0xabcdef) {
		/* This booting is from eMMC/UFS. not T32 */
		get_el3_mon_version(el3_mon_ver, EL3_MON_VERSION_STR_SIZE);
		printf("\nEL3 Monitor information: \n");
		printf("%s\n\n", el3_mon_ver);
	}
}

#ifdef CONFIG_EXYNOS_BOOTLOADER_DISPLAY
extern int display_drv_init(void);
void display_panel_init(void);

static void initialize_fbs(void)
{
	memset((void *)CONFIG_DISPLAY_LOGO_BASE_ADDRESS, 0, LCD_WIDTH * LCD_HEIGHT * 4);
	memset((void *)CONFIG_DISPLAY_FONT_BASE_ADDRESS, 0, LCD_WIDTH * LCD_HEIGHT * 4);
}
#endif

void arm_generic_timer_disable(void)
{
	mask_interrupt(ARCH_TIMER_IRQ);
}

void platform_early_init(void)
{
	unsigned int rst_stat = readl(EXYNOS9830_POWER_RST_STAT);
	unsigned int dfd_en = readl(EXYNOS9830_POWER_RESET_SEQUENCER_CONFIGURATION);

	if (!((rst_stat & (WARM_RESET | LITTLE_WDT_RESET)) &&
			dfd_en & EXYNOS9830_EDPCSR_DUMP_EN)) {
		invalidate_dcache_all();
		cpu_common_init();
		clean_invalidate_dcache_all();
	}

	read_chip_id();
	read_chip_rev();

#ifdef CONFIG_EXYNOS_BOOTLOADER_DISPLAY
	display_panel_init();
	initialize_fbs();
#endif
	set_first_boot_device_info();

	uart_console_init();
	printf("lk3rd built on %s, at %s\n", __DATE__, __TIME__);
	printf("Welcome to lk3rd!\n");
	dss_boot_cnt();

	arm_gic_init();
	writel(1 << 8, EXYNOS9830_MCT_G_TCON);
	arm_generic_timer_init(ARCH_TIMER_IRQ, 26000000);
}

static void print_acpm_version(void)
{
#ifdef EXYNOS_ACPM_BASE
	unsigned int i;
	unsigned int plugins, num_plugins, plugin_address, plugin_ops_address;
	struct plugin *plugin;
	struct plugin_ops *plugin_ops;
	char *build_info;

	/* Check ACPM STACK Magic */
	if (readl(EXYNOS_ACPM_MAGIC) != ACPM_MAGIC_VALUE)
		return;

	build_info = (char *)EXYNOS_ACPM_APSHARE + APSHARE_BUILDINFO_OFFSET;
	printf("ACPM: Framework's  version is %s %s\n", build_info,
	       build_info + BUILDINFO_ELEMENT_SIZE);

	plugins = readl(EXYNOS_ACPM_APSHARE);
	num_plugins = readl(EXYNOS_ACPM_APSHARE + 4);

	for (i = 1; i < num_plugins; i++) {
		plugin_address = plugins + sizeof(struct plugin) * i;
		if (readl(get_acpm_plugin_element(plugin, is_attached)) == 1) {
			plugin_ops_address = readl(get_acpm_plugin_element(plugin, plugin_ops));
			build_info = (char *)get_acpm_plugin_element(plugin_ops, info);
			printf("ACPM: Plugin(id:%d) version is %s %s\n",
			       (int)readl(get_acpm_plugin_element(plugin, id)),
			       build_info, build_info + BUILDINFO_ELEMENT_SIZE);
		}
	}
#endif /* ifdef EXYNOS_ACPM_BASE */
}

void sanitise_persistent_storage(void)
{
	int option_enabled;

        option_enabled = lk3rd_get_mainline_quirks();
	if(option_enabled != 0 && option_enabled != 1) // Not a sane value
		lk3rd_switch_mainline_quirks(false);

	option_enabled = lk3rd_get_kaslr_status();
	if(option_enabled != 0 && option_enabled != 1) // Not a sane value  
		lk3rd_switch_kaslr_status(true);

}

void reset_screen(void);

void platform_init(void)
{
	u32 ret = 0;
	u32 rst_stat = readl(POWER_RST_STAT);

	display_flexpmu_dbg();
	print_acpm_version();

	display_rst_stat(rst_stat);
	//get_bootloader_cmdline();
	//get_bootloader_reserved_memory();
	//get_board_rev();
	bootloader_cmdline = (char *)"root=/dev/ram0 androidboot.boot_devices=13100000.ufs androidboot.hardware=exynos990 bcm_setup=0xffffff80f8e00000 firmware_class.path=/vendor/firmware reserve-fimc=0xffffff90f9fe0000 nohugeiomap epx_activate=true fpsimd_check_context=y rcupdate.rcu_expedited=1 corememsize=8G cgroup.memory=nokmem loop.max_part=7 androidboot.bore_cnt=295 sec_debug.pcb_offset=7346944 sec_debug.smd_offset=7348992 sec_debug.lpddr4_size=12.0 sec_debug.dram_info=01,07,00,12G sec_debug.pwrsrc_rs=0x0000000820000000 sec_debug.reset_reason=7 sec_reset.reset_reason=7 sec_debug.reset_rwc=0 console=ram loglevel=4 sec_debug.level=0 sec_watchdog.sec_pet=5 androidboot.debug_level=0x4f4c androidboot.force_upload=0x0 sec_audio_debug.debug_level=0x4f4c sec_debug.dump_sink=0x0 sec_debug.upload_count=0 androidboot.dram_info=01,07,00,12G androidboot.ddr_size=12 androidboot.ap_serial=0x09F64EC2F5C0 sec_debug.charging_offset=7340592 sec_debug.wireless_offset=7340632 sec_debug.pd_hv_offset=7340644 androidboot.fmm_lock=0 sec_debug.fmm_lock_offset=7340628 softdog.soft_margin=100 softdog.soft_panic=1 androidboot.sn.param.offset=7343024 androidboot.im.param.offset=7342864 androidboot.me.param.offset=7342944 androidboot.pr.param.offset=7343104 androidboot.sku.param.offset=7343184 androidboot.prototype.param.offset=7351040 androidboot.recovery_offset=7355136 ess_setup=0xfd900000 sec_debug_next=0x1000000@0x91200000 charging_mode=0x3030 wireless_ic=0x20014440 pd_disable=0x30 s3cfb.bootloaderfb=0xf1000000 lcdtype=8454403 mcd-panel.boot_panel_id=8454403 androidboot.carrierid.param.offset=7340596 androidboot.carrierid=EUX consoleblank=0 ehci_hcd.park=3 oops=panic pmic_info=43 ccic_info=1 fg_reset=0 androidboot.emmc_checksum=3 androidboot.sales.param.offset=7340568 sales_code=EUX androidboot.bootloader=G981BXXSMHXK1 androidboot.selinux=enforcing androidboot.ucs_mode=0 androidboot.revision=22 androidboot.warranty_bit=1 androidboot.wb.hs=030c androidboot.rp=22 androidboot.wb.snapQB=CUSTOM sec_debug.bin=N androidboot.hmac_mismatch=0 androidboot.sec_atd.tty=/dev/ttySAC0 androidboot.serialno=RFCN30JLS7L snd_soc_core.pmdown_time=1000 androidboot.cp_reserved_mem=off androidboot.dtbo_idx=4 androidboot.fmp_config=0 androidboot.em.did=2009f64ec2f5c011 androidboot.em.model=SM-G981B androidboot.em.status=0x0 androidboot.em.rdx_dump=false androidboot.sb.debug0=0x0 androidboot.verifiedbootstate=green androidboot.svb.ver=SVB1.0 androidboot.ulcnt=10 androidboot.subpcb=0 androidboot.slavepcb=1 androidboot.sysup.edtbo=0@7381760 androidboot.sysup.param=7361280 androidboot.hdm_status=NONE s3c2410_wdt.tmr_atboot=1 androidboot.vup=0 androidboot.asb=0 sec_bootstat.boot_time_bl1=100 sec_bootstat.boot_time_bl2=184 sec_bootstat.boot_time_bl3=1706";
	board_rev = 22;
	read_dram_info();
	pmic_init();
	display_pmic_info();
#ifdef CONFIG_SUB_PMIC_S2DOS05
	pmic_init_s2dos05();
#else
	sub_pmic_s2mpb02_init();
#endif
#ifdef CONFIG_S2MU106_CHARGER
	s2mu106_charger_init();
	fg_init_s2mu106();
#endif

	ufs_alloc_memory();
	ufs_init(2);
	ret = ufs_set_configuration_descriptor();
	if (ret == 1)
		ufs_init(2);

	/*
	 * Initialize mmc for all channel.
	 * Sometimes need mmc device when it is not boot device.
	 * So always call mmc_init().
	 */
#ifndef CONFIG_SKIP_MMC_INIT
	mmc_init(MMC_CHANNEL_SD);
#else
	printf("Device does not have an SD card slot! Skip SD init\n");
#endif
	part_init();

	ret = display_drv_init();

	writel(0x1281, 0x19050070);
	//dss_fdt_init();
	dfd_get_dbgc_version();
	if (rst_stat & (WARM_RESET | LITTLE_WDT_RESET))
		dfd_run_post_processing();

	//dfd_display_core_stat();
	reset_screen();
	if (true) {
		unsigned int dfd_en =
			readl(EXYNOS9830_POWER_RESET_SEQUENCER_CONFIGURATION);
		unsigned int rst_stat = readl(EXYNOS9830_POWER_RST_STAT);

		/* read secure chip state */
		if (read_secure_chip() == 0)
			printf("Secure boot is disabled (non-secure chip)\n");
		else if (read_secure_chip() == 1)
			printf("Secure boot is enabled (test key)\n");
		else if (read_secure_chip() == 2)
			printf("Secure boot is enabled (secure chip)\n");
		else
			printf("Can not read secure chip state\n");

		if ((rst_stat & (WARM_RESET | LITTLE_WDT_RESET)) &&
		      (dfd_en & EXYNOS9830_EDPCSR_DUMP_EN)) {
			/* in case of dumpgpr, do not load ldfw/sp */
			printf("Dumpgpr mode. do not load ldfw/sp .\n");
			goto by_dumpgpr_out;
		}

		if (!init_keystorage())
			printf("keystorage: init done successfully.\n");
		else
			printf("keystorage: init failed.\n");

		if (!init_ssp())
			printf("ssp: init done successfully.\n");
		else
			printf("ssp: init failed.\n");

		if (!init_ldfws())
			printf("ldfw: init done successfully.\n");
		else
			printf("ldfw: init failed.\n");

#if defined(CONFIG_USE_RPMB)
		rpmb_key_programming();
#if defined(CONFIG_USE_AVB20)
		rpmb_load_boot_table();
#endif
#endif
		ret = (u32)init_sp();
		if (!ret)
			printf("secure_payload: init done successfully.\n");
		else
			printf("secure_payload: init failed.\n");

		/* Enabling H-Arx */
		if (s5p_chip_rev.main >= SOC_REVISION_EVT1) {
			if (load_and_init_harx()) {
				printf("CAN NOT enter EL2\n");
			} else {
				if (load_and_init_harx_plugin(EXYNOS_HARX_PLUGIN_PART_NAME,
								EXYNOS_HARX_PLUGIN_BASE_ADDR))
					printf("There is no H-Arx plug-in\n");
			}
		}

		writel(0x1281, 0x19050070);
by_dumpgpr_out:
		print_el3_monitor_version();

		printf("Register handler1\n");

		if(exynos_smc(0xb2000102,0xb2000202,(u64)tima_smc_handler,0x98000801))
		{
			printf("Failed to register handler1!\n");
			while(1);
		}

		printf("Register handler2\n");

		if(exynos_smc(0xb2000102,0xb2000201,(u64)spi_handler,0x98000601))
		{
			printf("Failed to register handler2!\n");
			while(1);
		}
		else
		{
			printf("Disable handler registration!\n");
			exynos_smc(0xb2000104,0,0,0);
		}
	}

	display_tmu_info();
	display_trip_info();

	display_dvfs_info();

	chg_init_max77705();

	sanitise_persistent_storage();

	if(get_boot_device() == BOOT_USB)
	{
		writel(REBOOT_MODE_LK3RD_USB, EXYNOS9830_POWER_SYSIP_DAT0);
		writel(0x1281, 0x19050070);
	}
}
