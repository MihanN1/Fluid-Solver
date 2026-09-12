<#
    Builds the release rows this machine can produce, then packs them.

    The whole matrix gets built. The installer lets the user turn each of the
    three switches on or off on its own, and it can only offer what is
    actually in dist\, so both sides of all three are built. AVX2 has to be
    split in any case: a binary cannot decide at runtime whether it may
    execute an AVX2 instruction, it just dies.

        windows-x64    AVX2 {on,off} x OpenMP {on,off} x CUDA {on,off} = 8
        windows-x86    AVX2 {on,off} x OpenMP {on,off}                 = 4
        windows-arm64  OpenMP {on,off}                                 = 2

    There is no 32-bit CUDA and has not been since CUDA 9, and no AVX2 or CUDA
    on ARM at all - AVX2 is an x86 instruction set and the toolkit has no
    Windows-on-ARM target. "plain" - no AVX2, no OpenMP, no CUDA - is the row
    that runs on anything, and the only one with no OpenMP runtime beside it.

    The installer is ONE file for all three architectures: it reads the
    processor at run time and unpacks the matching build, so nobody downloading
    a solver has to know what is inside their laptop. -PerArch builds the three
    single-architecture installers as well, for a smaller download.

        pwsh -File scripts\make-release.ps1 -Version 0.2
        pwsh -File scripts\make-release.ps1 -Version 0.2 -WithInstallers
        pwsh -File scripts\make-release.ps1 -Version 0.2 -Only Package

    -FromRelease fills dist\ from the published archives in release\<version>,
    or in -ReleaseDir <path>, instead of from a build, and cuts the two Windows
    installers from those. That is the only way the UI ever gets in: a
    "<variant>-ui" archive is put together by hand and no build produces one.
    -Only Installers does it by itself when dist\ holds no Windows rows.

        pwsh -File scripts\make-release.ps1 -Version 0.2 -Only Installers

    Needs Visual Studio with the C++ workload. The CUDA rows additionally need
    the CUDA Toolkit with its Visual Studio Integration component; without it
    they are reported as skipped and the rest still build. The CUDA
    architecture list is chosen from the installed toolkit version, so nothing
    has to be passed by hand.
#>

