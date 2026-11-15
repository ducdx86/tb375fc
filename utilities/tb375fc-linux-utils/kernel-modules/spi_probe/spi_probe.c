// SPDX-License-Identifier: GPL-2.0
/*
 * tb-spi-probe —— 触摸 SPI 链路 A/B 探针（模块级，不改内核、不刷机）
 *
 * 目的：验证 mt36xxx 触摸读回恒为 00 是否由 "Novatek 驱动把 controller_data
 *       覆盖成 tick_delay=0" 引起。
 *
 * 原理：mtk_spi_hw_init() 在 prepare_message 里每次都用 spi->controller_data
 *       里的 tick_delay 重写 SPI_CMD[24:22]。本模块自己绑定 spi0.0，
 *       把 controller_data 指到自己带 tick_delay 参数的结构上，于是每条消息
 *       都会用我们给的 tick delay —— 用 tick_delay=3 与 =0 各跑一遍即可对照。
 *
 * 用法（先解绑原驱动）：
 *   echo spi0.0 > /sys/bus/spi/drivers/NVT-ts-spi/unbind
 *   insmod spi_probe.ko tick_delay=3 do_power=1
 *   dmesg | grep tbp
 *   insmod spi_probe.ko tick_delay=0 do_power=0     # 对照（芯片已上电）
 *   # 收尾：rmmod spi_probe && echo spi0.0 > /sys/bus/spi/drivers/NVT-ts-spi/bind
 */
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/platform_data/spi-mt65xx.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/of.h>

#define SPI2_BASE	0x11012000UL
#define SPI2_SIZE	0x1000UL
#define GPIO_BASE	0x10005000UL
#define GPIO_SIZE	0x100UL

/* GPIO：DIR = 0x000 + bank*0x10, DOUT = 0x100 + bank*0x10, bank = pin/32 */
#define GPIO_DOUT(pin)	(0x100 + ((pin) / 32) * 0x10)
#define GPIO_DIR(pin)	(0x000 + ((pin) / 32) * 0x10)

/* 触摸模组相关的 SoC 侧 GPIO（历史实测值） */
#define PIN_TP_RESET	60	/* GPIO60 = TP_RESET（高=释放） */
#define PIN_1P8V_EN	63	/* GPIO63 = 1.8V 使能 */
#define PIN_PWR_186	186
#define PIN_PWR_187	187

static unsigned int tick_delay = 3;
module_param(tick_delay, uint, 0644);
MODULE_PARM_DESC(tick_delay, "SPI_CMD 的 tick delay（厂商 DT = 3）");

static unsigned int sample_sel;
module_param(sample_sel, uint, 0644);
MODULE_PARM_DESC(sample_sel, "采样相位");

static unsigned int speed_hz = 1000000;
module_param(speed_hz, uint, 0644);
MODULE_PARM_DESC(speed_hz, "SPI 时钟（0 = 用 DT 的）");

static unsigned int do_power = 1;
module_param(do_power, uint, 0644);
MODULE_PARM_DESC(do_power, "是否做一次上电/复位脉冲");

static unsigned int n_rounds = 3;
module_param(n_rounds, uint, 0644);
MODULE_PARM_DESC(n_rounds, "每个 pattern 重复几轮");

static void __iomem *spi2, *gpio;
static struct mtk_chip_config cc = { .sample_sel = 0, .tick_delay = 3, };

static void gpio_out(u32 off, u32 pin, int val)
{
	u32 shift = pin % 32;
	void __iomem *p = gpio + off + (pin / 32) * 0x10;
	u32 v = readl(p);

	if (val)
		v |= BIT(shift);
	else
		v &= ~BIT(shift);
	writel(v, p);
}

static void power_cycle(void)
{
	gpio_out(GPIO_DIR(PIN_TP_RESET), PIN_TP_RESET, 1);
	gpio_out(GPIO_DIR(PIN_1P8V_EN), PIN_1P8V_EN, 1);
	gpio_out(GPIO_DIR(PIN_PWR_186), PIN_PWR_186, 1);
	gpio_out(GPIO_DIR(PIN_PWR_187), PIN_PWR_187, 1);

	gpio_out(GPIO_DOUT(PIN_1P8V_EN), PIN_1P8V_EN, 1);
	gpio_out(GPIO_DOUT(PIN_PWR_186), PIN_PWR_186, 1);
	gpio_out(GPIO_DOUT(PIN_PWR_187), PIN_PWR_187, 1);
	msleep(20);

	gpio_out(GPIO_DOUT(PIN_TP_RESET), PIN_TP_RESET, 0);
	pr_info("tbp: TP_RESET LOW\n");
	msleep(20);

	gpio_out(GPIO_DOUT(PIN_TP_RESET), PIN_TP_RESET, 1);
	pr_info("tbp: TP_RESET HIGH\n");
	msleep(60);
}

