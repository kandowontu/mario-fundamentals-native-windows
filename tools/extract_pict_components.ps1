param(
    [Parameter(Mandatory = $true)] [string] $Deark,
    [string] $ResourceDirectory = "work/rip/resources/PICT",
    [string] $OutputDirectory = "work/converted/pict-components"
)

$dearkExecutable = (Resolve-Path -LiteralPath $Deark).Path
$resourceRoot = (Resolve-Path -LiteralPath $ResourceDirectory).Path
$outputRoot = [System.IO.Path]::GetFullPath($OutputDirectory)
$results = @()
foreach ($id in 400..409) {
    $source = Join-Path $resourceRoot ("+{0:D5}.bin" -f $id)
    $destination = Join-Path $outputRoot ("{0:D5}" -f $id)
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    & $dearkExecutable -q -m pict -od $destination $source
    if ($LASTEXITCODE -ne 0) { throw "Deark failed on PICT $id." }
    $results += [pscustomobject]@{
        Id = $id
        Components = (Get-ChildItem -LiteralPath $destination -File).Count
    }
}
$results
