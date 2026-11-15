/* regdump.c —— 只读寄存器快照模块 v2
 *
 * 用途：加载到厂商 recovery 内核（6.1.138 GKI）里，读出 SPI2 控制器和
 *       触摸引脚 IOCFG 的“工作态”寄存器值，与 mainline 的 snapshot 对比。
 *
 * 安全性：只做 ioremap + readl + printk，不写任何寄存器，不驻留内核
 *         （init 返回非 0 ⇒ 打印完自动卸载）。
 *
 * v2：IOCFG 采集扩展到触摸相关引脚（bank0 pins8-15 = SPI，bank1 pin60/61/63 = 复位/中断/1.8V）
 */
#include <linux/module.h>
#include <linux/io.h>
#include <linux/kernel.h>

#define SPI2_BASE     0x11012000UL
#define SPI2_SIZE     0x100
#define SPI_REG_MAX   0x70

#define IOCFG_BASE    0x10005000UL
#define IOCFG_SIZE    0x900

/* 需要采集的 IOCFG 寄存器（偏移 : 名称） */
static const struct {
	unsigned int off;
	const char *name;
} iocfg_regs[] = {
	{ 0x000, "DIR0 (pins0-31)" },
	{ 0x010, "DIR1 (pins32-63)" },
	{ 0x100, "DOUT0" },
	{ 0x110, "DOUT1" },
	{ 0x200, "DIN0" },
	{ 0x210, "DIN1" },
	{ 0x300, "MODE0 (pins0-7)" },
	{ 0x310, "MODE1 (pins8-15) <-- SPI pins" },
	{ 0x370, "MODE7 (pins56-63) <-- reset/int/1p8v" },
	{ 0x400, "IES0 (input enable)" },
	{ 0x410, "IES1" },
	{ 0x500, "SMT0 (schmitt)" },
	{ 0x510, "SMT1" },
	{ 0x600, "PU0 (pull-up)" },
	{ 0x610, "PU1" },
	{ 0x700, "PD0 (pull-down)" },
	{ 0x710, "PD1" },
	{ 0x800, "DRV0 (pins0-15, 2bit/pin)" },
	{ 0x804, "DRV1 (pins16-31)" },
	{ 0x820, "DRV8 (pins32-47)" },
	{ 0x824, "DRV9 (pins48-63) <-- reset/int/1p8v" },
};

static int __init regdump_init(void)
{
	void __iomem *spi, *iocfg;
	int i;

	spi = ioremap(SPI2_BASE, SPI2_SIZE);
	if (!spi) {
		pr_err("regdump: ioremap(SPI2) failed\n");
		return -ENOMEM;
	}

	pr_info("regdump: ===== SPI2 @ %#lx =====\n", SPI2_BASE);
	for (i = 0; i < SPI_REG_MAX; i += 4)
		pr_info("regdump: spi[%02x] = %08x\n", i, readl(spi + i));
	iounmap(spi);

	iocfg = ioremap(IOCFG_BASE, IOCFG_SIZE);
	if (!iocfg) {
		pr_err("regdump: ioremap(IOCFG) failed\n");
		return -ENOMEM;
	}

	pr_info("regdump: ===== IOCFG @ %#lx =====\n", IOCFG_BASE);
	for (i = 0; i < ARRAY_SIZE(iocfg_regs); i++)
		pr_info("regdump: iocfg[%03x] = %08x   %s\n",
			iocfg_regs[i].off,
			readl(iocfg + iocfg_regs[i].off),
			iocfg_regs[i].name);
	iounmap(iocfg);

	pr_info("regdump: done (unloading)\n");

	/* 返回非 0：打印完立即卸载，不驻留 */
	return -EIO;
}

static void __exit regdump_exit(void)
{
}

module_init(regdump_init);
module_exit(regdump_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TB375FC read-only SPI2/IOCFG register snapshot");
