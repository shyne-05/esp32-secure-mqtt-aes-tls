$env:IDF_TOOLS_PATH="C:\Espressif"

while ($true) {
    Clear-Host
    Write-Host "=========================================================="
    Write-Host "         TRINH NAP FIRMWARE ESP32-S3 TU DONG"
    Write-Host "=========================================================="
    Write-Host " 1. Nap Stage 1: Baseline (Plaintext JSON, Cong 1883)"
    Write-Host " 2. Nap Stage 2: AES-GCM (Ma hoa payload, Cong 1883)"
    Write-Host " 3. Nap Stage 3: TLS 1.3 (Kenh truyen TLS, Cong 8883)"
    Write-Host " 4. Nap Stage 4: AES-GCM + TLS 1.3 (Bao mat 2 lop, Cong 8883)"
    Write-Host " 5. Thoat"
    Write-Host "=========================================================="
    
    $choice = Read-Host "Nhap lua chon cua ban (1-5)"
    
    if ($choice -eq "5") {
        break
    }
    
    $stageFolder = ""
    if ($choice -eq "1") {
        $stageFolder = "stage1_baseline"
    } elseif ($choice -eq "2") {
        $stageFolder = "stage2_aesgcm"
    } elseif ($choice -eq "3") {
        $stageFolder = "stage3_tls"
    } elseif ($choice -eq "4") {
        $stageFolder = "stage4_aesgcm_tls"
    } else {
        Write-Host "Lua chon khong hop le!" -ForegroundColor Red
        Start-Sleep -Seconds 2
        continue
    }
    
    Write-Host "`n[1/3] Kich hoat moi truong ESP-IDF..." -ForegroundColor Cyan
    Set-ExecutionPolicy -ExecutionPolicy Bypass -Scope Process -Force
    . C:\Espressif\v5.4\esp-idf\export.ps1
    
    Write-Host "[2/3] Di chuyen toi thu muc c:\doan2\$stageFolder..." -ForegroundColor Cyan
    cd c:\doan2\$stageFolder
    
    Write-Host "[3/3] Dang nap firmware vao ESP32-S3 (COM6)..." -ForegroundColor Cyan
    idf.py -p COM6 flash
    
    Write-Host "`n[HOAN THANH] Da nap code xong cho $stageFolder!" -ForegroundColor Green
    Read-Host "Nhan Enter de tiep tuc..."
}
