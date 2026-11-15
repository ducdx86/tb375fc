// SPDX-License-Identifier: GPL-2.0
/*
 * emi_peek.c —— 纯只读地把 connsys EMI 窗口抄出来，判"LK 到底有没有往里装 CONN_RO"
 *
 * 背景（2026-09-19 实测，顺序很重要）：
 *   1) 我们内核的 `wlanDownloadEMISection` 一直在往 `0x8cc00000 + 0` 拷 147,076 字节，
 *      而那份文件是 **蓝牙侧 MCU 固件的别名**（md5 和 soc7_0_ram_mcu_1_1_hdr.bin 完全一样）。
 *      更要命的是它 `src[0]=0x00000000` —— 也就是说**它往基址上写的是大段 0**，
 *      会把 LK 可能装好的东西盖掉。所以之前任何"基址读出来是 0"都不能当证据。
 *   2) 现在设备上那份别名已经改名成 `*.moved-away`，`XAGA-FW: request ... ret=-2`，
 *      内核侧**一个字节都不再碰这块**了。⇒ 这一次的读数才是干净的。
 *
 * 判据：原厂 `connsys_wifi.img` = 0x200 头的镜像，头 8 字节是
 *      `88 16 88 58 10 c7 18 00` + ASCII "connsys_wifi"，
 *      载荷从文件偏移 0x200 开始（= 我们手上的 WIFI_RAM_CODE_soc7_0_1_1.bin）。
 *   如果 LK 把 CONN_RO 装进了这块 EMI，那么在某个固定偏移上会看到
 *   "connsys_wifi" 这串 ASCII 或者载荷的开头；全 0 / 全 aaaaaaaa = LK 没装。
 *   （所以这里把 base+0 和 base+0x200 都各抄 64 字节，再抄几个候选偏移 + 全区 64K 步长采样。）
 *
 * ⚠️ 本模块**不写任何东西**，也不发 SMC。只 ioremap + readl。
 *    教训记录：读 emimpu@10226000 / emi-mpu-apb@10330000 或发 smc(0xC200050B,…)
 *    会把 AP 直接挂死，这里一律不碰。
 */
#include <linux/module.h>
#include <linux/io.h>
#include <linux/kernel.h>

#define PFX "XAGA-PEEK "

#define EMI_PH		0x8cc00000UL
#define EMI_SZ		0x00d00000UL

static const unsigned long sites[] = {
	0x0, 0x40, 0x100, 0x200, 0x240, 0x1000, 0x2000,
	0x10000, 0x20000, 0x40000, 0x80000, 0x100000, 0x18d000, 0x200000,
	0x400000, 0x680000, 0xc00000,
};

static void peek(void __iomem *m, unsigned long off)
{
	u32 w[16];
	char buf[256];
	int i, n = 0, nz = 0;

	for (i = 0; i < 16; i++) {
		w[i] = readl(m + off + i * 4);
		nz += !!w[i];
	}
	n = scnprintf(buf, sizeof(buf), "off=%07lx nz=%2d/16:", off, nz);
	for (i = 0; i < 8; i++)
		n += scnprintf(buf + n, sizeof(buf) - n, " %08x", w[i]);
	pr_info(PFX "%s\n", buf);
	n = 0;
	buf[0] = 0;
	/* 第二行：能直接肉眼认出 "connsys_wifi" 这类 ASCII */
	for (i = 0; i < 8; i++)
		n += scnprintf(buf + n, sizeof(buf) - n, " %08x", w[8 + i]);
	pr_info(PFX "off=%07lx        :%s  |%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c\n",
		off + 0x20, buf,
		((u8 *)&w[8])[0], ((u8 *)&w[8])[1], ((u8 *)&w[8])[2], ((u8 *)&w[8])[3],
		((u8 *)&w[8])[4], ((u8 *)&w[8])[5], ((u8 *)&w[8])[6], ((u8 *)&w[8])[7],
		((u8 *)&w[9])[0], ((u8 *)&w[9])[1], ((u8 *)&w[9])[2], ((u8 *)&w[9])[3],
		((u8 *)&w[9])[4], ((u8 *)&w[9])[5], ((u8 *)&w[9])[6], ((u8 *)&w[9])[7]);
}

static int __init emi_peek_init(void)
{
	void __iomem *m;
	unsigned long off;
	int i, row = 0;
	char buf[160];

	m = ioremap(EMI_PH, EMI_SZ);
	if (!m) {
		pr_info(PFX "ioremap(0x%lx, 0x%lx) FAILED\n", EMI_PH, EMI_SZ);
		return -EIO;
	}

	pr_info(PFX "===== connsys EMI %lx+%lx 纯只读（内核侧已不再写这块）=====\n",
		EMI_PH, EMI_SZ);

	for (i = 0; i < ARRAY_SIZE(sites); i++)
		peek(m, sites[i]);

	/* 全区 64K 步长采样，一行 8 个点 */
	for (off = 0; off < EMI_SZ; off += 0x10000) {
		int j, n = 0;

		n = scnprintf(buf, sizeof(buf), "MAP +%03lxK:", off >> 10);
		for (j = 0; j < 8 && off + j * 0x10000 < EMI_SZ; j++)
			n += scnprintf(buf + n, sizeof(buf) - n, " %08x",
				       readl(m + off + j * 0x10000));
		pr_info(PFX "%s\n", buf);
		row += j;
		off += (unsigned long)(j - 1) * 0x10000;
	}

	pr_info(PFX "done: %d 个采样点，未写任何地址（unloading）\n", row);
	iounmap(m);
	return -EIO;
}

static void __exit emi_peek_exit(void)
{
}

module_init(emi_peek_init);
module_exit(emi_peek_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TB375FC read-only connsys EMI content peek (did LK load CONN_RO)");
