#!/usr/bin/env python3
"""
deploy_apk.py - 跨平台 APK 注入与签名自动化脚本
将编译好的 .so 文件注入到预编译壳 APK 中，执行 zipalign + apksigner。

Usage:
    python deploy_apk.py                                    # 默认参数
    python deploy_apk.py --abi arm64-v8a x86 x86_64        # 多 ABI
    python deploy_apk.py --so-dir ./build_output/android    # 自定义产物目录
    python deploy_apk.py --install                          # 注入后自动 adb install
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

# ── 路径常量 ──
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.normpath(os.path.join(SCRIPT_DIR, "..", "..", ".."))

# APK 工具链目录
APK_TOOLS_DIR = os.path.normpath(os.path.join(PROJECT_ROOT, "GGELUA", "build", "android"))
DEFAULT_APK = os.path.join(APK_TOOLS_DIR, "GGELUA.apk")
DEFAULT_KEYSTORE = os.path.join(APK_TOOLS_DIR, "debug.keystore")
# 备用 keystore (Gradle 项目里的)
BACKUP_KEYSTORE = os.path.join(SCRIPT_DIR, "app", "GGELUA", "debug.keystore")
DEFAULT_SO_DIR = os.path.join(SCRIPT_DIR, "build_output", "android")
DEFAULT_ABIS = ["arm64-v8a", "armeabi-v7a", "x86", "x86_64"]

# 签名参数
KS_PASS = "android"
KEY_ALIAS = "androiddebugkey"
KEY_PASS = "android"


def find_tool(name: str, search_dirs: list[str]) -> str | None:
    """在指定目录列表中查找工具"""
    for d in search_dirs:
        candidate = os.path.join(d, name)
        if os.path.isfile(candidate):
            return candidate
    # 尝试 PATH
    result = shutil.which(name)
    return result


def find_java() -> str | None:
    """查找 Java 可执行文件"""
    # 优先使用内嵌 OpenJDK
    embedded = os.path.join(APK_TOOLS_DIR, "OpenJDK", "bin", "java.exe")
    if os.path.isfile(embedded):
        return embedded
    # Windows 上也试试不带 .exe
    embedded2 = os.path.join(APK_TOOLS_DIR, "OpenJDK", "bin", "java")
    if os.path.isfile(embedded2):
        return embedded2
    # JAVA_HOME
    java_home = os.environ.get("JAVA_HOME")
    if java_home:
        for name in ("java.exe", "java"):
            candidate = os.path.join(java_home, "bin", name)
            if os.path.isfile(candidate):
                return candidate
    # PATH
    return shutil.which("java") or shutil.which("java.exe")


def find_zipalign() -> str | None:
    """查找 zipalign"""
    # 内嵌工具
    embedded = os.path.join(APK_TOOLS_DIR, "zipalign.exe")
    if os.path.isfile(embedded):
        return embedded
    # Android SDK build-tools
    android_home = os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT")
    if android_home:
        bt_dir = os.path.join(android_home, "build-tools")
        if os.path.isdir(bt_dir):
            # 取最新版本
            versions = sorted(os.listdir(bt_dir), reverse=True)
            for v in versions:
                for name in ("zipalign.exe", "zipalign"):
                    candidate = os.path.join(bt_dir, v, name)
                    if os.path.isfile(candidate):
                        return candidate
    return shutil.which("zipalign") or shutil.which("zipalign.exe")


def find_apksigner_jar() -> str | None:
    """查找 apksigner.jar (优先 SDK build-tools 版本，兼容 JDK 17)"""
    # 1. SDK build-tools (newer, JDK 17 compatible)
    android_home = os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT")
    if android_home:
        bt_dir = os.path.join(android_home, "build-tools")
        if os.path.isdir(bt_dir):
            versions = sorted(os.listdir(bt_dir), reverse=True)
            for v in versions:
                candidate = os.path.join(bt_dir, v, "lib", "apksigner.jar")
                if os.path.isfile(candidate):
                    return candidate
    # 2. Fallback: embedded (may not work with JDK 17)
    embedded = os.path.join(APK_TOOLS_DIR, "apksigner.jar")
    if os.path.isfile(embedded):
        return embedded
    return None


def inject_so_files(apk_path: str, so_dir: str, abis: list[str], output_apk: str) -> dict[str, list[str]]:
    """
    将 .so 文件注入到 APK 中对应的 lib/<abi>/ 路径。
    使用 zipfile 模块直接操作，不依赖 apktool。

    返回 {abi: [injected_so_names]} 的映射。
    """
    injected = {}

    # 复制原始 APK 作为基础
    shutil.copy2(apk_path, output_apk)

    # 收集所有需要注入的 .so 文件
    entries_to_add = []  # (zip_entry_path, local_file_path)
    for abi in abis:
        abi_dir = os.path.join(so_dir, abi)
        if not os.path.isdir(abi_dir):
            print(f"  [!]  ABI 目录不存在，跳过: {abi_dir}")
            continue

        so_files = [f for f in os.listdir(abi_dir) if f.endswith(".so")]
        if not so_files:
            print(f"  [!]  ABI 目录为空，跳过: {abi_dir}")
            continue

        injected[abi] = []
        for so_name in so_files:
            zip_path = f"lib/{abi}/{so_name}"
            local_path = os.path.join(abi_dir, so_name)
            entries_to_add.append((zip_path, local_path))
            injected[abi].append(so_name)

    if not entries_to_add:
        print("[FAIL] 没有找到任何 .so 文件可供注入！")
        sys.exit(1)

    # 使用 zipfile 注入 — 需要重建 ZIP 以替换同名条目
    # 策略：创建临时 ZIP，逐条复制（跳过被替换的），再追加新条目
    temp_fd, temp_path = tempfile.mkstemp(suffix=".apk", dir=os.path.dirname(output_apk))
    os.close(temp_fd)

    # 构建需要替换的 zip 路径集合
    replace_set = {entry[0] for entry in entries_to_add}

    try:
        with zipfile.ZipFile(output_apk, 'r') as zin, \
             zipfile.ZipFile(temp_path, 'w', zipfile.ZIP_DEFLATED) as zout:

            # 复制原有条目（跳过将被替换的）
            for item in zin.infolist():
                if item.filename in replace_set:
                    continue
                # .so 文件在 APK 中通常使用 STORED（不压缩），保持原有压缩方式
                data = zin.read(item.filename)
                zout.writestr(item, data)

            # 追加新的 .so 文件 (使用 STORED 方式，Android 要求 .so 不压缩以支持直接 mmap)
            for zip_path, local_path in entries_to_add:
                info = zipfile.ZipInfo(zip_path)
                info.compress_type = zipfile.ZIP_STORED
                with open(local_path, 'rb') as f:
                    zout.writestr(info, f.read())

        # 替换（shutil.move 支持跨驱动器）
        if os.path.exists(output_apk):
            os.remove(output_apk)
        shutil.move(temp_path, output_apk)

    except Exception:
        # 清理临时文件
        if os.path.exists(temp_path):
            os.remove(temp_path)
        raise

    return injected


def run_zipalign(zipalign_exe: str, input_apk: str, output_apk: str):
    """执行 zipalign 4 字节对齐"""
    cmd = [zipalign_exe, "-p", "-f", "4", input_apk, output_apk]
    print(f"  命令: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"[FAIL] zipalign 失败:\n{result.stderr}")
        sys.exit(1)


def run_apksigner(java_exe: str, apksigner_jar: str, keystore: str,
                  input_apk: str, output_apk: str):
    """执行 apksigner 签名"""
    # JDK 17+ 需要 --add-opens 参数解除模块封装限制
    jvm_args = [
        "--add-opens", "java.base/java.io=ALL-UNNAMED",
        "--add-opens", "java.base/sun.security.x509=ALL-UNNAMED",
        "--add-opens", "java.base/sun.security.pkcs=ALL-UNNAMED",
    ]
    cmd = [
        java_exe, *jvm_args, "-jar", apksigner_jar, "sign",
        "--ks", keystore,
        "--ks-pass", f"pass:{KS_PASS}",
        "--ks-key-alias", KEY_ALIAS,
        "--key-pass", f"pass:{KEY_PASS}",
        "--out", output_apk,
        input_apk,
    ]
    print(f"  命令: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"[FAIL] apksigner 签名失败:\n{result.stderr}")
        sys.exit(1)


def verify_apk(java_exe: str, apksigner_jar: str, apk_path: str) -> bool:
    """验证 APK 签名"""
    jvm_args = [
        "--add-opens", "java.base/java.io=ALL-UNNAMED",
        "--add-opens", "java.base/sun.security.x509=ALL-UNNAMED",
        "--add-opens", "java.base/sun.security.pkcs=ALL-UNNAMED",
    ]
    cmd = [java_exe, *jvm_args, "-jar", apksigner_jar, "verify", "-v", apk_path]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode == 0:
        print(f"  [OK] 签名验证通过")
        return True
    else:
        print(f"  [FAIL] 签名验证失败:\n{result.stderr}")
        return False


def adb_install(apk_path: str):
    """通过 adb 安装 APK"""
    adb = shutil.which("adb") or shutil.which("adb.exe")
    if not adb:
        print("[!]  adb 未找到，跳过自动安装")
        return
    cmd = [adb, "install", "-r", apk_path]
    print(f"  命令: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode == 0:
        print(f"  [OK] 安装成功")
    else:
        print(f"  [FAIL] 安装失败:\n{result.stderr}")


def main():
    parser = argparse.ArgumentParser(description="APK 注入与签名自动化工具")
    parser.add_argument("--so-dir", default=DEFAULT_SO_DIR,
                        help=f"编译产物目录 (默认: {DEFAULT_SO_DIR})")
    parser.add_argument("--apk", default=DEFAULT_APK,
                        help=f"壳 APK 路径 (默认: {DEFAULT_APK})")
    parser.add_argument("--abi", nargs="+", default=DEFAULT_ABIS,
                        help=f"目标 ABI 列表 (默认: {' '.join(DEFAULT_ABIS)})")
    parser.add_argument("--output", default=None,
                        help="输出 APK 路径 (默认: 壳 APK 同级 GGELUA_patched.apk)")
    parser.add_argument("--keystore", default=None,
                        help=f"签名密钥库 (默认: {DEFAULT_KEYSTORE})")
    parser.add_argument("--install", action="store_true",
                        help="签名完成后自动 adb install")
    args = parser.parse_args()

    # ── 验证输入 ──
    if not os.path.isfile(args.apk):
        print(f"[FAIL] 壳 APK 不存在: {args.apk}")
        sys.exit(1)

    if not os.path.isdir(args.so_dir):
        print(f"[FAIL] 产物目录不存在: {args.so_dir}")
        sys.exit(1)

    # 查找签名密钥库
    keystore = args.keystore or DEFAULT_KEYSTORE
    if not os.path.isfile(keystore):
        keystore = BACKUP_KEYSTORE
    if not os.path.isfile(keystore):
        print(f"[FAIL] 签名密钥库不存在: {keystore}")
        sys.exit(1)

    # 输出路径
    output_dir = os.path.dirname(args.apk)
    final_apk = args.output or os.path.join(output_dir, "GGELUA_patched.apk")

    # ── 查找工具 ──
    java_exe = find_java()
    zipalign_exe = find_zipalign()
    apksigner_jar = find_apksigner_jar()

    missing = []
    if not java_exe:
        missing.append("java (检查 OpenJDK/ 或 JAVA_HOME)")
    if not zipalign_exe:
        missing.append("zipalign (检查 ANDROID_HOME/build-tools/ 或内嵌工具)")
    if not apksigner_jar:
        missing.append("apksigner.jar (检查内嵌工具目录)")

    if missing:
        print("[FAIL] 缺少必要工具:")
        for m in missing:
            print(f"   - {m}")
        sys.exit(1)

    print("=" * 60)
    print("  APK 注入与签名自动化工具")
    print(f"  壳 APK:    {args.apk}")
    print(f"  产物目录:  {args.so_dir}")
    print(f"  目标 ABI:  {', '.join(args.abi)}")
    print(f"  输出 APK:  {final_apk}")
    print(f"  Java:      {java_exe}")
    print(f"  zipalign:  {zipalign_exe}")
    print(f"  apksigner: {apksigner_jar}")
    print(f"  keystore:  {keystore}")
    print("=" * 60)

    # ── Stage 1: 注入 .so ──
    print("\n[1/4] 注入 .so 文件到 APK...")
    injected_apk = os.path.join(output_dir, "GGELUA_injected.apk")
    injected = inject_so_files(args.apk, args.so_dir, args.abi, injected_apk)

    for abi, so_list in injected.items():
        print(f"  {abi}: {len(so_list)} 个 .so 文件")
        for name in sorted(so_list):
            size = os.path.getsize(os.path.join(args.so_dir, abi, name))
            print(f"    - {name} ({size / 1024:.1f} KB)")

    # ── Stage 2: zipalign ──
    print("\n[2/4] zipalign 4 字节对齐...")
    aligned_apk = os.path.join(output_dir, "GGELUA_aligned.apk")
    run_zipalign(zipalign_exe, injected_apk, aligned_apk)
    print("  [OK] 对齐完成")

    # ── Stage 3: apksigner ──
    print("\n[3/4] apksigner 签名...")
    run_apksigner(java_exe, apksigner_jar, keystore, aligned_apk, final_apk)
    print("  [OK] 签名完成")

    # ── Stage 4: 验证 ──
    print("\n[4/4] 验证签名...")
    verified = verify_apk(java_exe, apksigner_jar, final_apk)

    # 清理中间文件
    for tmp_file in (injected_apk, aligned_apk):
        if os.path.isfile(tmp_file):
            os.remove(tmp_file)

    apk_size = os.path.getsize(final_apk) if os.path.isfile(final_apk) else 0

    print(f"\n{'=' * 60}")
    if verified:
        print(f"[OK] 部署完成! 最终 APK: {final_apk} ({apk_size / 1024 / 1024:.1f} MB)")
        print(f"   可执行: adb install -r \"{final_apk}\"")
    else:
        print(f"[FAIL] 签名验证失败，APK 可能无法安装")
        sys.exit(1)

    # 自动安装
    if args.install:
        print("\n[bonus] adb install...")
        adb_install(final_apk)

    print("=" * 60)


if __name__ == "__main__":
    main()