static u32 cmd_snapshot(void)
{
	return spi2 ? readl(spi2 + 0x20) : 0xffffffff;
}

static const u8 p_nvt_wake[] = { 0xff, 0xff, 0xff, 0xff, 0xfa };
static const u8 p_zero[]    = { 0x00, 0x00, 0x00, 0x00 };
static const u8 p_engrst[]  = { 0x5a, 0x00, 0x00, 0x00 };

struct tbp_pat {
	const char *name;
	const u8 *data;
	u32 len;
};

static const struct tbp_pat pats[] = {
	{ "nvt_wake(FF FF FF FF FA)", p_nvt_wake, sizeof(p_nvt_wake) },
	{ "zero(00 x4)",              p_zero,     sizeof(p_zero)     },
	{ "engrst(5A 00 00 00)",      p_engrst,   sizeof(p_engrst)   },
};

static int tbp_probe(struct spi_device *spi)
{
	u8 tx[16], rx[16];
	int p, r;

	pr_info("tbp: ==== probe: tick_delay=%u speed=%u power=%u ====\n",
		tick_delay, speed_hz, do_power);

	if (do_power)
		power_cycle();

	cc.tick_delay = tick_delay;
	cc.sample_sel = sample_sel;
	spi->controller_data = &cc;	/* ← 关键：左右 hw_init 的 CMD 值 */
	spi->mode = SPI_MODE_0;
	if (speed_hz)
		spi->max_speed_hz = speed_hz;

	pr_info("tbp: spi_setup -> %d\n", spi_setup(spi));
	pr_info("tbp: CMD 寄存器 = 0x%08x\n", cmd_snapshot());

	for (p = 0; p < ARRAY_SIZE(pats); p++) {
		for (r = 0; r < n_rounds; r++) {
			struct spi_transfer xfer = { };
			struct spi_message msg;
			int ret;

			memset(tx, 0x00, sizeof(tx));
			memset(rx, 0xaa, sizeof(rx));
			memcpy(tx, pats[p].data, pats[p].len);
			xfer.tx_buf = tx;
			xfer.rx_buf = rx;
			xfer.len = pats[p].len;
			if (speed_hz)
				xfer.speed_hz = speed_hz;

			spi_message_init(&msg);
			spi_message_add_tail(&xfer, &msg);
			ret = spi_sync(spi, &msg);

			pr_info("tbp: [%s] round%d ret=%d rx=%02x %02x %02x %02x %02x  CMD=0x%08x\n",
				pats[p].name, r, ret,
				rx[0], rx[1], rx[2], rx[3], rx[4], cmd_snapshot());
		}
	}

	pr_info("tbp: ==== probe done ====\n");
	return 0;
}

static void tbp_remove(struct spi_device *spi)
{
	pr_info("tbp: removed\n");
}

static const struct of_device_id tbp_of_match[] = {
	{ .compatible = "novatek,NVT-ts-spi" },
	{ .compatible = "novatek,nvt-ts-spi" },
	{ }
};
MODULE_DEVICE_TABLE(of, tbp_of_match);

static struct spi_driver tbp_driver = {
	.driver = {
		.name = "tb-spi-probe",
		.of_match_table = tbp_of_match,
	},
	.probe = tbp_probe,
	.remove = tbp_remove,
};

static int __init tbp_init(void)
{
	int ret;

	spi2 = ioremap(SPI2_BASE, SPI2_SIZE);
	gpio = ioremap(GPIO_BASE, GPIO_SIZE);
	if (!spi2 || !gpio) {
		pr_err("tbp: ioremap 失败\n");
		goto out;
	}

	ret = spi_register_driver(&tbp_driver);
	pr_info("tbp: registered, ret=%d\n", ret);
	return ret;

out:
	if (spi2)
		iounmap(spi2);
	if (gpio)
		iounmap(gpio);
	return -ENOMEM;
}

static void __exit tbp_exit(void)
{
	spi_unregister_driver(&tbp_driver);
	if (spi2)
		iounmap(spi2);
	if (gpio)
		iounmap(gpio);
	pr_info("tbp: unloaded\n");
}

module_init(tbp_init);
module_exit(tbp_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TB375FC touch SPI link A/B probe (tick delay)");