[CmdletBinding()]
param(
    [string] $Version   = "",       # empty = whatever CMakeLists.txt says
    [string] $Generator = "Visual Studio 17 2022",
    [string] $CudaArchs = "",       # empty = decide from the installed toolkit
    [switch] $WithInstallers,       # off until the UI folders exist
    [switch] $Skip32,
    [switch] $SkipArm,
    [switch] $PerArch,              # also cut one installer per architecture
    [switch] $FromRelease,          # rebuild dist\ from the published archives
    [switch] $FromDist,             # never do that, use dist\ as it stands
    [string] $ReleaseDir = "",      # empty = release\<version>
    [ValidateSet("All","Build","Installers","Package")]
    [string] $Only = "All"
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

# The installer's OutputBaseFilename starts with this, and signing has to name
# the file it produced.
$AppNameForSigning = "Fluid Solver"
$dist = Join-Path $repo "dist"
$rel  = Join-Path $repo "release\$Version"
$problems = New-Object System.Collections.Generic.List[string]

function Say($msg, $colour = "Cyan") { Write-Host $msg -ForegroundColor $colour }

# The lines that actually say what went wrong, or failing that the last few.
function Show-Tail($text) {
    $lines = $text -split "`r?`n" | Where-Object { $_.Trim() }
    $errs = $lines | Where-Object { $_ -match 'error|fatal|LNK\d|unresolved|Unsupported' }
    $show = if ($errs) { $errs | Select-Object -Last 12 } else { $lines | Select-Object -Last 12 }
    $show | ForEach-Object { Write-Host "      $_" -ForegroundColor DarkGray }
}

# ---------------------------------------------------------------- toolkit ---
# CUDA 13 dropped Maxwell, Pascal and Volta, and nvcc errors out rather than
# warning when asked for one of them, so the list follows the toolkit.
function Resolve-CudaArchs {
    if ($CudaArchs) { return $CudaArchs }
    $nvcc = Get-Command nvcc -ErrorAction SilentlyContinue
    if (-not $nvcc) { return "75;80;86;89;90" }
    $out = & nvcc --version 2>&1 | Out-String
    if ($out -match 'release\s+(\d+)\.') {
        if ([int]$Matches[1] -ge 13) {
            Write-Host "  CUDA $($Matches[1]).x detected: targeting Turing and newer" -ForegroundColor DarkGray
            return "75;80;86;89;90"
        }
        Write-Host "  CUDA $($Matches[1]).x detected: targeting Maxwell and newer" -ForegroundColor DarkGray
        return "50;60;61;70;75;80;86;89;90"
    }
    return "75;80;86;89;90"
}

# The redist version a copy of the runtime belongs to. The path says it -
# ...\VC\Redist\MSVC\14.44.35112\x64\... - and that is the toolset it shipped
# with, which is what has to be compared. FileVersion is the fallback for a
# copy that sits somewhere else.
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

# A collapsed loop compiles into a call to __kmpc_calc_original_ivs_*, which
# libomp only started exporting a few toolsets ago. Packing an older copy links
# and zips perfectly and then dies on the user's machine with "entry point not
# found", so the copy that is about to be packed gets read and checked. Export
# names sit in the file as plain ASCII, which is why looking for the string is
# enough and no PE parser is needed.
function Test-OpenMpCollapse([string] $path) {
    # vcomp140.dll carries no __kmpc_ names at all and is only ever the
    # fallback for a toolchain without /openmp:llvm, so it is not asked.
    if ([IO.Path]::GetFileName($path) -notlike "libomp*") { return $true }
    try {
        $text = [Text.Encoding]::GetEncoding(28591).GetString(
            [IO.File]::ReadAllBytes($path))
    } catch {
        return $false
    }
    return $text.Contains("__kmpc_calc_original_ivs_rectang")
}

function Find-OpenMpRuntime($Bits) {
    # /openmp:llvm links libomp140.<arch>.dll, which sits beside vcomp140.dll in
    # the same redist tree. vcomp140.dll is still looked for after it, so a
    # toolchain that fell back to the classic runtime still packs.
    #
    # Every installed toolset keeps its own copy and they are not
    # interchangeable. Taking whichever one the recursive search reached first
    # is what shipped a 2019-era libomp beside a binary built by the 2022
    # toolset, so the newest is taken instead and it has to answer for the
    # collapse entry point before it goes into an archive.
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
                    "the OpenMP rows will not start") -ForegroundColor Yellow
        $problems.Add("the packed $($best.Name) is older than the compiler " +
                      "that built the OpenMP rows - install a current MSVC " +
                      "toolset and rebuild them")
        return $best.FullName
    }
    return $null
}

# Authenticode, when a certificate is configured. Decided once. With no certificate this used to print the same eleven-line
# paragraph after every row, which buried the build output it was printed
# between.
$script:SigningOn = [bool]($env:CFD_SIGN_THUMBPRINT -or $env:CFD_SIGN_PFX)
if (-not $script:SigningOn) {
    Write-Host "  signing: no certificate configured (CFD_SIGN_THUMBPRINT or CFD_SIGN_PFX), nothing will be signed" -ForegroundColor DarkGray
}

function Invoke-Signing($TargetPaths) {
    if (-not $script:SigningOn) { return }
    $script = Join-Path $PSScriptRoot "sign-windows.ps1"
    if (-not (Test-Path $script)) { return }
    & pwsh -NoLogo -NoProfile -File $script @TargetPaths
    if ($LASTEXITCODE -ne 0) {
        $problems.Add("signing failed for: " + ($TargetPaths -join ", "))
    }
}

function Find-Iscc {
    $c = Get-Command iscc -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    foreach ($p in @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
                     "${env:ProgramFiles}\Inno Setup 6\ISCC.exe")) {
        if (Test-Path $p) { return $p }
    }
    return $null
}

