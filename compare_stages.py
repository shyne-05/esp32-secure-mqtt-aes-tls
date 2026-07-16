import os
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import requests

# Set design aesthetics for matplotlib
plt.style.use('seaborn-v0_8-whitegrid' if 'seaborn-v0_8-whitegrid' in plt.style.available else 'default')
plt.rcParams['font.family'] = 'Arial'
plt.rcParams['font.size'] = 10
plt.rcParams['axes.edgecolor'] = '#cccccc'
plt.rcParams['axes.linewidth'] = 0.8

stages = {
    1: {"name": "Giai doan 1: Baseline", "color": "#4a90e2", "file": "log_stage1.csv"},
    2: {"name": "Giai doan 2: AES-GCM", "color": "#f5a623", "file": "log_stage2.csv"},
    3: {"name": "Giai doan 3: TLS 1.3", "color": "#7ed321", "file": "log_stage3.csv"},
    4: {"name": "Giai doan 4: AES+TLS", "color": "#d0021b", "file": "log_stage4.csv"}
}

def load_data():
    data = {}
    for stage_id, info in stages.items():
        filename = info["file"]
        if os.path.exists(filename):
            try:
                df = pd.read_csv(filename)
                if not df.empty:
                    data[stage_id] = df
                    print(f"[OK] Loaded {len(df)} records from {filename}")
                else:
                    print(f"[!] File {filename} is empty.")
            except Exception as e:
                print(f"[ERROR] Failed to read {filename}: {e}")
        else:
            print(f"[-] File {filename} does not exist yet. Please run auto_listenerv2.py for this stage.")
    return data

