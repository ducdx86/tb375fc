#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>

static int __init test_pinmux_init(void)
{
    void __iomem *base = ioremap(0x10005000, 0x1000);
    if (!base) {
        pr_err("PINMUX ioremap failed!\n");
        return -ENOMEM;
    }

    pr_info("[PINMUX] MODE0 (0-7)   = 0x%08x\n", readl(base + 0x0300));
    pr_info("[PINMUX] MODE1 (8-15)  = 0x%08x\n", readl(base + 0x0310));
    pr_info("[PINMUX] MODE7 (56-63) = 0x%08x\n", readl(base + 0x0370));
    pr_info("[PINMUX] DIR0 (0-31)   = 0x%08x\n", readl(base + 0x0000));
    pr_info("[PINMUX] DIR1 (32-63)  = 0x%08x\n", readl(base + 0x0010));
    pr_info("[PINMUX] DOUT0 (0-31)  = 0x%08x\n", readl(base + 0x0100));
    pr_info("[PINMUX] DOUT1 (32-63) = 0x%08x\n", readl(base + 0x0110));
    pr_info("[PINMUX] DIN0 (0-31)   = 0x%08x\n", readl(base + 0x0200));
    pr_info("[PINMUX] DIN1 (32-63)  = 0x%08x\n", readl(base + 0x0210));

    iounmap(base);
    return 0;
}

static void __exit test_pinmux_exit(void) {}

module_init(test_pinmux_init);
module_exit(test_pinmux_exit);
MODULE_LICENSE("GPL");