# ----------------------------------------------------------- the 12 rows ---
$Rows = @()
foreach ($avx2 in $true, $false) {
    foreach ($omp in $true, $false) {
        foreach ($cuda in $true, $false) {
            $Rows += @{ Arch = "x64"; Avx2 = $avx2; OpenMp = $omp; Cuda = $cuda }
        }
        # No 32-bit CUDA exists, so that axis is not in the 32-bit rows.
        $Rows += @{ Arch = "Win32"; Avx2 = $avx2; OpenMp = $omp; Cuda = $false }
    }
}
# ARM64 varies on OpenMP alone: AVX2 is an x86 instruction set, and the CUDA
# toolkit has no Windows-on-ARM target.
foreach ($omp in $true, $false) {
    $Rows += @{ Arch = "ARM64"; Avx2 = $false; OpenMp = $omp; Cuda = $false }
}

function Build-Row($Row, $Archs) {
    $tags = @()
    if ($Row.Avx2)   { $tags += "avx2" }
    if ($Row.OpenMp) { $tags += "omp"  }
    if ($Row.Cuda)   { $tags += "cuda" }
    $feature = if ($tags.Count) { $tags -join "-" } else { "plain" }
    # The generator's spelling of the architecture and the release's are not the
    # same: CMake wants Win32 and ARM64, the file names want x86 and arm64.
    $label   = switch ($Row.Arch) {
        "x64"   { "windows-x64" }
        "ARM64" { "windows-arm64" }
        default { "windows-x86" }
    }
    $name    = "Fluid Solver $Version $label $feature"
    $build   = Join-Path $repo "build-$label-$feature"

    Write-Host "  $name ... " -NoNewline
    Remove-Item $build -Recurse -Force -ErrorAction SilentlyContinue

    $cmakeArgs = @("-S", $repo, "-B", $build, "-G", $Generator, "-A", $Row.Arch,
              "-DCFD_STATIC=ON",
              "-DCFD_ENABLE_AVX2=$(if($Row.Avx2){'ON'}else{'OFF'})",
              "-DCFD_ENABLE_OPENMP=$(if($Row.OpenMp){'ON'}else{'OFF'})",
              "-DCFD_ENABLE_CUDA=$(if($Row.Cuda){'ON'}else{'OFF'})")
    # Without these a missing toolkit or runtime quietly produces a binary that
    # would then be published under a name promising the feature it lacks.
    if ($Row.Cuda)   { $cmakeArgs += @("-DCFD_ENABLE_CUDA_EXPLICIT=ON", "-DCFD_CUDA_ARCHITECTURES=$Archs") }
    if ($Row.Cuda) {
        $nvccPath = & where nvcc 2>$null | Select-Object -First 1
        if (-not $nvccPath) {
            # Пробуем по CUDA_PATH
            $nvccPath = Get-ChildItem "$env:CUDA_PATH\bin\nvcc.exe" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty FullName
        }
        if ($nvccPath) {
            $cmakeArgs += "-DCMAKE_CUDA_COMPILER=`"$nvccPath`""
            Write-Host "      using nvcc: $nvccPath" -ForegroundColor DarkGray
        } else {
            Write-Host "      nvcc not found, this row will fail" -ForegroundColor Yellow
        }
    }
    if ($Row.OpenMp) { $cmakeArgs += "-DCFD_ENABLE_OPENMP_EXPLICIT=ON" }

    # The whole log goes to a file and the tail goes to the screen. Hiding a
    # compiler error behind a -Verbose nobody passes is not a summary, it is a
    # dead end.
    $logDir = Join-Path $repo "logs"
    New-Item -ItemType Directory -Force -Path $logDir | Out-Null
    $log = Join-Path $logDir "$label-$feature.log"

    $out = & cmake @cmakeArgs 2>&1 | Out-String
    $out | Set-Content $log
    if ($LASTEXITCODE -ne 0) {
        Write-Host "configure failed" -ForegroundColor Yellow
        Show-Tail $out
        Write-Host "      full log: $log" -ForegroundColor DarkGray
        $problems.Add("$name - configure failed, see logs\$label-$feature.log")
        return
    }
    $out = & cmake --build $build --config Release --parallel 2>&1 | Out-String
    $out | Add-Content $log
    if ($LASTEXITCODE -ne 0) {
        Write-Host "build failed" -ForegroundColor Yellow
        Show-Tail $out
        Write-Host "      full log: $log" -ForegroundColor DarkGray
        $problems.Add("$name - build failed, see logs\$label-$feature.log")
        return
    }

    $exe = Join-Path $build "bin\Release\Fluid Solver.exe"
    if (-not (Test-Path $exe)) { Write-Host "no executable" -ForegroundColor Yellow; $problems.Add("$name - no executable produced"); return }

    $rowDir = Join-Path $dist $name
    Remove-Item $rowDir -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $rowDir | Out-Null
    Copy-Item $exe (Join-Path $rowDir "Fluid Solver.exe") -Force

    # MSVC has no static OpenMP runtime; the build does not start without this.
    $extra = ""
    if ($Row.OpenMp) {
        # The redist tree is laid out by architecture folder: x64, x86, arm64.
        $bits = switch ($Row.Arch) {
            "x64"   { "x64" }
            "ARM64" { "arm64" }
            default { "x86" }
        }
        $dll = Find-OpenMpRuntime $bits
        if ($dll) {
            Copy-Item $dll $rowDir -Force
            $extra = " + " + (Split-Path $dll -Leaf)
        }
        else { $problems.Add("$name - no OpenMP runtime DLL found, this build will not start") }
    }

    # Signed here, one row at a time, rather than after the fact: the installer
    # carries these executables verbatim, so signing them now is what makes the
    # installed program signed as well as the installer that put it there.
    Invoke-Signing @((Join-Path $rowDir "Fluid Solver.exe"))

    $mb = [math]::Round((Get-Item (Join-Path $rowDir "Fluid Solver.exe")).Length / 1MB, 2)
    Write-Host "ok, $mb MB$extra" -ForegroundColor Green
}

function Build-All {
    Say "Building the executables"
    New-Item -ItemType Directory -Force -Path $dist | Out-Null

    $haveNvcc = [bool](Get-Command nvcc -ErrorAction SilentlyContinue)
    $archs = Resolve-CudaArchs

    if (-not $haveNvcc) {
        Write-Host "  (the four CUDA rows are skipped: nvcc is not on PATH)" -ForegroundColor Yellow
        $problems.Add("the four CUDA rows were skipped - nvcc is not on PATH, install the CUDA Toolkit with Visual Studio Integration")
    }

    foreach ($row in $Rows) {
        if ($row.Arch -eq "Win32" -and $Skip32) { continue }
        if ($row.Arch -eq "ARM64" -and $SkipArm) { continue }
        if ($row.Cuda -and -not $haveNvcc) { continue }
        Build-Row $row $archs
    }
}

# --------------------------------------------- dist out of the archives ----
# The installer reads dist\, and up to now a build on this machine was the only
# thing that ever filled it. That cannot produce the whole payload any more: a
# "<variant>-ui" folder is put together by hand and exists nowhere else, so an
# installer cut from a build has no UI to offer. Rebuilding dist\ from
# release\<version> instead makes the installer carry exactly what was
# published - solver rows and UI rows alike - and nothing that was not.
#
# Expand-Archive rather than scripts\unpack-release.py, so this needs nothing
# but PowerShell. It is also the easy half of that script: a Windows row and a
# UI row both stay folders, and only Linux and macOS rows have to collapse back
# into a single file.
function Expand-ReleaseRows {
    param([string] $Source)

    Say "Rebuilding dist\ from $Source"
    if (-not (Test-Path $Source)) {
        Write-Host "  no such folder" -ForegroundColor Yellow
        $problems.Add("dist\ was not rebuilt - $Source does not exist")
        return
    }
    New-Item -ItemType Directory -Force -Path $dist | Out-Null

    $taken = 0
    Get-ChildItem $Source -Filter *.zip -File | Sort-Object Name | ForEach-Object {
        # "Fluid Solver <ver> windows-<arch> <feature>[-ui]" and nothing else:
        # the Linux and macOS rows in the same folder are not this machine's to
        # unpack, and the source archive is not a row at all.
        # A space or a dot between the parts: GitHub rewrites spaces to dots in
        # release asset filenames, so the same archive downloaded from a tag
        # arrives as "Fluid.Solver.0.1.windows-x64.avx2.zip".
        if ($_.BaseName -notmatch "^Fluid[ .]Solver[ .]$([regex]::Escape($Version))[ .]windows-(x64|x86|arm64)[ .][a-z0-9-]+$") { return }

        $stage = Join-Path ([System.IO.Path]::GetTempPath()) ([guid]::NewGuid())
        Expand-Archive -LiteralPath $_.FullName -DestinationPath $stage -Force
        $inner = Get-ChildItem $stage -Directory
        if ($inner.Count -ne 1) {
            Write-Host "  $($_.Name): expected one folder inside, found $($inner.Count)" -ForegroundColor Yellow
            $problems.Add("$($_.Name) - expected exactly one folder inside the archive")
            Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
            return
        }
        # dist\ always holds the spaced form, because that is what the .iss
        # variant folders are looked up by.
        $target = Join-Path $dist ($_.BaseName -replace "^Fluid[ .]Solver[ .]", "Fluid Solver " `
                                              -replace "[ .](windows-(?:x64|x86|arm64))[ .]", ' $1 ')
        Remove-Item $target -Recurse -Force -ErrorAction SilentlyContinue
        Move-Item $inner[0].FullName $target
        Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
        Write-Host "  $($_.BaseName)"
        $taken++
    }

    if ($taken -eq 0) {
        Write-Host "  nothing matched 'Fluid Solver $Version windows-<arch> <feature>.zip'" -ForegroundColor Yellow
        $problems.Add("dist\ was not rebuilt - no Windows rows for $Version in $Source")
    }
    Write-Host ""
}