def analyze():
    data = load_data()
    if not data:
        print("\n[!] No log files found. Please run the stages to collect data first.")
        return

    summary_rows = []
    
    # Calculate baseline free heap (Stage 1 average) if available
    baseline_free_heap = None
    if 1 in data:
        baseline_free_heap = data[1]["heap"].mean()
        print(f"Baseline (Stage 1) Average Free Heap: {baseline_free_heap:.1f} bytes")

    # Metrics to calculate mean, median, max
    metrics = ["raw_payload_size", "heap", "enc_us", "tls_ms", "cpu_pct"]
    
    stats_summary = {}

    for stage_id, df in data.items():
        stage_name = stages[stage_id]["name"]
        
        row = {
            "Stage": stage_name,
            "Records": len(df)
        }
        
        stats_summary[stage_id] = {}

        for m in metrics:
            if m in df.columns:
                mean_val = df[m].mean()
                median_val = df[m].median()
                max_val = df[m].max()
                
                row[f"{m}_mean"] = mean_val
                row[f"{m}_median"] = median_val
                row[f"{m}_max"] = max_val
                
                stats_summary[stage_id][m] = {
                    "mean": mean_val,
                    "median": median_val,
                    "max": max_val
                }
            else:
                row[f"{m}_mean"] = 0.0
                row[f"{m}_median"] = 0.0
                row[f"{m}_max"] = 0.0
                stats_summary[stage_id][m] = {"mean": 0.0, "median": 0.0, "max": 0.0}

        # Calculate RAM overhead (heap consumption delta compared to Stage 1)
        if baseline_free_heap is not None and "heap" in df.columns:
            avg_free_heap = df["heap"].mean()
            ram_overhead = max(0.0, baseline_free_heap - avg_free_heap)
            row["ram_overhead_avg_bytes"] = ram_overhead
            stats_summary[stage_id]["ram_overhead"] = ram_overhead
        else:
            row["ram_overhead_avg_bytes"] = 0.0
            stats_summary[stage_id]["ram_overhead"] = 0.0

        summary_rows.append(row)

    # Save to CSV
    summary_df = pd.DataFrame(summary_rows)
    summary_df.to_csv("comparison_summary.csv", index=False)
    print("\n[OK] Saved comparison summary to comparison_summary.csv")

    # Print Summary Table
    print("\n" + "="*80)
    print(f"{'STAGE COMPARISON SUMMARY (MEAN VALUES)':^80}")
    print("="*80)
    print(f"{'Stage':<20} | {'Msg Size':<10} | {'Enc (us)':<10} | {'TLS (ms)':<10} | {'CPU %':<8} | {'Free Heap (B)':<13} | {'RAM Overhead (B)':<15}")
    print("-"*120)
    for stage_id, info in stages.items():
        if stage_id in stats_summary:
            s = stats_summary[stage_id]
            name = info["name"]
            size = s["raw_payload_size"]["mean"]
            enc = s["enc_us"]["mean"]
            tls = s["tls_ms"]["mean"]
            cpu = s["cpu_pct"]["mean"]
            heap = s["heap"]["mean"]
            ram_ovr = s["ram_overhead"]
            print(f"{name:<20} | {size:>8.1f} B | {enc:>8.1f} us | {tls:>8.1f} ms | {cpu:>6.3f}% | {heap:>11.1f} B | {ram_ovr:>13.1f} B")
    print("="*80 + "\n")

    # Plot Bar Charts
    fig, axes = plt.subplots(3, 2, figsize=(14, 15))
    fig.suptitle("So sanh Hieu nang Bao mat MQTT tren ESP32-S3", fontsize=16, fontweight='bold', color='#333333')

    # 1. Payload Size Comparison
    ax = axes[0, 0]
    labels = [stages[sid]["name"] for sid in data.keys()]
    sizes = [stats_summary[sid]["raw_payload_size"]["mean"] for sid in data.keys()]
    colors = [stages[sid]["color"] for sid in data.keys()]
    bars = ax.bar(labels, sizes, color=colors, edgecolor='#555555', width=0.5)
    ax.set_title("Kich thuoc Payload trung binh (Bytes)", fontweight='bold')
    ax.set_ylabel("Kich thuoc (Bytes)")
    for bar in bars:
        yval = bar.get_height()
        ax.text(bar.get_x() + bar.get_width()/2, yval + 1, f"{yval:.1f} B", ha='center', va='bottom', fontsize=9, fontweight='bold')

    # 2. CPU Usage Comparison
    ax = axes[0, 1]
    cpus = [stats_summary[sid]["cpu_pct"]["mean"] for sid in data.keys()]
    bars = ax.bar(labels, cpus, color=colors, edgecolor='#555555', width=0.5)
    ax.set_title("Tai CPU trung binh (% Chu ky tich cuc)", fontweight='bold')
    ax.set_ylabel("Tai CPU (%)")
    for bar in bars:
        yval = bar.get_height()
        ax.text(bar.get_x() + bar.get_width()/2, yval + 0.005, f"{yval:.3f}%", ha='center', va='bottom', fontsize=9, fontweight='bold')

    # 3. Payload Encryption Time
    ax = axes[1, 0]
    enc_stages = [sid for sid in data.keys() if sid in (2, 4)]
    enc_labels = [stages[sid]["name"] for sid in enc_stages]
    enc_times = [stats_summary[sid]["enc_us"]["mean"] for sid in enc_stages]
    enc_colors = [stages[sid]["color"] for sid in enc_stages]
    if enc_times:
        bars = ax.bar(enc_labels, enc_times, color=enc_colors, edgecolor='#555555', width=0.4)
        for bar in bars:
            yval = bar.get_height()
            ax.text(bar.get_x() + bar.get_width()/2, yval + 5, f"{yval:.1f} us", ha='center', va='bottom', fontsize=9, fontweight='bold')
    else:
        ax.text(0.5, 0.5, "Khong co du lieu AES-GCM", ha='center', va='center')
    ax.set_title("Thoi gian ma hoa Payload trung binh (us)", fontweight='bold')
    ax.set_ylabel("Thoi gian (us)")

    # 4. TLS Handshake Time
    ax = axes[1, 1]
    tls_stages = [sid for sid in data.keys() if sid in (3, 4)]
    tls_labels = [stages[sid]["name"] for sid in tls_stages]
    tls_times = [stats_summary[sid]["tls_ms"]["mean"] for sid in tls_stages]
    tls_colors = [stages[sid]["color"] for sid in tls_stages]
    if tls_times:
        bars = ax.bar(tls_labels, tls_times, color=tls_colors, edgecolor='#555555', width=0.4)
        for bar in bars:
            yval = bar.get_height()
            ax.text(bar.get_x() + bar.get_width()/2, yval + 5, f"{yval:.1f} ms", ha='center', va='bottom', fontsize=9, fontweight='bold')
    else:
        ax.text(0.5, 0.5, "Khong co du lieu TLS", ha='center', va='center')
    ax.set_title("Thoi gian bat tay TLS trung binh (ms)", fontweight='bold')
    ax.set_ylabel("Thoi gian (ms)")

    # 5. Free Heap Comparison
    ax = axes[2, 0]
    heaps = [stats_summary[sid]["heap"]["mean"] / 1024 for sid in data.keys()]
    bars = ax.bar(labels, heaps, color=colors, edgecolor='#555555', width=0.5)
    ax.set_title("Bo nho Heap con trong trung binh (KB)", fontweight='bold')
    ax.set_ylabel("Dung luong (KB)")
    for bar in bars:
        yval = bar.get_height()
        ax.text(bar.get_x() + bar.get_width()/2, yval + 1, f"{yval:.1f} KB", ha='center', va='bottom', fontsize=9, fontweight='bold')

    # 6. RAM Consumption Overhead
    ax = axes[2, 1]
    ovrs = [stats_summary[sid]["ram_overhead"] / 1024 for sid in data.keys()]
    bars = ax.bar(labels, ovrs, color=colors, edgecolor='#555555', width=0.5)
    ax.set_title("RAM Overhead (Bo nho chiem dung so voi Baseline, KB)", fontweight='bold')
    ax.set_ylabel("Chiem dung (KB)")
    for bar in bars:
        yval = bar.get_height()
        ax.text(bar.get_x() + bar.get_width()/2, yval + 0.5, f"{yval:.2f} KB", ha='center', va='bottom', fontsize=9, fontweight='bold')

    plt.tight_layout(rect=[0, 0, 1, 0.96])
    plt.savefig("comparison_charts.png", dpi=300)
    print("[OK] Saved comparison charts to comparison_charts.png")
    plt.close()

    # Send photo to Telegram
    caption = (
        "📊 <b>BIEU DO SO SANH HIEU NANG CAC GIAI DOAN</b>\n"
        "- Do an: Application of AES-256 and TLS 1.3 on ESP32-S3\n"
        "- Cap nhat tu dong tu ket qua thuc te."
    )
    send_telegram_photo("comparison_charts.png", caption)

def send_telegram_photo(photo_path: str, caption: str):
    token = "8651731453:AAGaiFXEXeH45CDuM0FYSKjds4JizCqcaEg"
    chat_id = "7207855274"
    url = f"https://api.telegram.org/bot{token}/sendPhoto"
    try:
        with open(photo_path, 'rb') as photo:
            response = requests.post(
                url,
                data={"chat_id": chat_id, "caption": caption, "parse_mode": "HTML"},
                files={"photo": photo},
                timeout=10
            )
            if response.status_code == 200:
                print("[OK] Da gui anh bieu do so sanh den Telegram!")
            else:
                print(f"[!] Gui anh bieu do that bai, status={response.status_code}, body={response.text}")
    except Exception as e:
        print(f"[!] Loi khi gui anh qua Telegram: {e}")

if __name__ == "__main__":
    analyze()
