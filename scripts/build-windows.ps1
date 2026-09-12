<#
    Builds every Windows row of the release matrix on this machine.

    x64  : AVX2 {on,off} x OpenMP {on,off} x CUDA {on,off}  = 8
    Win32: AVX2 {on,off} x OpenMP {on,off}                  = 4
           (there is no 32-bit CUDA and has not been since CUDA 9)
    ARM64: OpenMP {on,off}                                  = 2
           (AVX2 is an x86 instruction set; no CUDA target here either)

    Needs Visual Studio with the C++ workload, and the CUDA Toolkit with its
    Visual Studio Integration component for the CUDA rows. Rows whose
    prerequisites are missing are reported and skipped, not failed.

        pwsh -File scripts/build-windows.ps1
        pwsh -File scripts/build-windows.ps1 -SkipCuda
        pwsh -File scripts/build-windows.ps1 -Generator "Visual Studio 17 2022"
#>

[CmdletBinding()]
param(
    [string]   $Generator  = "Visual Studio 18 2026",
    [string]   $Version    = "",    # empty = whatever CMakeLists.txt says
    [string]   $OutDir     = "dist",
    # CUDA 12.x covers sm_50..sm_90. CUDA 13 dropped everything below Turing,
    # so drop 50;60;61;70 from this list if that is the toolkit installed.
    [string]   $CudaArchs  = "75;80;86;89;90",
    [switch]   $SkipCuda,
    [switch]   $SkipArm
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
# major.minor from CMakeLists.txt. $Version defaults to empty because param()
# runs before this does.
function Get-ProjectVersion {
    $cmake = Join-Path $repo "CMakeLists.txt"
    if (Test-Path $cmake) {
        $match = [regex]::Match((Get-Content $cmake -Raw),
                                'project\s*\([^)]*?VERSION\s+(\d+)\.(\d+)')
        if ($match.Success) {
            return "$($match.Groups[1].Value).$($match.Groups[2].Value)"
        }
    }
    return ""
}
if (-not $Version) {
    $Version = Get-ProjectVersion
    if (-not $Version) {
        throw "No -Version given and no version found in CMakeLists.txt"
    }
}

$dist = Join-Path $repo $OutDir
New-Item -ItemType Directory -Force -Path $dist | Out-Null

# The OpenMP runtime is the one MSVC cannot link statically, so the OpenMP rows
# carry it beside them. /openmp:llvm wants libomp140.<arch>.dll; vcomp140.dll is
# the classic runtime and only the fallback. Newer toolsets bump the VC1xx
# directory name, and each keeps its own copy.
function Get-RuntimeVersion($file) {
    $parsed = [version]"0.0"
    if ($file.FullName -match "\\MSVC\\(\d+(\.\d+)+)\\" -and
        [version]::TryParse($Matches[1], [ref] $parsed)) {
        return $parsed
    }
    $raw = $file.VersionInfo.FileVersion
    if ($raw -and [version]::TryParse(($raw -split " ")[0], [ref] $parsed)) {
        return $parsed
    }
    return [version]"0.0"
}

# A collapsed loop calls __kmpc_calc_original_ivs_*, which older copies of
# libomp do not export. Packing one of those links and zips perfectly and then
# dies on the user's machine with "entry point not found", so the copy that is
# about to be packed is read and checked. Export names are plain ASCII in the
# file, so looking for the string is enough.
function Test-OpenMpCollapse([string] $path) {
    if ([IO.Path]::GetFileName($path) -notlike "libomp*") { return $true }
    try {
        $text = [Text.Encoding]::GetEncoding(28591).GetString(
            [IO.File]::ReadAllBytes($path))
    } catch {
        return $false
    }
    return $text.Contains("__kmpc_calc_original_ivs_rectang")
}

function Find-OpenMpRuntime {
    param([string] $Bits = "x64")
    $names = switch ($Bits) {
        "x64"   { @("libomp140.x86_64.dll", "vcomp140.dll") }
        "arm64" { @("libomp140.aarch64.dll", "vcomp140.dll") }
        default { @("libomp140.i386.dll", "vcomp140.dll") }
    }
    $roots = @()
    if ($env:VCToolsRedistDir) { $roots += $env:VCToolsRedistDir }
    $roots += "${env:ProgramFiles}\Microsoft Visual Studio"
    $roots += "${env:ProgramFiles(x86)}\Microsoft Visual Studio"
    foreach ($name in $names) {
        $hits = @()
        foreach ($root in $roots) {
            if (-not (Test-Path $root)) { continue }
            $hits += Get-ChildItem $root -Recurse -Filter $name -ErrorAction SilentlyContinue |
                     Where-Object { $_.FullName -match "\\$Bits\\" }
        }
        if (-not $hits) { continue }
        $ranked = $hits | Sort-Object -Property @{
            Expression = { Get-RuntimeVersion $_ }
        } -Descending
        $good = $ranked | Where-Object { Test-OpenMpCollapse $_.FullName } |
                Select-Object -First 1
        if ($good) { return $good.FullName }
        $best = $ranked | Select-Object -First 1
        Write-Host ("    $($best.Name) exports no collapse entry point - " +
                    "this build will not start") -ForegroundColor Yellow
        return $best.FullName
    }
    return $null
}

function Build-Row {
    param($Arch, $Avx2, $OpenMp, $Cuda)

    $tags = @()
    if ($Avx2)   { $tags += "avx2" }
    if ($OpenMp) { $tags += "omp"  }
    if ($Cuda)   { $tags += "cuda" }
    $feature = if ($tags.Count) { $tags -join "-" } else { "plain" }
    # CMake's spelling of the architecture and the release's are not the same:
    # the generator wants Win32 and ARM64, the file names want x86 and arm64.
    $label   = switch ($Arch) {
        "x64"   { "windows-x64" }
        "ARM64" { "windows-arm64" }
        default { "windows-x86" }
    }
    $name    = "Fluid Solver $Version $label $feature"
    $build   = Join-Path $repo "build-$label-$feature"

    Write-Host "==> $name" -ForegroundColor Cyan
    Remove-Item $build -Recurse -Force -ErrorAction SilentlyContinue

    $cmakeArch = $Arch
    $args = @(
        "-S", $repo, "-B", $build,
        "-G", $Generator, "-A", $cmakeArch,
        "-DCFD_STATIC=ON",
        "-DCFD_ENABLE_AVX2=$(if($Avx2){'ON'}else{'OFF'})",
        "-DCFD_ENABLE_OPENMP=$(if($OpenMp){'ON'}else{'OFF'})",
        "-DCFD_ENABLE_CUDA=$(if($Cuda){'ON'}else{'OFF'})"
    )
    # Without this a missing toolkit silently produces a CPU-only binary that
    # would then be published under a name promising CUDA.
    if ($Cuda) {
        $args += "-DCFD_ENABLE_CUDA_EXPLICIT=ON"
        $args += "-DCFD_CUDA_ARCHITECTURES=$CudaArchs"
    }

    & cmake @args 2>&1 | Out-String | Write-Verbose
    if ($LASTEXITCODE -ne 0) {
        Write-Host "    configure failed - skipped" -ForegroundColor Yellow
        return
    }
    & cmake --build $build --config Release --parallel 2>&1 | Out-String | Write-Verbose
    if ($LASTEXITCODE -ne 0) {
        Write-Host "    build failed - skipped" -ForegroundColor Yellow
        return
    }

    $exe = Join-Path $build "bin\Release\Fluid Solver.exe"
    if (-not (Test-Path $exe)) {
        Write-Host "    no executable produced - skipped" -ForegroundColor Yellow
        return
    }

    # One folder per row, so the OpenMP rows can carry their DLL and the whole
    # folder is what gets zipped or handed to the installer.
    $rowDir = Join-Path $dist $name
    New-Item -ItemType Directory -Force -Path $rowDir | Out-Null
    Copy-Item $exe (Join-Path $rowDir "Fluid Solver.exe") -Force

    if ($OpenMp) {
        $bits = switch ($Arch) { "x64" { "x64" } "ARM64" { "arm64" } default { "x86" } }
        $runtime = Find-OpenMpRuntime $bits
        if ($runtime) {
            Copy-Item $runtime $rowDir -Force
            Write-Host "    + $([IO.Path]::GetFileName($runtime))" -ForegroundColor DarkGray
        } else {
            Write-Host "    no OpenMP runtime found - this build will not start without it" -ForegroundColor Yellow
        }
    }

    $size = [math]::Round((Get-Item (Join-Path $rowDir "Fluid Solver.exe")).Length / 1MB, 2)
    Write-Host "    ok, $size MB" -ForegroundColor Green
}

$rows = @()
foreach ($avx2 in $true, $false) {
    foreach ($omp in $true, $false) {
        foreach ($cuda in $true, $false) {
            if ($cuda -and $SkipCuda) { continue }
            $rows += ,@("x64", $avx2, $omp, $cuda)
        }
        $rows += ,@("Win32", $avx2, $omp, $false)
    }
}
if (-not $SkipArm) {
    foreach ($omp in $true, $false) { $rows += ,@("ARM64", $false, $omp, $false) }
}

foreach ($row in $rows) { Build-Row -Arch $row[0] -Avx2 $row[1] -OpenMp $row[2] -Cuda $row[3] }

Write-Host ""
Write-Host "Built into $dist :" -ForegroundColor Cyan
Get-ChildItem $dist -Directory | ForEach-Object { Write-Host "  $($_.Name)" }
