// SPDX-License-Identifier: GPL-2.0
/*
 * conn_probe.c —— MT6897 EMI 保护（SSMPU / NSMPU / EMI-MPU）只读取证 + ATF SMC 通路
 *
 * 前面几轮已经钉住的（不再重复测）：
 *   - connsys EMI [0x8cc00000,0x8d900000) 对 AP **读得出、写不进**（RO+0 / RO+0x4954e0 /
 *     RO-end / RW+0 / RW+0x1000 / RW+0x400000 六处全 DROPPED），紧邻的 wfdma 池
 *     0x8d900000 可写 ⇒ 屏蔽边界正好落在 LK 那段 consys_emi_reserved 的两端。
 *   - EMI base remap（bus_cr+0x354/0x358、host_csr_top+0x354）AP 也写不进。
 *   - conn_infra 掉电时整域回毒值 0x00010002；本轮不碰 conn 域，所以不用等活窗口。
 *
 * v7 只回答一个问题：
 *   connsys EMI 那段"读得出、写不进"，是不是 SSMPU/NSMPU 的一条区描述符造成的？
 *       厂商 recovery 模块里有 smpu.ko / emi-mpu.ko / emi-slb.ko / mkp.ko，
 *       DT 节点给的是 nsmpu@10351000、ssmpu@10355000（sr-cnt=0x3f ⇒ 63 条安全区）。
 *       把这两块寄存器读出来，看有没有一条区的范围覆盖 0x8cc00000..0x8d900000。
 *       命中 ⇒ 屏蔽确实在 SSMPU 层，接下来只谈"谁能改"；
 *       全空 ⇒ 屏蔽来自 AP 看不见的层面（BL2/LK 固定保留），改 DT 基址这条路要换方向。
 *   顺带：对 consys EMI 全区做 64K 粒度的内容取样，分辨"这片区是空的（LK 没装
 *   CONN_RO）"还是"装着东西（LK 装了、只是 AP 不让写）"。
 *
 * 安全性：只 ioremap + readl，唯一的两处写是 consys EMI base+0（本来就写不进）
 * 和 wfdma 池（我们 DT 里 no-map 自己占的）。不写任何保护寄存器，不发任何 SMC。
 * init 返回非 0 ⇒ 打印完自动卸载。
 */
#include <linux/module.h>
#include <linux/io.h>
#include <linux/kernel.h>

#define PFX "XAGA-MPU "

/* 被怀疑的 EMI 物理区间 */
#define CONSYS_EMI_PH	0x8cc00000UL
#define CONSYS_EMI_SZ	0x00d00000UL
#define WIFI_EMI_PH	0x8d900000UL

/* 保护控制器 —— **只留厂商 stock Linux 驱动真的会在 AP 上读写的那两块**。
 *
 * ⚠️ 2026-09-19 血账：上一版这个模块还带了 emimpu@10226000、emi-mpu-apb@10330000
 * 五个 SMC（smc(0xC200050B, sub) sub=2/7/8/9/0xa），insmod 之后 AP **立刻整体挂死**、
 * 掉出 USB，只能长按电源键强制重启。二者之一（或组合）就是致命的：
 *   - 未知 sub 的 SSMPU 服务可能真的在**改区表**，参数全 0 可能把 AP 自己挡在 DRAM 外；
 *   - 0x10226000/0x10330000 属内存控制器侧，时钟没开时 AP 读=总线挂死。
 * 所以这里只保留：ssmpu/nsmpu —— 判据是厂商树里 **smpu.ko 的 of_match 就是
 * compatible="mediatek,smpu"**，它在 stock Linux 里真的在 AP 上 probe、
 * 读这些块、还带中断；AP 可读是**被证过**的。emimpu/emi-mpu-apb 不再碰。
 * SMC 一条都不发（0xC2000534 已经测过是 SMC_UNK，0xC200050B 会挂死，没有第三条值得试）。
 */
static const struct {
	phys_addr_t pa;
	size_t sz;
	const char *tag;
} blk[] = {
	{ 0x10355000UL, 0x1000, "ssmpu " },	/* skp，sr-cnt=0x3f ⇒ 63 条安全区 */
	{ 0x10351000UL, 0x1000, "nsmpu " },	/* nkp */
};

/* 地址在 MTK 区描述符里可能按 4K/64K 编码，三种都试；窗口放宽一点防漏。
 * 注意 >>16 那种编码值只有 12 bit，必然有别名，所以只报、不据此下结论。
 */
