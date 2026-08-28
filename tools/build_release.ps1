param(
    [string] $BuildDirectory = "build",
    [string] $DistributionDirectory = "dist",
    [string] $CMakeGenerator = "Ninja"
)

$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDirectory))
$distributionRoot = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $DistributionDirectory))

function Assert-NoMarioProcess {
    param([Parameter(Mandatory = $true)] [string] $Context)

    $running = @(Get-Process -Name "MarioFundamentals" -ErrorAction SilentlyContinue)
    if ($running.Count -ne 0) {
        $identifiers = ($running | ForEach-Object { $_.Id }) -join ", "
        throw "MarioFundamentals process detected $Context (PIDs: $identifiers)."
    }
}

function Invoke-HiddenExecutableCheck {
    param(
        [Parameter(Mandatory = $true)] [string] $FilePath,
        [Parameter(Mandatory = $true)] [string] $Arguments,
        [Parameter(Mandatory = $true)] [string] $StandardOutput,
        [Parameter(Mandatory = $true)] [string] $StandardError,
        [Parameter(Mandatory = $true)] [string] $Label,
        [string] $WorkingDirectory = "",
        [int] $TimeoutMilliseconds = 180000
    )

    Assert-NoMarioProcess -Context "before $Label"
    Remove-Item -LiteralPath $StandardOutput -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $StandardError -ErrorAction SilentlyContinue
    if (-not $WorkingDirectory) { $WorkingDirectory = $projectRoot }

    $process = $null
    try {
        $process = Start-Process -FilePath $FilePath -ArgumentList $Arguments `
            -WorkingDirectory $WorkingDirectory -WindowStyle Hidden `
            -RedirectStandardOutput $StandardOutput `
            -RedirectStandardError $StandardError -PassThru
        # Windows PowerShell can return a Process wrapper without retaining
        # ExitCode after a timed wait unless its native handle is materialized
        # while the child is alive.
        $nativeProcessHandle = $process.Handle
        if (-not $process.WaitForExit($TimeoutMilliseconds)) {
            if (-not $process.HasExited) {
                # This is the exact hidden QA process created above, never a
                # name-based or user-owned window/process cleanup.
                Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
                $process.WaitForExit(5000) | Out-Null
            }
            throw "$Label timed out after $TimeoutMilliseconds ms."
        }
        $process.WaitForExit()
        $process.Refresh()
        if (Test-Path -LiteralPath $StandardOutput) { Get-Content -LiteralPath $StandardOutput }
        if (Test-Path -LiteralPath $StandardError) { Get-Content -LiteralPath $StandardError }
        if ($process.ExitCode -ne 0) {
            throw "$Label failed with exit code $($process.ExitCode)."
        }
    }
    finally {
        if ($null -ne $process -and -not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            $process.WaitForExit(5000) | Out-Null
        }
        Assert-NoMarioProcess -Context "after $Label"
    }
}

cmake -S $projectRoot -B $buildRoot -G $CMakeGenerator -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed." }
cmake --build $buildRoot --parallel
if ($LASTEXITCODE -ne 0) { throw "Native build failed." }

$executable = Join-Path $buildRoot "MarioFundamentals.exe"
$auditRoot = Join-Path $projectRoot "work/audit"
New-Item -ItemType Directory -Force -Path $auditRoot | Out-Null
$standardOutput = Join-Path $buildRoot "self-test.out"
$standardError = Join-Path $buildRoot "self-test.err"
Invoke-HiddenExecutableCheck -FilePath $executable -Arguments "--self-test" `
    -StandardOutput $standardOutput -StandardError $standardError `
    -Label "Executable self-test"

$titleSkipOutput = Join-Path $buildRoot "title-board-skip.out"
$titleSkipError = Join-Path $buildRoot "title-board-skip.err"
Invoke-HiddenExecutableCheck -FilePath $executable -Arguments "--qa-title-board-skip" `
    -StandardOutput $titleSkipOutput -StandardError $titleSkipError `
    -Label "Macintosh live title/board-click integration test"
Write-Output "PASS macintosh_live_title_board_click_skip"

$macIntroInputOutput = Join-Path $buildRoot "mac-game-intro-input.out"
$macIntroInputError = Join-Path $buildRoot "mac-game-intro-input.err"
Invoke-HiddenExecutableCheck -FilePath $executable -Arguments "--qa-mac-game-intro-input" `
    -StandardOutput $macIntroInputOutput -StandardError $macIntroInputError `
    -Label "Macintosh selected-game intro input integration test"
Write-Output "PASS macintosh_selected_game_intro_input"

$dosIntroInputOutput = Join-Path $buildRoot "dos-game-intro-input.out"
$dosIntroInputError = Join-Path $buildRoot "dos-game-intro-input.err"
Invoke-HiddenExecutableCheck -FilePath $executable -Arguments "--qa-dos-game-intro-input" `
    -StandardOutput $dosIntroInputOutput -StandardError $dosIntroInputError `
    -Label "DOS selected-game intro input integration test"
Write-Output "PASS dos_selected_game_intro_input"

# Regenerate the static function traceability ledgers whenever the local
# disassembly/source evidence is available. This prevents a green runtime
# build from silently relying on stale code-audit CSV/JSON files.
$macResourceManifest = Join-Path $projectRoot "work/rip/manifest.json"
$macCodeResourceRoot = Join-Path $projectRoot "work/rip/resources/CODE"
$macDataResource = Join-Path $projectRoot "work/rip/resources/DATA/+00000.bin"
$macDecodedDataRoot = Join-Path $projectRoot "work/decoded-data"
$macDisassemblyRoot = Join-Path $projectRoot "work/disassembly"
$macFunctionSummary = Join-Path $macDisassemblyRoot "summary.json"
if (Test-Path -LiteralPath $macResourceManifest) {
    if (-not (Test-Path -LiteralPath $macCodeResourceRoot) -or
        -not (Test-Path -LiteralPath $macDataResource)) {
        throw "Macintosh resource evidence is present but its CODE/DATA inputs are missing."
    }
    python (Join-Path $PSScriptRoot "decode_data.py") `
        $macDataResource $macDecodedDataRoot
    if ($LASTEXITCODE -ne 0) { throw "Macintosh DATA decode failed." }

    $macA5World = Join-Path $macDecodedDataRoot "a5_world.bin"
    $macA5WorldSummary = Join-Path $macDecodedDataRoot "a5_world.json"
    python (Join-Path $PSScriptRoot "disassemble_code.py") `
        $macCodeResourceRoot $macDisassemblyRoot `
        --a5-world $macA5World --a5-world-summary $macA5WorldSummary `
        --data-resource $macDataResource
    if ($LASTEXITCODE -ne 0) { throw "Macintosh global call-graph disassembly failed." }

    python (Join-Path $PSScriptRoot "verify_mac_relocations.py") `
        $macCodeResourceRoot $macA5World $macA5WorldSummary $macDataResource `
        --report (Join-Path $auditRoot "mac-relocation-audit.json")
    if ($LASTEXITCODE -ne 0) { throw "Macintosh relocation topology audit failed." }
}

if (Test-Path -LiteralPath $macFunctionSummary) {
    python (Join-Path $PSScriptRoot "build_function_traceability.py") `
        $macFunctionSummary (Join-Path $auditRoot "function-traceability.csv") `
        --json-output (Join-Path $auditRoot "function-traceability-summary.json")
    if ($LASTEXITCODE -ne 0) { throw "Macintosh function traceability audit failed." }
}

$macCodeDisassembly = Join-Path $projectRoot "work/disassembly/CODE_01.asm"
if (Test-Path -LiteralPath $macCodeDisassembly) {
    python (Join-Path $PSScriptRoot "verify_mac_audio_routes.py") `
        (Join-Path $projectRoot "work/disassembly") `
        (Join-Path $auditRoot "mac-audio-route-audit.json")
    if ($LASTEXITCODE -ne 0) { throw "Macintosh tracked/direct audio-route audit failed." }
}

if (Test-Path -LiteralPath $macResourceManifest) {
    python (Join-Path $PSScriptRoot "verify_mac_game_intro_input.py") `
        $macCodeResourceRoot `
        --report (Join-Path $auditRoot "mac-game-intro-input-audit.json")
    if ($LASTEXITCODE -ne 0) { throw "Macintosh selected-game intro input audit failed." }
}

$dosExecutableManifestPath = Join-Path $auditRoot "dos-executable-manifest.json"
$dosRadareFunctions = Join-Path $projectRoot "work/disassembly/dos/radare-functions.json"
$dosRadareSections = Join-Path $projectRoot "work/disassembly/dos/radare-sections.json"
$dosOverlayRoot = Join-Path $projectRoot "work/disassembly/dos/overlays"
$dosFunctionResourceManifest = Join-Path $auditRoot "dos-resource-manifest.json"
if ((Test-Path -LiteralPath $dosExecutableManifestPath) -and
    (Test-Path -LiteralPath $dosRadareFunctions) -and
    (Test-Path -LiteralPath $dosRadareSections) -and
    (Test-Path -LiteralPath $dosOverlayRoot) -and
    (Test-Path -LiteralPath $dosFunctionResourceManifest)) {
    $dosExecutableDocument = Get-Content -LiteralPath $dosExecutableManifestPath -Raw |
        ConvertFrom-Json
    $dosOriginalExecutable = [System.IO.Path]::GetFullPath(
        (Join-Path $projectRoot $dosExecutableDocument.source.path))
    if (-not (Test-Path -LiteralPath $dosOriginalExecutable)) {
        throw "DOS function evidence is present but the original executable is missing."
    }
    $dosOverlayCsv = Join-Path $auditRoot "dos-overlay-function-traceability.csv"
    $dosOverlaySummary = Join-Path $auditRoot "dos-overlay-function-traceability-summary.json"
    python (Join-Path $PSScriptRoot "build_dos_overlay_function_traceability.py") `
        $dosExecutableManifestPath $dosOriginalExecutable $dosOverlayRoot `
        $dosFunctionResourceManifest $dosOverlayCsv --json-output $dosOverlaySummary
    if ($LASTEXITCODE -ne 0) { throw "DOS exact overlay function traceability audit failed." }

    python (Join-Path $PSScriptRoot "verify_dos_game_intro_input.py") `
        $dosExecutableManifestPath $dosOverlayRoot `
        --report (Join-Path $auditRoot "dos-game-intro-input-audit.json")
    if ($LASTEXITCODE -ne 0) { throw "DOS selected-game intro input audit failed." }

    python (Join-Path $PSScriptRoot "build_dos_function_traceability.py") `
        $dosRadareFunctions $dosRadareSections $dosExecutableManifestPath `
        (Join-Path $auditRoot "dos-function-traceability.csv") `
        --executable $dosOriginalExecutable --overlay-directory $dosOverlayRoot `
        --overlay-summary $dosOverlaySummary `
        --json-output (Join-Path $auditRoot "dos-function-traceability-summary.json")
    if ($LASTEXITCODE -ne 0) { throw "DOS resident/discovery function traceability audit failed." }

    $dosOverlayZero = Get-ChildItem -LiteralPath (Join-Path $dosOverlayRoot "code") `
        -Filter "overlay-00-*.bin" -File | Select-Object -First 1
    if (-not $dosOverlayZero) {
        throw "DOS dialogue-table evidence is present but overlay 0 is missing."
    }
    python (Join-Path $PSScriptRoot "verify_dos_dialogue_tables.py") `
        $dosExecutableManifestPath $dosOriginalExecutable $dosOverlayZero.FullName `
        --resource-manifest $dosFunctionResourceManifest `
        --report (Join-Path $auditRoot "dos-dialogue-table-verification.json")
    if ($LASTEXITCODE -ne 0) { throw "DOS host-dialogue table verification failed." }
}

function Invoke-PresentationQa {
    param(
        [string] $Argument,
        [string] $OutputDirectory,
        [string] $Label,
        [int] $ExpectedFrames,
        [long] $ExpectedBytes
    )

    $started = [DateTime]::UtcNow.AddSeconds(-1)
    $qaOutput = Join-Path $auditRoot "$Label-presentation-qa.out"
    $qaError = Join-Path $auditRoot "$Label-presentation-qa.err"
    Invoke-HiddenExecutableCheck -FilePath $executable -Arguments $Argument `
        -StandardOutput $qaOutput -StandardError $qaError `
        -WorkingDirectory $projectRoot -Label "$Label presentation QA"
    $allFrames = @(Get-ChildItem -LiteralPath $OutputDirectory -Filter "*.bmp" -File)
    if ($allFrames.Count -ne $ExpectedFrames) {
        throw "$Label presentation directory contains $($allFrames.Count) frames; expected exactly $ExpectedFrames. Remove obsolete QA captures."
    }
    $freshFrames = @($allFrames | Where-Object { $_.LastWriteTimeUtc -ge $started })
    if ($freshFrames.Count -ne $ExpectedFrames) {
        throw "$Label presentation QA wrote $($freshFrames.Count) fresh frames; expected $ExpectedFrames."
    }
    $wrongSize = @($freshFrames | Where-Object { $_.Length -ne $ExpectedBytes })
    if ($wrongSize.Count -ne 0) {
        throw "$Label presentation QA wrote $($wrongSize.Count) frames at a non-native size."
    }
    Write-Output "PASS ${Label}_presentation_frames=$ExpectedFrames native_bmp_bytes=$ExpectedBytes"
}

Invoke-PresentationQa -Argument "--render-mac-qa" `
    -OutputDirectory (Join-Path $projectRoot "work/qa/mac") -Label "macintosh" `
    -ExpectedFrames 232 -ExpectedBytes 786486
Invoke-PresentationQa -Argument "--render-dos-qa" `
    -OutputDirectory (Join-Path $projectRoot "work/qa/dos") -Label "dos" `
    -ExpectedFrames 233 -ExpectedBytes 256054

# Independent references cover source fidelity at representative instants;
# this second layer pins every generated state so an unreferenced intro,
# opening, outcome, or roll frame cannot silently change behind a stable count.
python (Join-Path $PSScriptRoot "verify_qa_frame_corpus.py") `
    (Join-Path $PSScriptRoot "qa-frame-baseline.json") `
    --mac-directory (Join-Path $projectRoot "work/qa/mac") `
    --dos-directory (Join-Path $projectRoot "work/qa/dos") `
    --report (Join-Path $auditRoot "qa-frame-corpus-verification.json")
if ($LASTEXITCODE -ne 0) { throw "Deterministic QA frame corpus verification failed." }

# When the locally retained independent reference sets are available, compare
# original output with representative native gameplay frames. The captures
# remain unshipped source evidence, just like the disassembly inputs.
$visualReferenceRoot = Join-Path $projectRoot "work/references/original_startup"
if (Test-Path -LiteralPath $visualReferenceRoot) {
    $dosVisualReferenceRoot = Join-Path $projectRoot "work/references/mariowiki"
    $inventoryArguments = @(
        (Join-Path $PSScriptRoot "verify_visual_reference_inventory.py"),
        $visualReferenceRoot,
        "--json-output",
        (Join-Path $auditRoot "visual-reference-inventory.json")
    )
    if (Test-Path -LiteralPath $dosVisualReferenceRoot) {
        $inventoryArguments += @(
            "--dos-reference-directory", $dosVisualReferenceRoot
        )
    }
    python @inventoryArguments
    if ($LASTEXITCODE -ne 0) { throw "Visual-reference inventory has unaccounted captures." }

    $visualReferenceArguments = @(
        (Join-Path $PSScriptRoot "verify_visual_references.py"),
        $visualReferenceRoot,
        (Join-Path $projectRoot "work/qa/mac"),
        "--json-output",
        (Join-Path $auditRoot "visual-reference-verification.json")
    )
    if (Test-Path -LiteralPath $dosVisualReferenceRoot) {
        $visualReferenceArguments += @(
            "--dos-reference-directory", $dosVisualReferenceRoot,
            "--dos-qa-directory", (Join-Path $projectRoot "work/qa/dos")
        )
    }
    python @visualReferenceArguments
    if ($LASTEXITCODE -ne 0) { throw "Independent visual-reference verification failed." }
}

Assert-NoMarioProcess -Context "before hidden Alt+Enter integration test"
& (Join-Path $PSScriptRoot "test_fullscreen.ps1") -Executable $executable
if ($LASTEXITCODE -ne 0) { throw "Hidden Alt+Enter integration test failed." }
Assert-NoMarioProcess -Context "after hidden Alt+Enter integration test"

python (Join-Path $PSScriptRoot "verify_release.py") $executable `
    --asset-pack (Join-Path $projectRoot "assets/MarioFundamentals.pack") `
    --asset-pack (Join-Path $projectRoot "assets/MarioGameGallery.pack") `
    --report (Join-Path $auditRoot "release-verification.json")
if ($LASTEXITCODE -ne 0) { throw "Static PE/dependency verification failed." }

$manifestPath = Join-Path $projectRoot "work/rip/manifest.json"
$packPath = Join-Path $projectRoot "assets/MarioFundamentals.pack"
if (Test-Path -LiteralPath $manifestPath) {
    $preservationArguments = @(
        (Join-Path $PSScriptRoot "verify_preservation.py"),
        $manifestPath,
        $packPath,
        $executable,
        "--report",
        (Join-Path $auditRoot "preservation-verification.json")
    )
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.source.path -and (Test-Path -LiteralPath $manifest.source.path)) {
        $preservationArguments += @("--image", $manifest.source.path)
    }
    python @preservationArguments
    if ($LASTEXITCODE -ne 0) { throw "Byte-exact preservation verification failed." }
}

$dosManifestPath = Join-Path $projectRoot "work/audit/dos-resource-manifest.json"
$dosResourceRoot = Join-Path $projectRoot "work/rip/dos/resources"
$dosPackPath = Join-Path $projectRoot "assets/MarioGameGallery.pack"
if (Test-Path -LiteralPath $dosManifestPath) {
    if (-not (Test-Path -LiteralPath $dosResourceRoot)) {
        throw "DOS preservation manifest is present but its extracted resource directory is missing."
    }
    $dosArguments = @(
        (Join-Path $PSScriptRoot "verify_dos_preservation.py"),
        $dosManifestPath,
        $dosResourceRoot,
        $dosPackPath,
        $executable,
        "--report",
        (Join-Path $auditRoot "dos-preservation-verification.json")
    )
    $dosManifest = Get-Content -LiteralPath $dosManifestPath -Raw | ConvertFrom-Json
    $dosPrd = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $dosManifest.source.prd.path))
    $dosPrs = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $dosManifest.source.prs.path))
    if ((Test-Path -LiteralPath $dosPrd) -and (Test-Path -LiteralPath $dosPrs)) {
        $dosArguments += @("--prd", $dosPrd, "--prs", $dosPrs)
    }
    python @dosArguments
    if ($LASTEXITCODE -ne 0) { throw "DOS byte-exact preservation verification failed." }
}

$isolationRoot = Join-Path (Join-Path $projectRoot "work") ("release-isolation-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $isolationRoot | Out-Null
try {
    $isolationOutput = Join-Path $isolationRoot "self-test.out"
    $isolationError = Join-Path $isolationRoot "self-test.err"
    Invoke-HiddenExecutableCheck -FilePath $executable -Arguments "--self-test" `
        -WorkingDirectory $isolationRoot -StandardOutput $isolationOutput `
        -StandardError $isolationError -Label "Empty-directory executable self-test"
}
finally {
    Remove-Item -LiteralPath $isolationOutput -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $isolationError -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $isolationRoot -ErrorAction SilentlyContinue
}

# A second, independent configure/build proves that the release artifact is
# reproducible from the current source tree rather than merely stable across
# incremental links. Keep the generated tree under work/ and validate the
# resolved cleanup target before removing it.
$workRoot = [System.IO.Path]::GetFullPath((Join-Path $projectRoot "work"))
$reproductionRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $workRoot ("release-reproduction-" + [guid]::NewGuid().ToString("N"))))
$workPrefix = $workRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar) +
              [System.IO.Path]::DirectorySeparatorChar
if (-not $reproductionRoot.StartsWith(
        $workPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Reproduction build path escaped the workspace work directory."
}
try {
    cmake -S $projectRoot -B $reproductionRoot -G $CMakeGenerator -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { throw "Independent release configuration failed." }
    cmake --build $reproductionRoot --parallel
    if ($LASTEXITCODE -ne 0) { throw "Independent release build failed." }

    $primaryHash = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
    $reproducedExecutable = Join-Path $reproductionRoot "MarioFundamentals.exe"
    $reproducedHash = (Get-FileHash -LiteralPath $reproducedExecutable -Algorithm SHA256).Hash
    if ($primaryHash -ne $reproducedHash) {
        throw "Independent clean build hash $reproducedHash does not match $primaryHash."
    }
    Write-Output "PASS reproducible_clean_build_sha256=$primaryHash"
}
finally {
    if (Test-Path -LiteralPath $reproductionRoot) {
        Remove-Item -LiteralPath $reproductionRoot -Recurse -Force
    }
}

New-Item -ItemType Directory -Force -Path $distributionRoot | Out-Null
$releaseExecutable = Join-Path $distributionRoot "MarioFundamentals.exe"
Copy-Item -LiteralPath $executable -Destination $releaseExecutable -Force
$hash = Get-FileHash -LiteralPath $releaseExecutable -Algorithm SHA256
$checksumPath = Join-Path $distributionRoot "SHA256SUMS.txt"
[System.IO.File]::WriteAllText($checksumPath, "$($hash.Hash)  MarioFundamentals.exe`n")
[pscustomobject]@{
    Path = $releaseExecutable
    Bytes = (Get-Item -LiteralPath $releaseExecutable).Length
    SHA256 = $hash.Hash
}
