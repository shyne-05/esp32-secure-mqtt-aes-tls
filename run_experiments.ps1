$ErrorActionPreference = 'Stop'
$root = 'C:\doan2'
$pythonExe = 'C:\Users\danga\AppData\Local\Programs\Python\Python312\python.exe'
$env:IDF_TOOLS_PATH = 'C:\Espressif'
. 'C:\Espressif\v5.4\esp-idf\export.ps1' | Out-Null

# Preserve the previous run before collecting a fresh, consistent set.
$backup = Join-Path $root ('previous_run_' + (Get-Date -Format 'yyyyMMdd_HHmmss'))
New-Item -ItemType Directory -Path $backup | Out-Null
1..4 | ForEach-Object {
    $csv = Join-Path $root ("log_stage$_.csv")
    if (Test-Path $csv) { Copy-Item $csv $backup }
}

$projects = @('stage1_baseline','stage2_aesgcm','stage3_tls','stage4_aesgcm_tls')
foreach ($i in 1..4) {
    $project = $projects[$i-1]
    $csv = Join-Path $root ("log_stage$i.csv")
    if (Test-Path $csv) { Remove-Item -LiteralPath $csv -Force }
    Write-Host "=== FLASH AND COLLECT STAGE $i ==="
    Set-Location (Join-Path $root $project)
    idf.py -p COM6 flash
    if ($LASTEXITCODE -ne 0) { throw "flash failed: $project" }
    Start-Sleep -Seconds 5
    $out = Join-Path $root ("listener_stage$i.log")
    $err = Join-Path $root ("listener_stage$i.err.log")
    $proc = Start-Process -FilePath $pythonExe -ArgumentList @('-u', (Join-Path $root 'auto_listenerv2.py'), "$i") -WorkingDirectory $root -RedirectStandardOutput $out -RedirectStandardError $err -PassThru
    Start-Sleep -Seconds 36
    if (!$proc.HasExited) { Stop-Process -Id $proc.Id -Force }
    Write-Host "Collected stage $i; listener log: $out"
}
