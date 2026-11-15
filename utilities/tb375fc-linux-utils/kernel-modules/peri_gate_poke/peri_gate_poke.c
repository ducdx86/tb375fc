// SPDX-License-Identifier: GPL-2.0
/*
 * peri_gate_poke.c — TB375FC 触摸调试用：pericfg_ao 时钟门控 dump / 单位置位
 *
 * 背景：厂商 DT 里 spi2 的 "spi-clk" 是 pericfg_ao(@0x11036000) 的门控 id 0x0e
 *      （名字 spi2_ck），而主线没有 mt6897 时钟驱动、DTS 里用 clk26m 占位，
 *      于是这个门控从未被使能。串行时钟不动 ⇒ 触摸芯片永不回话（读全是 0x00）。
 *
 * 本模块只做两件事：
 *   1) 打印 0x11036000 前 0x100 的非零寄存器 + SPI2 控制器全部寄存器（含 CMD/PAD_SEL）；
 *   2) 可选地把 1<<bit 写进 set_ofs（只置位、绝不清零），用于逐位试探哪个门控位是 SPI2。
 *
 * 用法：
 *   insmod peri_gate_poke.ko                       # 只 dump
 *   insmod peri_gate_poke.ko do_poke=1 set_ofs=0x10 bit=14
 *   rmmod peri_gate_poke                           # 本模块自己可以安全卸载
 */
#include <linux/module.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>

#define PERI_AO_BASE	0x11036000UL
#define PERI_AO_SIZE	0x1000UL
#define SPI2_BASE	0x11012000UL
#define SPI2_SIZE	0x100UL

static unsigned long set_ofs = 0x10;
module_param(set_ofs, ulong, 0444);
MODULE_PARM_DESC(set_ofs, "pericfg_ao SET 寄存器偏移（默认 0x10）");

static unsigned int bit;
module_param(bit, uint, 0444);
MODULE_PARM_DESC(bit, "要置位的门控位号 0-31");

static unsigned int do_poke;
module_param(do_poke, uint, 0444);
MODULE_PARM_DESC(do_poke, "非 0 时执行置位");

static unsigned int dump_len = 0x1000;
module_param(dump_len, uint, 0444);
MODULE_PARM_DESC(dump_len, "pericfg_ao dump 长度（默认 0x1000，只打印非零）");

static unsigned int dump_all;
module_param(dump_all, uint, 0444);
MODULE_PARM_DESC(dump_all, "非 0 时连 0 值也打印");

static void __iomem *pa;
static void __iomem *spi;

static void dump_region(void __iomem *b, unsigned long base, int len)
{
	int i;

	for (i = 0; i < len; i += 4) {
		u32 v = readl(b + i);

		if (v || dump_all)
			pr_info("pgp: 0x%08lx = 0x%08x\n", base + i, v);
	}
}

static void dump_spi2(void)
{
	int i;

	pr_info("pgp: ==== SPI2 @0x11012000 ====\n");
	for (i = 0; i < 0x60; i += 4)
		pr_info("pgp: spi2+0x%02x = 0x%08x\n", i, readl(spi + i));
}

static int __init pgp_init(void)
{
	pa = ioremap(PERI_AO_BASE, PERI_AO_SIZE);
	spi = ioremap(SPI2_BASE, SPI2_SIZE);
	if (!pa || !spi) {
		pr_err("pgp: ioremap 失败\n");
		if (pa)
			iounmap(pa);
		if (spi)
			iounmap(spi);
		return -ENOMEM;
	}

	pr_info("pgp: ==== PERICFG_AO @0x%08lx（len=0x%x）====\n",
		PERI_AO_BASE, dump_len);
	dump_region(pa, PERI_AO_BASE, dump_len);

	dump_spi2();

	if (do_poke) {
		u32 before = readl(pa + set_ofs);

		writel(before | (1u << bit), pa + set_ofs);
		pr_info("pgp: POKE set_ofs=0x%lx bit=%u : 0x%08x -> 0x%08x\n",
			set_ofs, bit, before, readl(pa + set_ofs));
		pr_info("pgp: ==== POKE 之后 ====\n");
		dump_region(pa, PERI_AO_BASE, dump_len);
		dump_spi2();
	}

	return 0;
}

static void __exit pgp_exit(void)
{
	dump_spi2();

	if (pa)
		iounmap(pa);
	if (spi)
		iounmap(spi);
	pr_info("pgp: unloaded\n");
}

module_init(pgp_init);
module_exit(pgp_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TB375FC pericfg_ao gate dump/poke (touch debug)");
