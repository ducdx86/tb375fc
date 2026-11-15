// SPDX-License-Identifier: GPL-2.0
/*
 * conn_watch.c —— 盯住"一次真实的 connsys 上电窗口"，只在数值变化时打印
 *
 * 要判的事（这是现在唯一还开着的分歧）：
 *   v18 的 XAGA-EMIDL 看到驱动往 connsys EMI 拷 ROM patch 之后回读和写入不一致（"写没落地"）；
 *   但同一块板子、同一个地址，2026-09-19 05:27 的纯模块写测试是
 *       W consys+0 0x8cc00000 before=00000000 after=5a5a1234 WRITABLE
 *   而且 SSMPU/NSMPU 里没有任何覆盖该段的区 ⇒ **"AP 被 MPU 挡住"已经不成立**。
 *   两次结果唯一的差别是**测量时刻**：一个在 POS 里，一个在 POS 结束后（域已掉电）。
 *   ⇒ 要么"掉电时能写、上电时不能写"（conn 侧在活着的时候接管了这块），
 *     要么 ROM patch 其实一直写进去了、失败在别处。
 *
 * 所以这个模块**只读**，在 30 秒窗口里每 100ms 采一次，只在变化时打一行。
 * 配合 `tools/conn-probe/trigger_pos.py` 从另一个会话重跑一次 POS，就能把
 * "patch 到底有没有出现在 EMI 基址" 和 "电源状态" 放在同一条时间轴上看。
 *
 * ⚠️ 只读，绝不写任何寄存器。且只读**已经被驱动自己读过无数次**的块：
 *    SPM 0x10001000、conn_infra_bus_cr 0x1804b000、conn_host_csr_top 0x18060000、
 *    wf_top_cfg_on 0x184c1000（= coda 头里的 WF_TOP_CFG_ON_BASE）、connsys EMI 0x8cc00000。
 *    2026-09-19 的教训：读 emimpu@10226000 / emi-mpu-apb@10330000 或发
 *    smc(0xC200050B,...) 会把 AP 直接挂死，这里一概不碰。
 *
 * insmod 会阻塞约 WATCH_SECONDS 秒（这是故意的：不驻留、不建 kthread），
 * 所以触发 POS 必须在**另一个 SSH 会话**里做。init 返回非 0 ⇒ 采完自动卸载。
 */
#include <linux/module.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/delay.h>

#define PFX "XAGA-WATCH "

/* ⚠️ SPM 是 **0x1c001000**，不是 0x10001000。
 * 0x10001000 是 INFRACFG_AO（我们枚举里的 idx0）。判据：驱动自己的 XAGA-LIVE 打的
 * `spm E04=c200000d` 走的是 CONN_REG_SPM_ADDR = reg_base_addr[SPM_BASE_INDEX=22]，
 * 而 kernel/dts/mt6897-connectivity.dtsi:56 第 23 个 reg 就是 <0x0 0x1c001000 …>。
 * 早先这里写 0x10001000 是把 infracfg_ao 当成了 SPM —— 那一列会一直不变，白采。 */
#define SPM_PH		0x1c001000UL	/* +0xE04: conn_infra 电源状态（0xc200000d=全开） */
#define BUS_CR_PH	0x1804b000UL	/* +0x100: 掉电时整块回毒值 0x00010002 */
#define HOST_CSR_PH	0x18060000UL	/* +0xB10: WF_ON_MONFLG（ROM done 的信箱） */
#define WF_TOP_CFG_PH	0x184c1000UL	/* +0x604: ROMCODE_INDEX */
#define EMI_PH		0x8cc00000UL

#define WATCH_MS	30000
#define STEP_MS		100

#define POISON_CONN	0x00010002u
#define POISON_WF		0xdead0001u
/* ROM patch 头前 20 字节 "2021120611340100ALPS" 的小端 u32；基址上出现它 = patch 真在里面 */
#define ROM_SIG0	0x31323032u

static struct sample {
	u32 spm_e04, buscr_100, monflg, romcode, emi0, emi4, emi8;
} last = { 0xdeadbeef, 0xdeadbeef, 0xdeadbeef, 0xdeadbeef, 0xdeadbeef, 0xdeadbeef, 0xdeadbeef };

static int __init conn_watch_init(void)
{
	void __iomem *spm, *bcr, *hcsr, *wfcfg, *emi;
	int i, n = WATCH_MS / STEP_MS;
	struct sample s;

	spm = ioremap(SPM_PH, 0x1000);
	bcr = ioremap(BUS_CR_PH, 0x1000);
	hcsr = ioremap(HOST_CSR_PH, 0x1000);
	wfcfg = ioremap(WF_TOP_CFG_PH, 0x1000);
	emi = ioremap(EMI_PH, 0x1000);
	if (!spm || !bcr || !hcsr || !wfcfg || !emi) {
		pr_info(PFX "ioremap failed\n");
		goto out;
	}

	pr_info(PFX "===== 采样 %d ms × %d（只读；现在去另一个会话触发 POS）=====\n",
		STEP_MS, n);

	for (i = 0; i < n; i++) {
		s.spm_e04 = readl(spm + 0xE04);
		s.buscr_100 = readl(bcr + 0x100);
		s.monflg = readl(hcsr + 0xB10);
		s.romcode = readl(wfcfg + 0x604);
		s.emi0 = readl(emi);
		s.emi4 = readl(emi + 4);
		s.emi8 = readl(emi + 8);

		if (s.spm_e04 != last.spm_e04 || s.buscr_100 != last.buscr_100 ||
		    s.monflg != last.monflg || s.romcode != last.romcode ||
		    s.emi0 != last.emi0 || s.emi4 != last.emi4 || s.emi8 != last.emi8) {
			pr_info(PFX "t=%4dms spm.E04=%08x buscr.100=%08x monflg=%08x romcode=%08x | emi[+0]=%08x [+4]=%08x [+8]=%08x%s%s\n",
				i * STEP_MS, s.spm_e04, s.buscr_100, s.monflg, s.romcode,
				s.emi0, s.emi4, s.emi8,
				s.buscr_100 == POISON_CONN ? " [CONN-OFF-poison]" : "",
				s.emi0 == ROM_SIG0 ? " [ROM-SIG!]" : "");
			last = s;
		}
		msleep(STEP_MS);
	}

	pr_info(PFX "结束：最后一次 spm.E04=%08x romcode=%08x emi[+0]=%08x\n",
		last.spm_e04, last.romcode, last.emi0);
out:
	if (spm)
		iounmap(spm);
	if (bcr)
		iounmap(bcr);
	if (hcsr)
		iounmap(hcsr);
	if (wfcfg)
		iounmap(wfcfg);
	if (emi)
		iounmap(emi);
	pr_info(PFX "done (unloading)\n");
	return -EIO;
}

static void __exit conn_watch_exit(void)
{
}

module_init(conn_watch_init);
module_exit(conn_watch_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TB375FC read-only sampler across a live connsys power-on window");
