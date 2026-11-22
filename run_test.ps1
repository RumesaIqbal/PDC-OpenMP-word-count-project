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

$compiler = Get-Command g++ -ErrorAction SilentlyContinue
if (-not $compiler) {
    throw "g++ was not found. Install/configure MSYS2 UCRT64 and add C:\msys64\ucrt64\bin to PATH."
}

$buildDir = Join-Path $projectRoot "build"
$resultsDir = Join-Path $projectRoot "results"
New-Item -ItemType Directory -Force -Path $buildDir, $resultsDir | Out-Null

$commonFlags = @("-O2", "-std=c++17", "-fopenmp", "-Wall", "-Wextra", ("-I" + (Join-Path $projectRoot "include")))
$targets = @(
    @{ Name = "wc_seq"; Source = "src/seq_main.cpp" },
    @{ Name = "wc_par"; Source = "src/par_main.cpp" },
    @{ Name = "wc_verify"; Source = "src/verify_main.cpp" }
)

foreach ($target in $targets) {
    $binaryPath = Join-Path $buildDir ($target.Name + ".exe")
    Write-Host "Building $($target.Name).exe..."
    $sourcePath = Join-Path $projectRoot $target.Source
    & $compiler.Source @commonFlags $sourcePath "-o" $binaryPath "-lpsapi"
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

Write-Host "Done. Full tables and stats are in $resultsDir"
