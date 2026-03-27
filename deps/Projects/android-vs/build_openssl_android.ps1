<#
.SYNOPSIS
    交叉编译 OpenSSL 3.x 静态库 for Android NDK
.DESCRIPTION
    为 ARM64, ARM, x86, x86_64 四架构编译 libcrypto.a 静态库。
    编译产物放置到 Dependencies/openssl/android/{Platform}/lib/ 目录。
    头文件复用 Dependencies/openssl/include/（平台无关）。
.PARAMETER NdkRoot
    Android NDK 根路径。默认使用 VS_NdkRoot 环境变量或常见安装路径。
.PARAMETER OpenSSLSource
    OpenSSL 源码路径。需要用户提前下载解压 openssl-3.x.x 源码。
.PARAMETER Architectures
    要编译的架构列表。默认为 ARM64, ARM, x86, x86_64。
.PARAMETER ApiLevel
    Android API Level。默认为 24。
.EXAMPLE
    .\build_openssl_android.ps1 -OpenSSLSource "C:\openssl-3.4.1" -NdkRoot "C:\Android\ndk\27.2.12479018"
#>
param(
    [string]$NdkRoot,
    [string]$OpenSSLSource,
    [string[]]$Architectures = @("ARM64", "ARM", "x86", "x86_64"),
    [int]$ApiLevel = 24
)

$ErrorActionPreference = "Stop"

# --- NDK 路径自动检测 ---
if (-not $NdkRoot) {
    # 尝试 VS 环境变量
    $NdkRoot = $env:VS_NdkRoot
    if (-not $NdkRoot) {
        # 尝试常见路径
        $candidates = @(
            "$env:LOCALAPPDATA\Android\Sdk\ndk\*",
            "$env:ANDROID_NDK_ROOT",
            "$env:ANDROID_NDK_HOME"
        )
        foreach ($c in $candidates) {
            $found = Get-Item $c -ErrorAction SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1
            if ($found) { $NdkRoot = $found.FullName; break }
        }
    }
}

if (-not $NdkRoot -or -not (Test-Path $NdkRoot)) {
    Write-Error "NDK 路径未找到。请通过 -NdkRoot 参数指定或设置 ANDROID_NDK_ROOT 环境变量。"
    exit 1
}

if (-not $OpenSSLSource -or -not (Test-Path "$OpenSSLSource\Configure")) {
    Write-Error "OpenSSL 源码路径无效。请通过 -OpenSSLSource 参数指定（需包含 Configure 文件）。`n下载: https://github.com/openssl/openssl/releases"
    exit 1
}

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  OpenSSL Android 交叉编译" -ForegroundColor Cyan
Write-Host "  NDK: $NdkRoot" -ForegroundColor Cyan
Write-Host "  OpenSSL: $OpenSSLSource" -ForegroundColor Cyan
Write-Host "  API Level: $ApiLevel" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$OutputBase = Join-Path (Split-Path $ScriptDir -Parent) "..\Dependencies\openssl\android"

# 架构映射
$ArchMap = @{
    "ARM64"  = @{ Target = "android-arm64";  Triple = "aarch64-linux-android";  Platform = "ARM64" }
    "ARM"    = @{ Target = "android-arm";    Triple = "armv7a-linux-androideabi"; Platform = "ARM" }
    "x86"    = @{ Target = "android-x86";    Triple = "i686-linux-android";     Platform = "x86" }
    "x86_64" = @{ Target = "android-x86_64"; Triple = "x86_64-linux-android";   Platform = "x64" }
}

$Toolchain = Join-Path $NdkRoot "toolchains\llvm\prebuilt\windows-x86_64"

foreach ($arch in $Architectures) {
    if (-not $ArchMap.ContainsKey($arch)) {
        Write-Warning "未知架构: $arch, 跳过"
        continue
    }

    $cfg = $ArchMap[$arch]
    $outputDir = Join-Path $OutputBase "$($cfg.Platform)\lib"
    $buildDir = Join-Path $env:TEMP "openssl-android-build-$arch"

    Write-Host "`n>>> 编译 $arch ($($cfg.Target)) <<<" -ForegroundColor Yellow

    # 清理和准备构建目录
    if (Test-Path $buildDir) { Remove-Item $buildDir -Recurse -Force }
    New-Item -ItemType Directory -Path $buildDir -Force | Out-Null

    # 设置环境变量
    $env:ANDROID_NDK_ROOT = $NdkRoot
    $env:PATH = "$Toolchain\bin;$env:PATH"

    Push-Location $OpenSSLSource
    try {
        # Configure: 只编译 libcrypto（不需要 libssl）
        # no-shared: 静态库
        # no-tests: 跳过测试
        # no-ui-console: 无控制台 UI（交叉编译无意义）
        $configArgs = @(
            $cfg.Target
            "-D__ANDROID_API__=$ApiLevel"
            "--prefix=$buildDir\install"
            "--openssldir=$buildDir\ssl"
            "no-shared"
            "no-tests"
            "no-ui-console"
            "no-engine"
            "no-comp"
            "-fPIC"
        )

        Write-Host "  Configure: perl Configure $($configArgs -join ' ')"
        & perl Configure @configArgs
        if ($LASTEXITCODE -ne 0) { throw "Configure 失败 for $arch" }

        # Build (只编译 libcrypto)
        Write-Host "  Building libcrypto..."
        & make -j([Environment]::ProcessorCount) build_libs
        if ($LASTEXITCODE -ne 0) { throw "make 失败 for $arch" }

        # 复制产物
        New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
        Copy-Item "libcrypto.a" $outputDir -Force
        Write-Host "  输出: $outputDir\libcrypto.a" -ForegroundColor Green

        # 清理
        & make clean 2>$null
    }
    finally {
        Pop-Location
    }
}

Write-Host "`n========================================" -ForegroundColor Green
Write-Host "  编译完成！" -ForegroundColor Green
Write-Host "  产出目录: $OutputBase" -ForegroundColor Green
Write-Host ""
Write-Host "  预期目录结构:" -ForegroundColor White
Write-Host "    Dependencies/openssl/android/" -ForegroundColor White
Write-Host "      ARM64/lib/libcrypto.a" -ForegroundColor White
Write-Host "      ARM/lib/libcrypto.a" -ForegroundColor White
Write-Host "      x86/lib/libcrypto.a" -ForegroundColor White
Write-Host "      x64/lib/libcrypto.a" -ForegroundColor White
Write-Host "========================================" -ForegroundColor Green
