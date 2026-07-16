$ErrorActionPreference = 'Stop'
$env:IDF_TOOLS_PATH = 'C:\Espressif'
. 'C:\Espressif\v5.4\esp-idf\export.ps1' | Out-Null
$stages = @('stage1_baseline','stage2_aesgcm','stage3_tls','stage4_aesgcm_tls')
foreach ($s in $stages) {
    Write-Host "=== FLASH $s ==="
    Set-Location (Join-Path 'C:\doan2' $s)
    idf.py -p COM6 flash
    if ($LASTEXITCODE -ne 0) { throw "flash failed: $s" }
}
