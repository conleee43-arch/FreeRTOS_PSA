$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot
try {
    python tests/check_gui_runtime_behavior.py
    if ($LASTEXITCODE -ne 0) {
        throw "GUI runtime behavior check failed!"
    }
} finally {
    Pop-Location
}
