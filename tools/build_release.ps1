param(
    [string] $BuildDirectory = "build",
    [string] $DistributionDirectory = "dist"
)

$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDirectory))
$distributionRoot = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $DistributionDirectory))

cmake -S $projectRoot -B $buildRoot -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed." }
cmake --build $buildRoot --parallel
if ($LASTEXITCODE -ne 0) { throw "Native build failed." }

$executable = Join-Path $buildRoot "MarioFundamentals.exe"
$auditRoot = Join-Path $projectRoot "work/audit"
New-Item -ItemType Directory -Force -Path $auditRoot | Out-Null
$standardOutput = Join-Path $buildRoot "self-test.out"
$standardError = Join-Path $buildRoot "self-test.err"
$test = Start-Process -FilePath $executable -ArgumentList "--self-test" -WindowStyle Hidden `
    -RedirectStandardOutput $standardOutput -RedirectStandardError $standardError -Wait -PassThru
Get-Content $standardOutput, $standardError
if ($test.ExitCode -ne 0) { throw "Executable self-test failed with exit code $($test.ExitCode)." }

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
    $qa = Start-Process -FilePath $executable -ArgumentList $Argument -WindowStyle Hidden `
        -WorkingDirectory $projectRoot -RedirectStandardOutput $qaOutput `
        -RedirectStandardError $qaError -Wait -PassThru
    Get-Content $qaOutput, $qaError
    if ($qa.ExitCode -ne 0) {
        throw "$Label presentation QA failed with exit code $($qa.ExitCode)."
    }
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
    -ExpectedFrames 200 -ExpectedBytes 786486
Invoke-PresentationQa -Argument "--render-dos-qa" `
    -OutputDirectory (Join-Path $projectRoot "work/qa/dos") -Label "dos" `
    -ExpectedFrames 210 -ExpectedBytes 256054

& (Join-Path $PSScriptRoot "test_fullscreen.ps1") -Executable $executable
if ($LASTEXITCODE -ne 0) { throw "Hidden Alt+Enter integration test failed." }

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
    $isolationTest = Start-Process -FilePath $executable -ArgumentList "--self-test" `
        -WorkingDirectory $isolationRoot -WindowStyle Hidden -RedirectStandardOutput $isolationOutput `
        -RedirectStandardError $isolationError -Wait -PassThru
    Get-Content $isolationOutput, $isolationError
    if ($isolationTest.ExitCode -ne 0) {
        throw "Empty-directory executable self-test failed with exit code $($isolationTest.ExitCode)."
    }
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
    cmake -S $projectRoot -B $reproductionRoot -G Ninja -DCMAKE_BUILD_TYPE=Release
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
