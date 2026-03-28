#!/usr/bin/env python3
"""
clean_android_build.py - 跨平台 Android 构建缓存清理脚本
安全删除 MSBuild / Gradle / apktool 产生的中间文件，绝不触碰源码和 assets。

Usage:
    python clean_android_build.py            # 执行清理
    python clean_android_build.py --dry-run  # 仅列出将删除的内容，不实际删除
"""

import argparse
import os
import shutil
import sys

# ── 脚本所在目录 = GGELUA3/scripts/ ──
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
# 引擎根目录 = GGELUA3/
ENGINE_ROOT = os.path.normpath(os.path.join(SCRIPT_DIR, ".."))
# Android VS 项目目录 (编译缓存所在)
ANDROID_VS_DIR = os.path.normpath(os.path.join(ENGINE_ROOT, "Projects", "android-vs"))
# 总项目根目录 (GGELUA/build/ 等)
PROJECT_ROOT = os.path.normpath(os.path.join(ENGINE_ROOT, ".."))

# ── 清理目标列表 (相对于各自的基准目录) ──
# 格式: (基准目录, 相对路径, 描述)
CLEAN_TARGETS = [
    # 统一编译产出 (Directory.Build.targets 控制)
    (ANDROID_VS_DIR, ".out",                        "统一编译输出 (.out/{abi}/*.so)"),
    (ANDROID_VS_DIR, ".obj",                        "统一中间文件 (.o)"),
    # 旧版散落目录 (兼容清理)
    (ANDROID_VS_DIR, ".ARM",                        "旧版 ARM 编译缓存"),
    (ANDROID_VS_DIR, ".ARM64",                      "旧版 ARM64 编译缓存"),
    (ANDROID_VS_DIR, ".x86",                        "旧版 x86 编译缓存"),
    (ANDROID_VS_DIR, ".x64",                        "旧版 x64 编译缓存"),
    (ANDROID_VS_DIR, "build_output",                "旧版产物暂存区"),
    # MSBuild 子项目中间产物 (旧版兼容)
    (ANDROID_VS_DIR, "app/GGELUA/.ARM64",           "MSBuild ARM64 中间产物"),
    (ANDROID_VS_DIR, "app/GGELUA/.ARM",             "MSBuild ARM 中间产物"),
    (ANDROID_VS_DIR, "app/GGELUA/.x86",             "MSBuild x86 中间产物"),
    (ANDROID_VS_DIR, "app/GGELUA/.x64",             "MSBuild x64 中间产物"),
    # Gradle 缓存
    (ANDROID_VS_DIR, "app/GGELUA/.gradle",          "Gradle 守护进程缓存"),
    (ANDROID_VS_DIR, "app/GGELUA/build",            "Gradle 根模块缓存"),
    (ANDROID_VS_DIR, "app/GGELUA/app/build",        "Gradle app 模块缓存"),
    # 旧版输出
    (ANDROID_VS_DIR, "output",                      "旧版产物输出目录"),
    # apktool 临时目录
    (PROJECT_ROOT, "GGELUA/build/android/%TEMP%", "apktool 临时目录"),
]

# ── 白名单 (绝对不允许删除的路径关键词) ──
WHITELIST_KEYWORDS = [
    "Sources", "Dependencies", "assets", ".sln", ".vcxproj", ".props",
    ".targets", "debug.keystore", "GGELUA.apk", "src/main/java",
    "src/main/res", "src/main/AndroidManifest", ".py", ".ps1", ".bat",
    ".lua", ".h", ".c", ".cpp", "OpenJDK",
]


def is_whitelisted(path: str) -> bool:
    """检查路径是否命中白名单（防止误删）"""
    normalized = os.path.normpath(path)
    for kw in WHITELIST_KEYWORDS:
        if kw in os.path.basename(normalized):
            return True
    return False


def get_dir_size(path: str) -> int:
    """递归计算目录大小 (bytes)"""
    total = 0
    try:
        for dirpath, _, filenames in os.walk(path):
            for f in filenames:
                fp = os.path.join(dirpath, f)
                try:
                    total += os.path.getsize(fp)
                except OSError:
                    pass
    except OSError:
        pass
    return total


def format_size(size_bytes: int) -> str:
    """格式化字节数为人类可读"""
    for unit in ("B", "KB", "MB", "GB"):
        if abs(size_bytes) < 1024:
            return f"{size_bytes:.1f} {unit}"
        size_bytes /= 1024
    return f"{size_bytes:.1f} TB"


def clean_jnilibs(dry_run: bool) -> int:
    """清理 jniLibs 下的 .so 文件（但保留目录结构）"""
    jnilibs_base = os.path.join(ANDROID_VS_DIR, "app", "GGELUA", "app", "src", "main", "jniLibs")
    freed = 0
    if not os.path.isdir(jnilibs_base):
        return freed

    for abi_dir in os.listdir(jnilibs_base):
        abi_path = os.path.join(jnilibs_base, abi_dir)
        if not os.path.isdir(abi_path):
            continue
        for f in os.listdir(abi_path):
            if f.endswith(".so"):
                fp = os.path.join(abi_path, f)
                size = os.path.getsize(fp)
                freed += size
                if dry_run:
                    print(f"  [DRY-RUN] 将删除: {fp} ({format_size(size)})")
                else:
                    os.remove(fp)
                    print(f"  已删除: {fp} ({format_size(size)})")
    return freed


def main():
    parser = argparse.ArgumentParser(description="Android 构建缓存清理工具")
    parser.add_argument("--dry-run", action="store_true",
                        help="仅列出将删除的内容，不实际执行删除")
    args = parser.parse_args()

    print("=" * 60)
    print("  Android 构建缓存清理工具")
    print(f"  模式: {'DRY-RUN (预览)' if args.dry_run else '实际清理'}")
    print(f"  脚本目录: {SCRIPT_DIR}")
    print("=" * 60)

    total_freed = 0
    cleaned_count = 0

    # 清理目录列表
    for base, rel_path, desc in CLEAN_TARGETS:
        full_path = os.path.normpath(os.path.join(base, rel_path))

        if not os.path.exists(full_path):
            continue

        if is_whitelisted(full_path):
            print(f"\n[!]  跳过白名单: {full_path}")
            continue

        size = get_dir_size(full_path) if os.path.isdir(full_path) else os.path.getsize(full_path)
        total_freed += size
        cleaned_count += 1

        if args.dry_run:
            print(f"\n[DRY-RUN] 将删除: {full_path}")
            print(f"  说明: {desc} | 大小: {format_size(size)}")
        else:
            print(f"\n[x]  删除: {full_path}")
            print(f"  说明: {desc} | 大小: {format_size(size)}")
            if os.path.isdir(full_path):
                shutil.rmtree(full_path, ignore_errors=True)
            else:
                os.remove(full_path)

    # 清理 jniLibs 中的 .so
    print(f"\n{'─' * 40}")
    print("清理 jniLibs .so 文件...")
    jni_freed = clean_jnilibs(args.dry_run)
    total_freed += jni_freed

    # 汇总
    print(f"\n{'=' * 60}")
    action = "将释放" if args.dry_run else "已释放"
    print(f"[OK] 清理完成 | {cleaned_count} 个目录 | {action} {format_size(total_freed)}")
    if args.dry_run:
        print("提示: 去掉 --dry-run 参数以执行实际清理")
    print("=" * 60)


if __name__ == "__main__":
    main()
