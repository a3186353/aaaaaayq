#!/usr/bin/env python3
"""
clean_apk_template.py - 清理 APK 模板中的无关 .so 库
仅保留游戏运行必需的 10 个核心 .so，移除所有其他原生库。

Usage:
    python clean_apk_template.py                    # 清理并覆盖原模板
    python clean_apk_template.py --dry-run          # 仅预览，不修改
    python clean_apk_template.py --output clean.apk # 输出到指定文件
"""

import argparse
import os
import shutil
import sys
import tempfile
import zipfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ENGINE_ROOT = os.path.normpath(os.path.join(SCRIPT_DIR, ".."))
PROJECT_ROOT = os.path.normpath(os.path.join(ENGINE_ROOT, ".."))
DEFAULT_APK = os.path.normpath(os.path.join(PROJECT_ROOT, "GGELUA", "build", "android", "GGELUA.apk"))

# 核心 .so 白名单 — 仅保留这些库（不含 lib 前缀和 .so 后缀）
CORE_LIBS = {
    "main",        # SDL 入口
    "ggelua",      # 引擎核心 (含 ghv 静态链接)
    "SDL2",        # SDL2
    "SDL_image",   # 图片
    "SDL_ttf",     # 字体
    "SDL_mixer",   # 音频
    "gsdl2",       # SDL Lua 绑定
    "mygxy",       # 安全/加密
    "gastar",      # A*寻路
    "lsqlite3",    # SQLite
    "lua54",       # Lua VM
    "cjson",       # JSON (Lua C 模块)
}


def get_so_basename(filename: str) -> str:
    """从 lib/arm64-v8a/libfoo.so 提取 foo"""
    base = os.path.basename(filename)
    if base.startswith("lib") and base.endswith(".so"):
        return base[3:-3]
    return base


def is_core_so(zip_path: str) -> bool:
    """判断 ZIP 条目是否为核心 .so"""
    if not zip_path.startswith("lib/") or not zip_path.endswith(".so"):
        return True  # 非 .so 文件，保留
    return get_so_basename(zip_path) in CORE_LIBS


def clean_apk(apk_path: str, output_path: str, dry_run: bool = False):
    """清理 APK 中非核心 .so 文件"""
    if not os.path.isfile(apk_path):
        print(f"[FAIL] APK 不存在: {apk_path}")
        sys.exit(1)

    kept = []
    removed = []
    saved_bytes = 0

    with zipfile.ZipFile(apk_path, 'r') as zin:
        for item in zin.infolist():
            if item.filename.startswith("lib/") and item.filename.endswith(".so"):
                if is_core_so(item.filename):
                    kept.append(item.filename)
                else:
                    removed.append(item.filename)
                    saved_bytes += item.file_size
            else:
                kept.append(item.filename)

    # 报告
    print(f"\n{'=' * 60}")
    print(f"  APK 模板清理工具")
    print(f"  输入: {apk_path}")
    print(f"  输出: {output_path}")
    print(f"{'=' * 60}")

    if removed:
        print(f"\n[移除] {len(removed)} 个无关 .so (节省 {saved_bytes / 1024 / 1024:.1f} MB):")
        for f in sorted(removed):
            print(f"  [-] {f}")

    # 按 ABI 汇总保留的 .so
    abi_libs = {}
    for f in kept:
        if f.startswith("lib/") and f.endswith(".so"):
            parts = f.split("/")
            if len(parts) == 3:
                abi = parts[1]
                abi_libs.setdefault(abi, []).append(parts[2])

    print(f"\n[保留] 各 ABI 核心 .so 数量:")
    for abi in sorted(abi_libs.keys()):
        libs = sorted(abi_libs[abi])
        print(f"  {abi}: {len(libs)} 个")
        for lib in libs:
            print(f"    [+] {lib}")

    # 检查各 ABI 是否完整
    for abi in sorted(abi_libs.keys()):
        present = {get_so_basename(f"lib/{abi}/{lib}") for lib in abi_libs[abi]}
        missing = CORE_LIBS - present
        if missing:
            print(f"\n  [!] {abi} 缺失: {', '.join(sorted(missing))}")

    if dry_run:
        print(f"\n[DRY RUN] 未修改任何文件")
        return

    # 执行清理
    temp_fd, temp_path = tempfile.mkstemp(suffix=".apk", dir=os.path.dirname(output_path) or ".")
    os.close(temp_fd)

    try:
        with zipfile.ZipFile(apk_path, 'r') as zin, \
             zipfile.ZipFile(temp_path, 'w') as zout:
            for item in zin.infolist():
                if item.filename in removed:
                    continue
                data = zin.read(item.filename)
                # 保持原有压缩方式
                out_info = item
                zout.writestr(out_info, data)

        # 移到目标位置
        if os.path.exists(output_path):
            os.remove(output_path)
        shutil.move(temp_path, output_path)
    except Exception:
        if os.path.exists(temp_path):
            os.remove(temp_path)
        raise

    orig_size = os.path.getsize(apk_path)
    new_size = os.path.getsize(output_path)
    print(f"\n[OK] 清理完成!")
    print(f"  原始大小: {orig_size / 1024 / 1024:.1f} MB")
    print(f"  清理后:   {new_size / 1024 / 1024:.1f} MB")
    print(f"  节省:     {(orig_size - new_size) / 1024 / 1024:.1f} MB ({(orig_size - new_size) / orig_size * 100:.1f}%)")


def main():
    parser = argparse.ArgumentParser(description="APK 模板 .so 清理工具")
    parser.add_argument("--apk", default=DEFAULT_APK, help=f"壳 APK 路径 (默认: {DEFAULT_APK})")
    parser.add_argument("--output", default=None, help="输出 APK 路径 (默认: 覆盖原文件)")
    parser.add_argument("--dry-run", action="store_true", help="仅预览，不修改文件")
    args = parser.parse_args()

    output = args.output or args.apk
    clean_apk(args.apk, output, args.dry_run)


if __name__ == "__main__":
    main()
