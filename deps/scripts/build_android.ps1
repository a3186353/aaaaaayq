# build_android.ps1 - Android .so 全平台编译 + 部署脚本
# Usage:
#   .\build_android.ps1                              # 编译全部 4 平台 Release
#   .\build_android.ps1 -Platform ARM64              # 只编译 ARM64
#   .\build_android.ps1 -Platform x86 -Deploy        # 编译 x86 并注入 APK
#   .\build_android.ps1 -Clean                       # 清理构建缓存
#
# Output: .out/{abi}/*.so  (与 jniLibs/ 格式一致)
#   .out/arm64-v8a/    ← 真机 64 位 (主流)
#   .out/armeabi-v7a/  ← 真机 32 位
#   .out/x86_64/       ← 模拟器 64 位
#   .out/x86/          ← 模拟器 32 位

param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [ValidateSet("All", "ARM", "ARM64", "x86", "x64")]
    [string]$Platform = "All",

    [switch]$Deploy,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$ScriptsDir = $PSScriptRoot
$EngineRoot = (Resolve-Path "$ScriptsDir\..").Path
$SolutionDir = Join-Path $EngineRoot "Projects\android-vs"

# ── Clean mode ──
if ($Clean) {
    $cleanScript = Join-Path $ScriptsDir "clean_android_build.py"
    if (Test-Path $cleanScript) {
        Write-Host "Invoking clean_android_build.py..." -ForegroundColor Yellow
        python $cleanScript
    } else {
        Write-Error "clean_android_build.py not found at $cleanScript"
    }
    exit 0
}

$SolutionFile = Join-Path $SolutionDir "Android.sln"

# VS Platform -> APK ABI 映射
$abiMap = @{
    'ARM64' = 'arm64-v8a'
    'ARM'   = 'armeabi-v7a'
    'x64'   = 'x86_64'
    'x86'   = 'x86'
}

# 确定要编译的平台列表
if ($Platform -eq "All") {
    $platforms = @("ARM64", "ARM", "x64", "x86")
} else {
    $platforms = @($Platform)
}

# Find MSBuild
$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vsWhere) {
    $msb = & $vsWhere -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
} else {
    $msb = "MSBuild.exe"
}
if (-not (Test-Path $msb)) {
    Write-Error "MSBuild.exe not found. Please install Visual Studio with Android C++ workload."
    exit 1
}

# Find llvm-strip (Release mode)
$stripExe = $null
if ($Configuration -eq "Release") {
    $ndkRoot = $null
    if ($env:ANDROID_NDK_HOME -and (Test-Path $env:ANDROID_NDK_HOME)) { $ndkRoot = $env:ANDROID_NDK_HOME }
    if (-not $ndkRoot) {
        $vsPath = & $vsWhere -latest -property installationPath 2>$null
        if ($vsPath) {
            $c = Get-ChildItem -Path "$vsPath\Common7\AndroidNDK" -Directory -EA SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1
            if ($c) { $ndkRoot = $c.FullName }
        }
    }
    if (-not $ndkRoot) {
        $p = "${env:ProgramFiles(x86)}\Android\AndroidNDK"
        if (Test-Path $p) { $c = Get-ChildItem $p -Directory -EA SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1; if ($c) { $ndkRoot = $c.FullName } }
    }
    if (-not $ndkRoot -and $env:ANDROID_HOME) {
        $p = Join-Path $env:ANDROID_HOME "ndk"
        if (Test-Path $p) { $c = Get-ChildItem $p -Directory -EA SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1; if ($c) { $ndkRoot = $c.FullName } }
    }
    if ($ndkRoot) {
        $stripExe = Get-ChildItem -Path $ndkRoot -Filter "llvm-strip.exe" -Recurse -EA SilentlyContinue | Select-Object -First 1
    }
}

$requiredSOs = @("libggelua.so", "libmain.so", "libgsdl2.so", "libmygxy.so", "libSDL2.so", "libSDL_image.so", "libSDL_ttf.so", "libSDL_mixer.so", "libgastar.so", "liblua54.so", "liblsqlite3.so", "libcjson.so")
$SolDirArg = "/p:SolutionDir=" + $SolutionDir + "\"
$totalErrors = 0

