#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>

static int __init diag_spi_init(void)
{
    struct device *dev;
    struct spi_controller *host;
    struct device *sdev;
    struct spi_device *spi;
    void __iomem *spi_base;
    void __iomem *gpio_base;
    u8 tx_buf[8] = {0};
    u8 rx_buf[8] = {0};
    struct spi_transfer t = {
        .tx_buf = tx_buf,
        .rx_buf = rx_buf,
        .len = 8,
        .speed_hz = 1000000, /* 1MHz test */
    };
    struct spi_message m;
    int ret, i;

    pr_info("=== [DIAG_SPI] Starting SPI2 & Novatek Diagnostics ===\n");

    gpio_base = ioremap(0x10005000, 0x1000);
    if (gpio_base) {
        pr_info("[DIAG_SPI] GPIO Base @ 0x10005000:\n");
        pr_info("  MODE0(0-7)=0x%08x MODE1(8-15)=0x%08x MODE7(56-63)=0x%08x\n",
                readl(gpio_base + 0x0300), readl(gpio_base + 0x0310), readl(gpio_base + 0x0370));
        pr_info("  DIR0=0x%08x DIR1=0x%08x DOUT0=0x%08x DOUT1=0x%08x\n",
                readl(gpio_base + 0x0000), readl(gpio_base + 0x0010),
                readl(gpio_base + 0x0100), readl(gpio_base + 0x0110));
        pr_info("  DIN0=0x%08x DIN1=0x%08x\n",
                readl(gpio_base + 0x0200), readl(gpio_base + 0x0210));
    }

    dev = bus_find_device_by_name(&platform_bus_type, NULL, "11012000.spi");
    if (!dev) {
        pr_err("[DIAG_SPI] Could not find 11012000.spi platform device!\n");
        if (gpio_base) iounmap(gpio_base);
        return -ENODEV;
    }

    host = dev_get_drvdata(dev);
    if (!host) {
        pr_err("[DIAG_SPI] Could not get spi_controller from 11012000.spi!\n");
        put_device(dev);
        if (gpio_base) iounmap(gpio_base);
        return -ENODEV;
    }

    pr_info("[DIAG_SPI] Found spi_controller: bus_num=%d, mode_bits=0x%x, flags=0x%x, num_cs=%d\n",
            host->bus_num, host->mode_bits, host->flags, host->num_chipselect);

    /* Power on SPI controller clock via PM runtime */
    ret = pm_runtime_get_sync(dev);
    pr_info("[DIAG_SPI] pm_runtime_get_sync ret=%d\n", ret);

    spi_base = ioremap(0x11012000, 0x100);
    if (!spi_base) {
        pr_err("[DIAG_SPI] ioremap 0x11012000 failed!\n");
        pm_runtime_put(dev);
        put_device(dev);
        if (gpio_base) iounmap(gpio_base);
        return -ENOMEM;
    }

    pr_info("[DIAG_SPI] SPI2 Registers (powered):\n");
    pr_info("  CFG0 (0x00) = 0x%08x\n", readl(spi_base + 0x00));
    pr_info("  CFG1 (0x04) = 0x%08x\n", readl(spi_base + 0x04));
    pr_info("  CMD  (0x18) = 0x%08x\n", readl(spi_base + 0x18));
    pr_info("  STAT0(0x1c) = 0x%08x\n", readl(spi_base + 0x1c));
    pr_info("  PAD  (0x24) = 0x%08x\n", readl(spi_base + 0x24));
    pr_info("  CFG2 (0x28) = 0x%08x\n", readl(spi_base + 0x28));
    pr_info("  IPM  (0x40) = 0x%08x\n", readl(spi_base + 0x40));

    sdev = bus_find_device_by_name(&spi_bus_type, NULL, "spi0.0");
    if (!sdev) {
        pr_err("[DIAG_SPI] Could not find spi0.0 device!\n");
        goto cleanup;
    }

    spi = to_spi_device(sdev);
    pr_info("[DIAG_SPI] Found spi_device spi0.0: mode=0x%x, max_speed=%u, cs=%d\n",
            spi->mode, spi->max_speed_hz, spi_get_chipselect(spi, 0));

    /* TEST 1: Standard read chip ID using spi_sync */
    /* Set page to 0x001FB104 (CHIP_VER_TRIM_ADDR for NT36532) */
    /* Page cmd: 0xFF, (addr>>15)&0xFF, (addr>>7)&0xFF = 0xFF, 0x3F, 0x62 */
    pr_info("[DIAG_SPI] --- Test 1: Set page 0x001FB104 & read 7 bytes ---\n");
    tx_buf[0] = 0xFF;
    tx_buf[1] = (0x001FB104 >> 15) & 0xFF; /* 0x3F */
    tx_buf[2] = (0x001FB104 >> 7) & 0xFF;  /* 0x62 */
    spi_message_init(&m);
    t.len = 3;
    t.rx_buf = NULL;
    spi_message_add_tail(&t, &m);
    ret = spi_sync(spi, &m);
    pr_info("[DIAG_SPI] Set page ret=%d\n", ret);

    /* Read 7 bytes at addr (0x001FB104 & 0x7F) = 0x04 */
    memset(tx_buf, 0, sizeof(tx_buf));
    memset(rx_buf, 0, sizeof(rx_buf));
    tx_buf[0] = 0x04 | 0x80; /* SPI read mask */
    t.len = 8;
    t.rx_buf = rx_buf;
    spi_message_init(&m);
    spi_message_add_tail(&t, &m);
    ret = spi_sync(spi, &m);
    pr_info("[DIAG_SPI] Read ret=%d: rx = [%02x %02x %02x %02x %02x %02x %02x %02x]\n",
            ret, rx_buf[0], rx_buf[1], rx_buf[2], rx_buf[3], rx_buf[4], rx_buf[5], rx_buf[6], rx_buf[7]);

    /* TEST 2: Try legacy page 0x3F004 */
    pr_info("[DIAG_SPI] --- Test 2: Set page 0x3F004 & read 7 bytes ---\n");
    tx_buf[0] = 0xFF;
    tx_buf[1] = (0x3F004 >> 15) & 0xFF; /* 0x07 */
    tx_buf[2] = (0x3F004 >> 7) & 0xFF;  /* 0xE0 */
    spi_message_init(&m);
    t.len = 3;
    t.rx_buf = NULL;
    spi_message_add_tail(&t, &m);
    ret = spi_sync(spi, &m);

    memset(tx_buf, 0, sizeof(tx_buf));
    memset(rx_buf, 0, sizeof(rx_buf));
    tx_buf[0] = (0x3F004 & 0x7F) | 0x80; /* 0x04 | 0x80 */
    t.len = 8;
    t.rx_buf = rx_buf;
    spi_message_init(&m);
    spi_message_add_tail(&t, &m);
    ret = spi_sync(spi, &m);
    pr_info("[DIAG_SPI] Read legacy ret=%d: rx = [%02x %02x %02x %02x %02x %02x %02x %02x]\n",
            ret, rx_buf[0], rx_buf[1], rx_buf[2], rx_buf[3], rx_buf[4], rx_buf[5], rx_buf[6], rx_buf[7]);

    /* TEST 3: Direct hardware CS pull-down before transfer */
    /* Put pin 12 into GPIO mode (MODE1 nibble 4 = 0), DIR1 bit 12 = 1, DOUT1 bit 12 = 0 */
    pr_info("[DIAG_SPI] --- Test 3: Manual GPIO CS pull-down ---\n");
    if (gpio_base) {
        u32 mode1 = readl(gpio_base + 0x0310);
        u32 dir0 = readl(gpio_base + 0x0000);
        u32 dout0 = readl(gpio_base + 0x0100);

        /* Set pin 12 to GPIO mode 0 */
        writel(mode1 & ~(0xF << 16), gpio_base + 0x0310);
        /* Set pin 12 as output */
        writel(dir0 | (1 << 12), gpio_base + 0x0000);

        /* Drive CS LOW */
        writel(dout0 & ~(1 << 12), gpio_base + 0x0100);
        udelay(10);

        /* Send page write */
        tx_buf[0] = 0xFF;
        tx_buf[1] = 0x3F;
        tx_buf[2] = 0x62;
        spi_message_init(&m);
        t.len = 3;
        t.rx_buf = NULL;
        spi_message_add_tail(&t, &m);
        spi_sync(spi, &m);

        /* Drive CS HIGH then LOW */
        writel(dout0 | (1 << 12), gpio_base + 0x0100);
        udelay(10);
        writel(dout0 & ~(1 << 12), gpio_base + 0x0100);
        udelay(10);

        /* Read 8 bytes */
        memset(tx_buf, 0, sizeof(tx_buf));
        memset(rx_buf, 0, sizeof(rx_buf));
        tx_buf[0] = 0x04 | 0x80;
        t.len = 8;
        t.rx_buf = rx_buf;
        spi_message_init(&m);
        spi_message_add_tail(&t, &m);
        ret = spi_sync(spi, &m);

        /* Drive CS HIGH */
        writel(dout0 | (1 << 12), gpio_base + 0x0100);

        /* Restore pin 12 to Mode 1 (SPI2_CSB) */
        writel(mode1, gpio_base + 0x0310);

        pr_info("[DIAG_SPI] Manual CS Read ret=%d: rx = [%02x %02x %02x %02x %02x %02x %02x %02x]\n",
                ret, rx_buf[0], rx_buf[1], rx_buf[2], rx_buf[3], rx_buf[4], rx_buf[5], rx_buf[6], rx_buf[7]);
    }

    put_device(sdev);

cleanup:
    pr_info("[DIAG_SPI] SPI2 Registers after test:\n");
    pr_info("  CFG0 (0x00) = 0x%08x\n", readl(spi_base + 0x00));
    pr_info("  CFG1 (0x04) = 0x%08x\n", readl(spi_base + 0x04));
    pr_info("  CMD  (0x18) = 0x%08x\n", readl(spi_base + 0x18));
    pr_info("  STAT0(0x1c) = 0x%08x\n", readl(spi_base + 0x1c));
    pr_info("  PAD  (0x24) = 0x%08x\n", readl(spi_base + 0x24));
    pr_info("  CFG2 (0x28) = 0x%08x\n", readl(spi_base + 0x28));
    pr_info("  IPM  (0x40) = 0x%08x\n", readl(spi_base + 0x40));

    iounmap(spi_base);
    pm_runtime_put(dev);
    put_device(dev);
    if (gpio_base) iounmap(gpio_base);
    return 0;
}

static void __exit diag_spi_exit(void)
{
    pr_info("=== [DIAG_SPI] Module unloaded ===\n");
}

module_init(diag_spi_init);
module_exit(diag_spi_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SPI2 and Novatek Hardware Diagnostics");