static int addr_in_window(u32 w)
{
	if (w >= 0x8c000000u && w <= 0x8e000000u)
		return 1;			/* 原样 */
	if (w >= (0x8c000000u >> 12) && w <= (0x8e000000u >> 12))
		return 2;			/* addr>>12（4K 页） */
	if (w >= (0x8c0u) && w <= (0x8e0u))
		return 4;			/* addr>>20（1M 粒度） */
	return 0;
}

static void dump_blk(const char *tag, phys_addr_t pa, size_t sz)
{
	void __iomem *m;
	size_t off;
	int hits = 0;

	m = ioremap(pa, sz);
	if (!m) {
		pr_info(PFX "%s @0x%llx ioremap FAILED\n", tag, (u64)pa);
		return;
	}

	/* 只印非零行：这几大块里绝大多数偏移是 0，全印会把 dmesg 冲爆 */
	for (off = 0; off < sz; off += 32) {
		u32 w[8];
		int i, nz = 0;

		for (i = 0; i < 8; i++) {
			w[i] = readl(m + off + i * 4);
			nz += !!w[i];
		}
		if (!nz)
			continue;
		pr_info(PFX "%s %04x: %08x %08x %08x %08x %08x %08x %08x %08x\n",
			tag, (u32)off, w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7]);
	}

	for (off = 0; off < sz; off += 4) {
		u32 v = readl(m + off);
		int enc = addr_in_window(v);

		if (!enc)
			continue;
		pr_info(PFX "%s HIT %04x = %08x (enc=%s) raw_win=0x%llx..0x%llx\n",
			tag, (u32)off, v,
			enc == 1 ? "addr" : enc == 2 ? "addr>>12" : "addr>>20",
			(u64)CONSYS_EMI_PH, (u64)(CONSYS_EMI_PH + CONSYS_EMI_SZ));
		hits++;
	}
	pr_info(PFX "%s @0x%llx size=0x%zx window_hits=%d\n", tag, (u64)pa, sz, hits);
	iounmap(m);
}

/* EMI 区内容粗取样：每 64K 读 1 个字。145 次读，不扫全区（整区扫会把 insmod 卡死）。
 * 目的是分辨「LK 把 CONN_RO 装进去了」还是「这片区基本没被写过」。
 */
static void sample_emi(const char *tag, phys_addr_t pa, size_t sz)
{
	void __iomem *m = ioremap(pa, sz);
	size_t off;
	int i = 0;

	if (!m) {
		pr_info(PFX "MAP %s ioremap(0x%llx+0x%zx) failed\n", tag, (u64)pa, sz);
		return;
	}
	/* 一行 8 个采样点 = 512K */
	for (off = 0; off < sz; off += 0x10000) {
		char buf[128];
		int n = 0;
		int j;

		n = scnprintf(buf, sizeof(buf), "MAP %s +%03llxK:", tag, (u64)(off >> 10));
		for (j = 0; j < 8 && off + j * 0x10000 < sz; j++)
			n += scnprintf(buf + n, sizeof(buf) - n, " %08x",
				       readl(m + off + j * 0x10000));
		pr_info(PFX "%s\n", buf);
		i += j;
		off += (j - 1) * 0x10000;
	}
	pr_info(PFX "MAP %s done: %d samples, 1 word per 64K over 0x%zx\n", tag, i, sz);
	iounmap(m);
}

static void wr_test(const char *tag, phys_addr_t pa)
{
	void __iomem *m = ioremap(pa, 0x100);
	u32 before, after;

	if (!m)
		return;
	before = readl(m);
	writel(0x5a5a1234, m);
	mb();
	after = readl(m);
	writel(before, m);
	mb();
	pr_info(PFX "W %-10s 0x%llx before=%08x after=%08x %s\n",
		tag, (u64)pa, before, after,
		after == 0x5a5a1234 ? "WRITABLE" : "DROPPED");
	iounmap(m);
}

static int __init conn_probe_init(void)
{
	int i;

	pr_info(PFX "===== v7: SSMPU/NSMPU 区描述符（只读，不发 SMC）=====\n");

	wr_test("consys+0", CONSYS_EMI_PH);
	wr_test("wfdma+0", WIFI_EMI_PH);

	for (i = 0; i < ARRAY_SIZE(blk); i++)
		dump_blk(blk[i].tag, blk[i].pa, blk[i].sz);

	sample_emi("consys", CONSYS_EMI_PH, CONSYS_EMI_SZ);

	pr_info(PFX "done (unloading)\n");
	return -EIO;
}

static void __exit conn_probe_exit(void)
{
}

module_init(conn_probe_init);
module_exit(conn_probe_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TB375FC read-only SSMPU/NSMPU/EMI-MPU region descriptor dump");
