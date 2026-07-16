$ErrorActionPreference = 'Stop'
$root='C:\doan2'
$env:IDF_TOOLS_PATH='C:\Espressif'
. 'C:\Espressif\v5.4\esp-idf\export.ps1' | Out-Null
if (Test-Path "$root\log_stage1.csv") { Remove-Item "$root\log_stage1.csv" -Force }
Set-Location "$root\stage1_baseline"
idf.py -p COM6 flash
Start-Sleep -Seconds 8
$p=Start-Process -FilePath 'C:\Users\danga\AppData\Local\Programs\Python\Python312\python.exe' -ArgumentList @('-u',"$root\auto_listenerv2.py",'1') -WorkingDirectory $root -RedirectStandardOutput "$root\listener_stage1.log" -RedirectStandardError "$root\listener_stage1.err.log" -PassThru
Start-Sleep -Seconds 65
if (!$p.HasExited) { Stop-Process -Id $p.Id -Force }
