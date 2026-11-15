#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>

static int __init test_gpio_init(void)
{
    void __iomem *base = ioremap(0x10005000, 0x1000);
    if (!base) {
        pr_err([TEST_GPIO]
