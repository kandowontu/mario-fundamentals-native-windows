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

$auditRoot = Join-Path $projectRoot "work/audit"
New-Item -ItemType Directory -Force -Path $auditRoot | Out-Null
python (Join-Path $PSScriptRoot "verify_release.py") $executable `
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