# ------------------------------------------------- the icon on the UI ------
# The UI was built without a resource script, so "Fluid Solver UI.exe" carries
# no icon and Explorer draws it as a blank binary. This puts the project icon on
# it inside the published archives, before anything unpacks them, so both the
# installers and the archives themselves end up with it. Windows is where this
# has to happen: writing a PE resource is done with Windows' own resource
# updater, and this is the machine that has it.
function Set-UiIcon {
    param([string] $Source)

    $script = Join-Path $repo "scripts\stamp-ui-icon.py"
    if (-not (Test-Path $script)) { return }
    if (-not (Test-Path $Source)) { return }
    # Both spellings, for the same reason Expand-ReleaseRows accepts both.
    $archives = Get-ChildItem $Source -File -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match "^Fluid[ .]Solver[ .]$([regex]::Escape($Version))[ .].*-ui\.zip$" }
    if (-not $archives) { return }

    Say "Putting the icon on the UI executable"
    & python $script $Version --release $Source
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  some UI archives still have no icon" -ForegroundColor Yellow
        $problems.Add("the UI icon could not be written into every -ui archive")
    }
    Write-Host ""
}

# ------------------------------------------------------ the installers -----
function Build-Installers {
    # Asked for outright, or asked for by implication: "-Only Installers" with
    # no Windows rows in dist\ can only have meant the archives.
    $haveRows = [bool](Get-ChildItem $dist -Directory -ErrorAction SilentlyContinue |
                       Where-Object { $_.Name -like "Fluid Solver $Version windows-*" })
    if (-not $FromDist -and ($FromRelease -or ($Only -eq "Installers" -and -not $haveRows))) {
        $src = if ($ReleaseDir) { $ReleaseDir } else { $rel }
        Set-UiIcon $src
        Expand-ReleaseRows $src
    }

    Say "Building the installers"
    $iscc = Find-Iscc
    if (-not $iscc) {
        Write-Host "  Inno Setup not found - skipped. Get it from jrsoftware.org." -ForegroundColor Yellow
        $problems.Add("installers - Inno Setup (iscc) is not installed")
        return
    }
    $iss = Join-Path $repo "installer\windows\fluid-solver.iss"

    # The one everybody downloads: all three architectures in a single file,
    # picked at run time. /DArch is left out entirely, which is what the script
    # reads as "all".
    $anyRow = Get-ChildItem $dist -Directory -ErrorAction SilentlyContinue |
              Where-Object { $_.Name -like "Fluid Solver $Version windows-* *" }
    if (-not $anyRow) {
        Write-Host "  nothing to package - skipped" -ForegroundColor Yellow
        $problems.Add("installers - no windows rows in dist\")
        return
    }
    Write-Host "  windows (x64 + x86 + arm64, whichever were built) ... " -NoNewline
    & $iscc "/DAppVersion=$Version" "/DDistDir=$dist" $iss 2>&1 | Out-String | Write-Verbose
    if ($LASTEXITCODE -eq 0) {
        Write-Host "ok" -ForegroundColor Green
        Invoke-Signing @((Join-Path $dist "$AppNameForSigning $Version windows setup.exe"))
    }
    else { Write-Host "failed" -ForegroundColor Yellow; $problems.Add("windows installer - iscc failed, rerun with -Verbose") }

    if (-not $PerArch) { return }

    # The smaller downloads, for people who know which one they want.
    foreach ($arch in "x64", "x86", "arm64") {
        if ($arch -eq "x86" -and $Skip32) { continue }
        if ($arch -eq "arm64" -and $SkipArm) { continue }
        $any = Get-ChildItem $dist -Directory -ErrorAction SilentlyContinue |
               Where-Object { $_.Name -like "Fluid Solver $Version windows-$arch *" }
        if (-not $any) { Write-Host "  windows-$arch ... nothing to package - skipped" -ForegroundColor Yellow; continue }
        Write-Host "  windows-$arch ... " -NoNewline
        & $iscc "/DAppVersion=$Version" "/DArch=$arch" "/DDistDir=$dist" $iss 2>&1 | Out-String | Write-Verbose
        if ($LASTEXITCODE -eq 0) {
            Write-Host "ok" -ForegroundColor Green
            Invoke-Signing @((Join-Path $dist "$AppNameForSigning $Version windows-$arch setup.exe"))
        }
        else { Write-Host "failed" -ForegroundColor Yellow; $problems.Add("windows-$arch installer - iscc failed, rerun with -Verbose") }
    }
}

# --------------------------------------------------------- the release ----
function Package {
    Say "Assembling release\$Version"
    Remove-Item $rel -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $rel | Out-Null

    # Both shapes, not just folders: a Windows row is a directory because of the
    # DLL beside the exe, while a Linux or macOS row dropped in from the other
    # script is a single file. Filtering on -Directory is why those used to
    # vanish from the release without a word.
    Get-ChildItem $dist -ErrorAction SilentlyContinue |
        Where-Object {
            $_.Name -like "Fluid Solver $Version *" -and
            $_.Name -notlike "*setup.exe" -and $_.Extension -notin ".zip", ".pkg", ".run"
        } |
        ForEach-Object {
            $stage = Join-Path ([System.IO.Path]::GetTempPath()) ([guid]::NewGuid())
            $inner = Join-Path $stage $_.Name
            New-Item -ItemType Directory -Force -Path $inner | Out-Null
            if ($_.PSIsContainer) {
                Copy-Item "$($_.FullName)\*" $inner -Recurse -Force
            } else {
                Copy-Item $_.FullName (Join-Path $inner "Fluid Solver") -Force
            }
            foreach ($f in "README.md", "LICENSE") {
                if (Test-Path (Join-Path $repo $f)) { Copy-Item (Join-Path $repo $f) $inner }
            }
            New-Item -ItemType Directory -Force -Path (Join-Path $inner "output") | Out-Null
            Compress-Archive -Path $inner -DestinationPath (Join-Path $rel "$($_.Name).zip") -Force
            Remove-Item $stage -Recurse -Force
            Write-Host "  $($_.Name).zip"
        }

    Get-ChildItem $dist -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like "*setup.exe" -or $_.Extension -in ".pkg", ".run" } |
        ForEach-Object { Copy-Item $_.FullName $rel; Write-Host "  $($_.Name)" }

    # The two standalone files: what the release was built from, and how to use
    # it. lib\sfml belongs to the UI and dwarfs everything else.
    $stage = Join-Path ([System.IO.Path]::GetTempPath()) ([guid]::NewGuid())
    $srcDir = Join-Path $stage "Fluid-Solver-Source-Code"
    New-Item -ItemType Directory -Force -Path $srcDir | Out-Null
    # .toolchain holds a Linux virtualenv when the build ran through WSL or a
    # container. Its symlinks are unreadable from Windows and Compress-Archive
    # stops the whole run on the first one, so it never belongs in here.
    $skip = @(".git", ".github", ".vs", ".vscode", "out", "dist", "release",
              "_to_delete", ".toolchain", "logs")
    Get-ChildItem $repo -Force | Where-Object {
        $_.Name -notin $skip -and $_.Name -notlike "build*" -and $_.Name -notlike "bt-*"
    } | ForEach-Object { Copy-Item $_.FullName $srcDir -Recurse -Force -ErrorAction SilentlyContinue }
    Remove-Item (Join-Path $srcDir "lib\sfml") -Recurse -Force -ErrorAction SilentlyContinue
    Get-ChildItem (Join-Path $srcDir "output") -Filter *.vtk -ErrorAction SilentlyContinue | Remove-Item -Force
    Compress-Archive -Path $srcDir -DestinationPath (Join-Path $rel "Fluid-Solver-Source-Code.zip") -Force
    Remove-Item $stage -Recurse -Force
    Write-Host "  Fluid-Solver-Source-Code.zip"

    Copy-Item (Join-Path $repo "README.md") $rel -Force
    Write-Host "  README.md"

    # The guide belongs beside the files it describes. Somebody opening
    # release\0.2\ six months from now should not have to go back to the branch
    # to find out what a "-ui" archive is or which of the 34 rows to take.
    $guide = Join-Path $repo "RELEASE-GUIDE.md"
    if (Test-Path $guide) {
        Copy-Item $guide $rel -Force
        Write-Host "  RELEASE-GUIDE.md"
    }
    # Release notes are written per version, so this is the one file that is
    # copied only when it exists for this one.
    foreach ($candidate in @((Join-Path $repo "release\RELEASE-NOTES-$Version.md"),
                             (Join-Path $repo "RELEASE-NOTES-$Version.md"))) {
        if (Test-Path $candidate) {
            Copy-Item $candidate $rel -Force
            Write-Host "  $(Split-Path $candidate -Leaf)"
            break
        }
    }

    Get-ChildItem $rel -File | Where-Object { $_.Name -ne "SHA256SUMS.txt" } | ForEach-Object {
        "{0}  {1}" -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower(), $_.Name
    } | Set-Content (Join-Path $rel "SHA256SUMS.txt") -Encoding ascii
}

# ------------------------------------------------------------------ run ---
Say "Fluid Solver $Version - Windows"
Write-Host ""
if ($Only -in "All","Build")      { Build-All; Write-Host "" }
if ($Only -eq "Installers" -or ($Only -eq "All" -and $WithInstallers)) { Build-Installers; Write-Host "" }
if ($Only -in "All","Package")    { Package;   Write-Host "" }

Say "Done"
if (Test-Path $rel) {
    Get-ChildItem $rel | ForEach-Object { "{0,10:N0} KB  {1}" -f ($_.Length / 1KB), $_.Name | Write-Host }
}
if (-not $WithInstallers -and $Only -eq "All") {
    Write-Host ""
    Write-Host "Installers were not built. Add -WithInstallers for those; the UI becomes a component of them once dist\ui-windows-<arch> exists." -ForegroundColor DarkGray
}
if ($problems.Count) {
    Write-Host ""
    Say "$($problems.Count) thing(s) did not work:" "Yellow"
    $problems | ForEach-Object { Write-Host "  $_" -ForegroundColor Yellow }
}
Write-Host ""
Write-Host "Linux and macOS rows are built by scripts/make-release.sh on those systems."
Write-Host "Drop their dist\ output in beside this one and rerun with -Only Package."
