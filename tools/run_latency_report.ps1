$ErrorActionPreference = "Stop"

# Run from the repository regardless of the caller's working directory.
$repoRoot = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repoRoot "build\release\Release\order_book_latency.exe"
$plotScript = Join-Path $repoRoot "tools\plot_latency.py"

if (-not (Test-Path $exe)) {
    throw "Latency executable not found: $exe. Build with: cmake --build --preset release"
}

Push-Location $repoRoot
try {
    & $exe
    if ($LASTEXITCODE -ne 0) {
        throw "Latency executable failed with exit code $LASTEXITCODE"
    }

    # Uses the newest report created by this run.
    py -3 $plotScript
    if ($LASTEXITCODE -ne 0) {
        throw "Latency plotting failed. Install matplotlib with: py -m pip install matplotlib"
    }
}
finally {
    Pop-Location
}
