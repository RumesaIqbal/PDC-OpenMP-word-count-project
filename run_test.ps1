param(
    [string]$InputFile = "test.txt",
    [int]$Threads = 4,
    [ValidateSet("static", "dynamic")]
    [string]$Schedule = "static",
    [ValidateSet("critical", "tree")]
    [string]$Merge = "critical"
)

$ErrorActionPreference = "Stop"
$projectRoot = $PSScriptRoot
$inputPath = if ([System.IO.Path]::IsPathRooted($InputFile)) {
    $InputFile
} else {
    Join-Path $projectRoot $InputFile
}

if (-not (Test-Path -LiteralPath $inputPath -PathType Leaf)) {
    throw "Input file not found: $inputPath`nPut test.txt in the project folder or pass a file path."
}

$compiler = Get-Command gcc -ErrorAction SilentlyContinue
if (-not $compiler) {
    throw "gcc was not found. Install/configure MSYS2 UCRT64 and add C:\msys64\ucrt64\bin to PATH."
}

$buildDir = Join-Path $projectRoot "build"
$resultsDir = Join-Path $projectRoot "results"
New-Item -ItemType Directory -Force -Path $buildDir, $resultsDir | Out-Null

$commonFlags = @("-O2", "-std=c11", "-fopenmp", "-Wall", "-Wextra", ("-I" + (Join-Path $projectRoot "include")))
$targets = @(
    @{ Name = "wc_seq"; Sources = @("src/seq_main.c", "src/wc.c") },
    @{ Name = "wc_par"; Sources = @("src/par_main.c", "src/wc.c") },
    @{ Name = "wc_verify"; Sources = @("src/verify_main.c") }
)

foreach ($target in $targets) {
    $binaryPath = Join-Path $buildDir ($target.Name + ".exe")
    Write-Host "Building $($target.Name).exe..."
    $sourcePaths = @($target.Sources | ForEach-Object { Join-Path $projectRoot $_ })
    & $compiler.Source @commonFlags @sourcePaths "-o" $binaryPath "-lpsapi"
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed for $($target.Name) (exit code $LASTEXITCODE)."
    }
}

$baseName = [System.IO.Path]::GetFileNameWithoutExtension($inputPath)
$seqOutput = Join-Path $resultsDir ($baseName + "_seq.tsv")
$seqStats = Join-Path $resultsDir ($baseName + "_seq_stats.json")
$parOutput = Join-Path $resultsDir ($baseName + "_par.tsv")
$parStats = Join-Path $resultsDir ($baseName + "_par_stats.json")

Write-Host "Running sequential count..."
& (Join-Path $buildDir "wc_seq.exe") $inputPath --out $seqOutput --stats $seqStats --top 10
if ($LASTEXITCODE -ne 0) { throw "Sequential count failed (exit code $LASTEXITCODE)." }

Write-Host "Running parallel count ($Threads threads, $Schedule/$Merge)..."
& (Join-Path $buildDir "wc_par.exe") $inputPath --threads $Threads --schedule $Schedule --merge $Merge --out $parOutput --stats $parStats --top 10
if ($LASTEXITCODE -ne 0) { throw "Parallel count failed (exit code $LASTEXITCODE)." }

Write-Host "Verifying complete count tables..."
& (Join-Path $buildDir "wc_verify.exe") $seqOutput $parOutput
if ($LASTEXITCODE -ne 0) { throw "Verification failed (exit code $LASTEXITCODE)." }

$seqSummary = Get-Content -LiteralPath $seqStats -Raw | ConvertFrom-Json
$parSummary = Get-Content -LiteralPath $parStats -Raw | ConvertFrom-Json
$seqSeconds = [double]$seqSummary.t_total
$parSeconds = [double]$parSummary.t_total

Write-Host ""
Write-Host "Timing comparison (counting time; excludes compilation and output-file writing):"
Write-Host ("Sequential: {0:F6} s ({1:F3} ms)" -f $seqSeconds, ($seqSeconds * 1000))
Write-Host ("Parallel:   {0:F6} s ({1:F3} ms)" -f $parSeconds, ($parSeconds * 1000))
if ($seqSeconds -gt 0 -and $parSeconds -gt 0) {
    if ($parSeconds -lt $seqSeconds) {
        Write-Host ("Parallel speedup: {0:F2}x" -f ($seqSeconds / $parSeconds))
    } elseif ($parSeconds -gt $seqSeconds) {
        Write-Host ("Parallel slowdown: {0:F2}x" -f ($parSeconds / $seqSeconds))
    } else {
        Write-Host "Sequential and parallel times are equal."
    }
}

Write-Host "Done. Full tables and stats are in $resultsDir"
