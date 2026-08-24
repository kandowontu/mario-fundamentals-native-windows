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
$standardOutput = Join-Path $buildRoot "self-test.out"
$standardError = Join-Path $buildRoot "self-test.err"
$test = Start-Process -FilePath $executable -ArgumentList "--self-test" -WindowStyle Hidden `
    -RedirectStandardOutput $standardOutput -RedirectStandardError $standardError -Wait -PassThru
Get-Content $standardOutput, $standardError
if ($test.ExitCode -ne 0) { throw "Executable self-test failed with exit code $($test.ExitCode)." }

& (Join-Path $PSScriptRoot "test_fullscreen.ps1") -Executable $executable
if ($LASTEXITCODE -ne 0) { throw "Hidden Alt+Enter integration test failed." }

$auditRoot = Join-Path $projectRoot "work/audit"
New-Item -ItemType Directory -Force -Path $auditRoot | Out-Null
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