# ================================================================
#  主循环：逐平台编译 → 收集 → strip → 同步 jniLibs
# ================================================================
foreach ($plat in $platforms) {
    $abi = $abiMap[$plat]

    Write-Host "`n╔══════════════════════════════════════╗" -ForegroundColor Cyan
    Write-Host "║  Building: $plat ($abi)" -ForegroundColor Cyan
    Write-Host "║  Config:   $Configuration" -ForegroundColor Cyan
    Write-Host "╚══════════════════════════════════════╝" -ForegroundColor Cyan

    # ── 三阶段编译 ──
    Write-Host "  [1/3] Base libraries..." -ForegroundColor Yellow
    & $msb $SolutionFile /p:Configuration=$Configuration /p:Platform=$plat $SolDirArg /m /v:quiet /p:ContinueOnError=ErrorAndContinue 2>$null
    Write-Host "  [2/3] Mid-level..." -ForegroundColor Yellow
    & $msb $SolutionFile /p:Configuration=$Configuration /p:Platform=$plat $SolDirArg /m /v:quiet /p:ContinueOnError=ErrorAndContinue 2>$null
    Write-Host "  [3/3] Top-level..." -ForegroundColor Yellow
    & $msb $SolutionFile /p:Configuration=$Configuration /p:Platform=$plat $SolDirArg /m /v:quiet /p:ContinueOnError=ErrorAndContinue 2>$null

    # ── 收集 .so 到 .out/{abi}/ ──
    $outDir = Join-Path $SolutionDir ".out\$abi"
    if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir -Force | Out-Null }

    $searchPaths = @(
        (Join-Path $SolutionDir ".obj\$plat\$Configuration"),
        (Join-Path $SolutionDir ".$plat\$Configuration")
    )
    $soMap = @{}
    foreach ($sp in $searchPaths) {
        if (Test-Path $sp) {
            Get-ChildItem -Path $sp -Filter "*.so" -Recurse -EA SilentlyContinue | ForEach-Object {
                if (-not $soMap.ContainsKey($_.Name) -or $_.LastWriteTime -gt $soMap[$_.Name].LastWriteTime) {
                    $soMap[$_.Name] = $_
                }
            }
        }
    }
    foreach ($f in $soMap.Values) { Copy-Item $f.FullName -Destination $outDir -Force }

    # ── 校验 ──
    $missing = @()
    foreach ($so in $requiredSOs) {
        if (-not (Test-Path (Join-Path $outDir $so))) { $missing += $so }
    }

    $soFiles = Get-ChildItem -Path $outDir -Filter "*.so" | Sort-Object Name
    if ($missing.Count -gt 0) {
        Write-Host "  [WARN] ${abi}: missing $($missing.Count) .so: $($missing -join ', ')" -ForegroundColor Red
        $totalErrors++
    }
    Write-Host "  ${abi}: $($soFiles.Count)/$($requiredSOs.Count) .so collected" -ForegroundColor $(if ($missing.Count -eq 0) { "Green" } else { "Yellow" })

    # ── strip (Release) ──
    if ($stripExe -and $Configuration -eq "Release") {
        foreach ($so in $soFiles) {
            $sizeBefore = [math]::Round($so.Length / 1KB, 1)
            & $stripExe.FullName --strip-all $so.FullName
            $so.Refresh()
            $sizeAfter = [math]::Round($so.Length / 1KB, 1)
        }
        Write-Host "  stripped $($soFiles.Count) files" -ForegroundColor Yellow
    }

    # ── 同步到 jniLibs ──
    $jniLibsDir = Join-Path $SolutionDir "app\GGELUA\app\src\main\jniLibs\$abi"
    if (-not (Test-Path $jniLibsDir)) { New-Item -ItemType Directory -Path $jniLibsDir -Force | Out-Null }
    $soFiles = Get-ChildItem -Path $outDir -Filter "*.so"
    foreach ($f in $soFiles) { Copy-Item $f.FullName -Destination $jniLibsDir -Force }
}

# ── 汇总 ──
Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host " Build Summary ($Configuration)" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
foreach ($plat in $platforms) {
    $abi = $abiMap[$plat]
    $outDir = Join-Path $SolutionDir ".out\$abi"
    $count = (Get-ChildItem -Path $outDir -Filter "*.so" -EA SilentlyContinue).Count
    $status = if ($count -ge $requiredSOs.Count) { "[OK]" } else { "[!!]" }
    $color = if ($count -ge $requiredSOs.Count) { "Green" } else { "Yellow" }
    $totalKB = [math]::Round((Get-ChildItem -Path $outDir -Filter "*.so" -EA SilentlyContinue | Measure-Object Length -Sum).Sum / 1KB, 0)
    Write-Host "  $status $abi : $count/$($requiredSOs.Count) .so ($totalKB KB)" -ForegroundColor $color
}
Write-Host "  Output: $(Join-Path $SolutionDir '.out')" -ForegroundColor Cyan

# ── Deploy mode ──
if ($Deploy) {
    $deployScript = Join-Path $ScriptsDir "deploy_apk.py"
    if (Test-Path $deployScript) {
        Write-Host "`n Deploying to APK..." -ForegroundColor Magenta
        python $deployScript --so-dir (Join-Path $SolutionDir ".out")
    } else {
        Write-Error "deploy_apk.py not found at $deployScript"
    }
}

if ($totalErrors -gt 0) {
    Write-Host "`n[WARN] $totalErrors platform(s) have missing .so files" -ForegroundColor Yellow
} else {
    Write-Host "`nAll platforms built successfully!" -ForegroundColor Green
}
