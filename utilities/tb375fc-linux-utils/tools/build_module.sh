#!/bin/bash
# Cross-build an out-of-tree kernel module against a prepared kernel tree.
#
# Usage:
#   export KDIR=~/mt6897/linux          # built kernel source tree (required)
#   export CROSS_COMPILE=aarch64-buildroot-linux-gnu-   # toolchain prefix
#   ./build_module.sh kernel-modules/spi_probe
#
set -e

KDIR="${KDIR:?set KDIR to a built kernel source tree}"
CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}"
M="${1:?usage: build_module.sh <module dir>}"
JOBS="${JOBS:-$(nproc)}"

# Optional: out-of-tree kernel build output (O=...). If the kernel was built
# with LLVM (Android clang), export LLVM=1 as well.
EXTRA_ARGS=()
[ -n "$KBUILD_OUTPUT" ] && EXTRA_ARGS+=("O=$KBUILD_OUTPUT")
[ -n "$LLVM" ] && EXTRA_ARGS+=("LLVM=$LLVM")

make -C "$KDIR" "${EXTRA_ARGS[@]}" M="$M" ARCH=arm64 CROSS_COMPILE="$CROSS_COMPILE" -j"$JOBS" modules
