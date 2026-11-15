# tb375fc-linux-utils

辅助工具集，用于 **联想小新 Pad Pro 12.7（TB375FC，联发科 MT6897 / Dimensity 8300）主线 Linux 移植** 的调试与启动。

配套内核仓库为独立维护的 **TB375FC 主线内核树**（基于 Linux 7.2）：
分支 `power-fix-tb375fc` / `touch-fix-tb375fc` / `wifi-bt-fix-tb375fc`，设备树
`arch/arm64/boot/dts/mediatek/mt6897-lenovo-tb375fc.dts`。

> ⚠️ 仅供学习研究。刷写第三方固件有变砖风险，会失去保修，后果自负。

## 目录结构

### `kernel-modules/` — 树外诊断内核模块

bring-up 期间用来从内核侧直接探测硬件状态的小模块，均在设备上 `insmod` 后从 dmesg 读结果：

| 模块 | 用途 |
|---|---|
| `diag_spi` | 探测 SPI 控制器寄存器与 GPIO 复用状态 |
| `spi_probe` | SPI 总线探测 |
| `peri_gate_poke` | 外设时钟门控（peri gate）寄存器读写 |
| `regdump` | 通用物理地址寄存器转储 |
| `test_pm` | 电源管理 / runtime PM 行为验证 |
| `test_gpio` | GPIO 读写测试 |
| `conn_probe` | MTK conninfra（WiFi/BT 共用子系统）状态探测 |
| `conn_watch` | conninfra 状态轮询监视 |
| `emi_peek` | EMI（内存控制器）寄存器读取 |

### `initramfs/` — 最小 initramfs

自带静态 `init`，完成根文件系统挂载前的准备工作（含从 `nvdata` 分区拷贝 WiFi 校准数据）。
改写自 xaga（Redmi Note 11T Pro）主线项目的 initramfs，分区号已适配 TB375FC。

```sh
make clean
make   # 产物: initramfs.cpio.lz4，随内核以 CONFIG_INITRAMFS_SOURCE 或 boot.img ramdisk 使用
```

### `tools/` — 构建辅助

- `build_module.sh`：交叉编译上述树外模块的通用入口（见文件内注释）。

## 编译内核模块

需要一个已编译过的内核源码树（`KDIR`）和 aarch64 交叉工具链：

```sh
export KDIR=~/mt6897/linux
export CROSS_COMPILE=aarch64-buildroot-linux-gnu-
./tools/build_module.sh kernel-modules/spi_probe
```

## License

GPL-2.0（内核模块与 initramfs 均为内核衍生 / 内核接口使用场景，统一采用 GPL-2.0，见 `LICENSE`）。
