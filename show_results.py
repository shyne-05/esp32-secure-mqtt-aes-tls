import os
import sys
import pandas as pd

SUMMARY_CSV = r"c:\doan2\comparison_summary.csv"
CHARTS_PNG = r"c:\doan2\comparison_charts.png"

def show_presentation_data():
    print("=" * 85)
    print(f"{'BAO CAO KET QUA DO DAC HIEU NANG - DO AN TOT NGHIEP':^85}")
    print("=" * 85)
    
    if not os.path.exists(SUMMARY_CSV):
        print(f"[LOI] Khong tim thay file du lieu so sanh tai {SUMMARY_CSV}")
        print("Vui long chay file compare_stages.py truoc de tao du lieu.")
        return
        
    try:
        df = pd.read_csv(SUMMARY_CSV)
        print(f"{'Giai doan':<22} | {'Msg Size':<10} | {'Ma hoa (us)':<12} | {'TLS (ms)':<10} | {'Tai CPU %':<10} | {'RAM du (B)':<12}")
        print("-" * 85)
        
        for _, row in df.iterrows():
            stage = row["Stage"]
            # Map metrics safely
            size = f"{row.get('raw_payload_size_mean', 0.0):.1f} B"
            enc = f"{row.get('enc_us_mean', 0.0):.1f} us"
            tls = f"{row.get('tls_ms_mean', 0.0):.1f} ms"
            cpu = f"{row.get('cpu_pct_mean', 0.0):.3f}%"
            # Calculate average free heap or map it
            heap = f"{row.get('heap_mean', 0.0):,.0f} B"
            
            print(f"{stage:<22} | {size:>10} | {enc:>12} | {tls:>10} | {cpu:>10} | {heap:>12}")
            
        print("=" * 85)
        print("\n* NHAN XET DANH GIA NHANH:")
        print("  1. Ma hoa AES-256-GCM phan cung cuc ky nhanh (~500 us), tang tai CPU khong dang ke (~0.1%).")
        print("  2. Kenh bao mat TLS 1.3 ton ~900 ms de bat tay (chi thuc hien 1 lan luc ket noi), sau do chay muot ma.")
        print("  3. Su dung TLS 1.3 tieu ton ~36.7 KB RAM, ESP32-S3 van du ~227 KB RAM dam bao he thong hoat dong on dinh.")
        
    except Exception as e:
        print(f"Loi doc file bao cao: {e}")

    # Automatically open the comparison chart image
    if os.path.exists(CHARTS_PNG):
        print(f"\n[OK] Dang tu dong mo bieu do so sanh: {CHARTS_PNG}")
        try:
            os.startfile(CHARTS_PNG)
        except Exception as e:
            print(f"Khong the tu dong mo anh bieu do: {e}")
    else:
        print(f"\n[!] Khong tim thay anh bieu do tai {CHARTS_PNG}. Hay chay compare_stages.py de tao.")

if __name__ == "__main__":
    show_presentation_data()
    # Read input safely depending on interactive environment
    try:
        input("\nNhan Enter de dong bao cao...")
    except Exception:
        pass
