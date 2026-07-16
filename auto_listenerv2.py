"""
GIAI DOAN 1 / 2 / 3 / 4 - TU DONG KHOI DONG MOSQUITTO + LISTENER + BAO CAO TELEGRAM CHI TIET

Chay script nay, nhap 1/2/3/4, no se tu dong:
  1. Khoi dong Mosquitto Broker (neu chua chay)
  2. Subscribe dung topic MQTT cua giai doan da chon (co hoac khong TLS)
  3. Neu giai doan co ma hoa payload (2, 4): giai ma AES-256-GCM
  4. Gui bao cao CHI TIET tung buoc ve Telegram (tieng Viet) de giao vien
     xem duoc ro qua trinh: nhan goi tin -> (giai ma) -> du lieu cam bien
     -> so lieu hieu nang (heap, thoi gian ma hoa/giai ma, kich thuoc goi tin)

Nhan Ctrl+C de dung chuong trinh (se tu tat luon Mosquitto neu script nay khoi dong no).
"""

import base64
import json
import time
import socket
import subprocess
import atexit
import sys
import requests
import paho.mqtt.client as mqtt
from Crypto.Cipher import AES

# ===== DUONG DAN MOSQUITTO (SUA LAI NEU CAI O NOI KHAC) =====
MOSQUITTO_EXE = r"C:\Program Files\mosquitto\mosquitto.exe"
MOSQUITTO_CONF = r"C:\Program Files\mosquitto\mosquitto.conf"

# ===== CAU HINH MQTT (PHAI KHOP VOI CODE ESP32) =====
MQTT_BROKER = "172.20.10.6"
CA_CERT_PATH = r"C:\doan2\mosquitto-certs\ca.crt"

# ===== KEY AES-256 CO DINH (32 byte) - PHAI KHOP VOI CODE ESP32 =====
AES_KEY = bytes([
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F
])

NONCE_LEN = 12
TAG_LEN = 16

# ===== CAU HINH TELEGRAM BOT =====
TELEGRAM_BOT_TOKEN = "8651731453:AAGaiFXEXeH45CDuM0FYSKjds4JizCqcaEg"
TELEGRAM_CHAT_ID = "7207855274"
TELEGRAM_API_URL = f"https://api.telegram.org/bot{TELEGRAM_BOT_TOKEN}/sendMessage"

# ===== CAU HINH TUNG GIAI DOAN =====
# port        : cong MQTT (1883 = plain TCP, 8883 = TLS)
# topic       : phai khop dung voi mqtt_topic trong file .ino tuong ung
# use_tls     : co bat TLS cho ket noi MQTT khong
# encrypted   : payload co bi ma hoa AES-256-GCM khong (True = phai giai ma)
# label       : mo ta de hien thi trong log / Telegram
STAGE_CONFIG = {
    1: {
        "port": 1883,
        "topic": "dothi/sensor/baseline",
        "use_tls": False,
        "encrypted": False,
        "label": "Giai doan 1 - BASELINE (khong ma hoa, khong TLS)",
    },
    2: {
        "port": 1883,
        "topic": "dothi/sensor/aes256gcm",
        "use_tls": False,
        "encrypted": True,
        "label": "Giai doan 2 - AES-256-GCM (ma hoa payload, KHONG TLS)",
    },
    3: {
        "port": 8883,
        "topic": "dothi/sensor/tls13",
        "use_tls": True,
        "encrypted": False,
        "label": "Giai doan 3 - TLS (bao mat kenh truyen, KHONG ma hoa payload)",
    },
    4: {
        "port": 8883,
        "topic": "dothi/sensor/aesgcm_tls",
        "use_tls": True,
        "encrypted": True,
        "label": "Giai doan 4 - AES-256-GCM + TLS (2 lop bao mat)",
    },
}

mosquitto_process = None


def send_telegram_message(text: str):
    """Gui tin nhan toi Telegram bot. Loi khong lam crash chuong trinh chinh."""
    try:
        response = requests.post(
            TELEGRAM_API_URL,
            data={"chat_id": TELEGRAM_CHAT_ID, "text": text, "parse_mode": "HTML"},
            timeout=5
        )
        if response.status_code != 200:
            print(f"[LOI TELEGRAM] Gui that bai, status={response.status_code}, body={response.text}")
    except Exception as e:
        print(f"[LOI TELEGRAM] Khong the gui tin nhan: {e}")


def is_broker_running(host: str, port: int, timeout: float = 1.5) -> bool:
    """Kiem tra xem co dich vu nao dang lang nghe tai host:port khong."""
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def start_mosquitto():
    """Khoi dong Mosquitto Broker neu chua chay. Tra ve True neu tu khoi dong (can tat sau)."""
    global mosquitto_process

    if is_broker_running(MQTT_BROKER, 1883) or is_broker_running(MQTT_BROKER, 8883):
        print("[OK] Mosquitto Broker da dang chay san, khong can khoi dong lai.")
        return False

    print("[...] Mosquitto chua chay, dang tu khoi dong...")
    try:
        mosquitto_process = subprocess.Popen(
            [MOSQUITTO_EXE, "-c", MOSQUITTO_CONF, "-v"],
            creationflags=subprocess.CREATE_NEW_CONSOLE
        )
    except FileNotFoundError:
        print(f"[LOI] Khong tim thay mosquitto.exe tai: {MOSQUITTO_EXE}")
        print("      Kiem tra lai duong dan cai dat Mosquitto va sua bien MOSQUITTO_EXE trong script.")
        sys.exit(1)

    for _ in range(20):
        time.sleep(0.5)
        if is_broker_running(MQTT_BROKER, 1883) or is_broker_running(MQTT_BROKER, 8883):
            print("[OK] Mosquitto Broker da khoi dong thanh cong.")
            return True

    print("[LOI] Mosquitto khong khoi dong duoc trong 10 giay. Kiem tra lai file conf/cert.")
    sys.exit(1)


def stop_mosquitto():
    """Tat Mosquitto neu script nay la nguoi khoi dong no."""
    if mosquitto_process is not None:
        print("\n[...] Dang tat Mosquitto Broker...")
        mosquitto_process.terminate()
        try:
            mosquitto_process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            mosquitto_process.kill()
        print("[OK] Da tat Mosquitto Broker.")


def decrypt_gcm(base64_payload: str):
    """Giai ma goi tin: base64 -> [nonce(12) | ciphertext | tag(16)] -> plaintext"""
    raw = base64.b64decode(base64_payload)
    nonce = raw[:NONCE_LEN]
    tag = raw[-TAG_LEN:]
    ciphertext = raw[NONCE_LEN:-TAG_LEN]

    cipher = AES.new(AES_KEY, AES.MODE_GCM, nonce=nonce)
    plaintext = cipher.decrypt_and_verify(ciphertext, tag)
    return plaintext.decode("utf-8")


def on_connect(client, userdata, flags, rc):
    cfg = userdata["cfg"]
    topic = cfg["topic"]
    if rc == 0:
        print(f"[OK] Da ket noi MQTT broker {MQTT_BROKER}:{cfg['port']} (TLS={'Co' if cfg['use_tls'] else 'Khong'})")
        client.subscribe(topic)
        print(f"[OK] Dang lang nghe topic: {topic}\n")
        send_telegram_message(
            f"🟢 <b>He thong da ket noi</b>\n"
            f"📋 {cfg['label']}\n"
            f"🔌 Broker: <code>{MQTT_BROKER}:{cfg['port']}</code>\n"
            f"🔐 Kenh truyen TLS: {'Co' if cfg['use_tls'] else 'Khong'}\n"
            f"🔑 Ma hoa payload AES-256-GCM: {'Co' if cfg['encrypted'] else 'Khong'}\n"
            f"📡 Dang lang nghe topic: <code>{topic}</code>"
        )
    else:
        print(f"[LOI] Ket noi that bai, rc={rc}")
        send_telegram_message(
            f"🔴 <b>Ket noi MQTT that bai</b>\n"
            f"📋 {cfg['label']}\n"
            f"Ma loi rc={rc}"
        )


def on_message(client, userdata, msg):
    cfg = userdata["cfg"]
    stage = userdata["stage"]
    raw_payload = msg.payload.decode("utf-8")
    raw_size = len(raw_payload)

    steps = []  # List of processing steps
    steps.append(f"- Nhan tin tu topic [{msg.topic}], size: {raw_size} bytes.")

    try:
        decrypt_time_us = None

        if cfg["encrypted"]:
            steps.append("- Phat hien ma hoa AES-256-GCM. Dang tien hanh giai ma...")
            decrypt_start = time.perf_counter()
            plaintext = decrypt_gcm(raw_payload)
            decrypt_time_us = (time.perf_counter() - decrypt_start) * 1_000_000
            steps.append(f"- Giai ma thanh cong (GCM tag hop le). Thoi gian giai ma: {decrypt_time_us:.1f} us.")
        else:
            steps.append("- Payload dang plaintext (khong ma hoa). Doc truc tiep.")
            plaintext = raw_payload

        data = json.loads(plaintext)
        steps.append("- Parse chuoi JSON thanh cong, dang trich xuat du lieu...")

        print("----------------------------------------------------------------------")
        print(f"[{cfg['label']}]")
        print(f"Raw payload : {raw_payload[:85]}...")
        print(f"Plaintext   : {plaintext}")
        if decrypt_time_us is not None:
            print(f"Decrypt time: {decrypt_time_us:.1f} us")
        
        temp = data.get('temp')
        humid = data.get('humid')
        msg_id = data.get('id')
        ts = data.get('ts')
        heap = data.get('heap')
        enc_us = data.get('enc_us', 0)
        tls_ms = data.get('tls_ms', 0)
        cpu_pct = data.get('cpu_pct', 0.0)

        print(f"Parsed data : ID={msg_id} | Temp={temp}C | Humid={humid}% | CPU={cpu_pct}% | TLS={tls_ms}ms")

        # Save to CSV
        import os
        csv_filename = f"log_stage{stage}.csv"
        file_exists = os.path.exists(csv_filename)
        with open(csv_filename, "a", encoding="utf-8") as f:
            if not file_exists:
                f.write("timestamp,msg_id,temp,humid,raw_payload_size,heap,enc_us,tls_ms,cpu_pct\n")
            f.write(f"{time.strftime('%Y-%m-%d %H:%M:%S')},{msg_id},{temp},{humid},{raw_size},{heap},{enc_us},{tls_ms},{cpu_pct}\n")
        print(f"[OK] Da luu vao {csv_filename}")

        # Build professional Telegram message
        steps_text = "\n".join(steps)
        encryption_stats = ""
        if cfg["encrypted"]:
            encryption_stats = (
                f"- Thoi gian ma hoa (ESP32): {enc_us} us\n"
                f"- Thoi gian giai ma (Host): {decrypt_time_us:.1f} us\n"
            )
        tls_stats = ""
        if cfg["use_tls"]:
            tls_stats = (
                f"- Phien ban giao thuc: TLS 1.3\n"
                f"- Thoi gian bat tay TLS: {tls_ms} ms\n"
                f"- Bao mat kenh truyen: Da ma hoa duong truyen (MQTTS)\n"
            )
        else:
            tls_stats = (
                f"- Giao thuc truyen: MQTT thuong\n"
                f"- Kênh bao mat: Khong su dung TLS\n"
            )

        telegram_text = (
            f"<b>[BAO CAO THU THAP DU LIEU GOI TIN MQTT]</b>\n"
            f"==========================================\n"
            f"<b>GIAI DOAN:</b> {cfg['label']}\n"
            f"<b>TRANG THAI:</b> Da nhan va xu ly thanh cong!\n\n"
            f"<b>1. THONG TIN CHUNG:</b>\n"
            f"- Topic MQTT: <code>{msg.topic}</code>\n"
            f"- So thu tu (Msg ID): <code>{msg_id}</code>\n"
            f"- Kich thuoc goi tin tho: {raw_size} bytes\n"
            f"- Kich thuoc sau khi giai ma: {len(plaintext)} bytes\n\n"
            f"<b>2. DU LIEU CAM BIEN:</b>\n"
            f"- Nhiet do (Temperature): <b>{temp} C</b>\n"
            f"- Do am (Humidity): <b>{humid} %</b>\n"
            f"- Thoi gian chay he thong: {ts} ms\n\n"
            f"<b>3. CHI TIET HIEU NANG PHAN CUNG:</b>\n"
            f"- Heap RAM tu do: {heap:,} bytes (~{heap/1024:.1f} KB)\n"
            f"- Tai CPU (Active Duty): {cpu_pct:.3f} %\n"
            f"{encryption_stats}"
            f"{tls_stats}\n"
            f"<b>4. CAC BUOC XU LY TREN HE THONG:</b>\n"
            f"{steps_text}"
        )

        send_telegram_message(telegram_text)

    except Exception as e:
        print(f"[LOI] Xu ly that bai: {e}")
        print(f"  Payload nhan duoc: {raw_payload}")
        
        steps_text = "\n".join(steps)
        error_text = (
            f"<b>[LOI XU LY DU LIEU MQTT]</b>\n"
            f"==========================================\n"
            f"<b>GIAI DOAN:</b> {cfg['label']}\n"
            f"<b>CHI TIET LOI:</b> <code>{e}</code>\n\n"
            f"<b>Cac buoc da thuc hien:</b>\n"
            f"{steps_text}\n\n"
            f"<b>Payload nhan duoc:</b>\n"
            f"<code>{raw_payload}</code>"
        )
        send_telegram_message(error_text)


def choose_stage() -> int:
    while True:
        choice = input(
            "Chon giai doan can chay:\n"
            "  1 = Baseline (khong ma hoa, khong TLS)\n"
            "  2 = AES-256-GCM (khong TLS)\n"
            "  3 = TLS (khong ma hoa payload)\n"
            "  4 = AES-256-GCM + TLS\n"
            "Nhap 1/2/3/4: "
        ).strip()
        if choice in ("1", "2", "3", "4"):
            return int(choice)
        print("Vui long nhap 1, 2, 3 hoac 4.")


def main():
    import sys
    if len(sys.argv) > 1:
        try:
            stage = int(sys.argv[1])
            if stage not in (1, 2, 3, 4):
                raise ValueError()
        except ValueError:
            print("Loi: Giai doan phai la 1, 2, 3 hoac 4.")
            sys.exit(1)
    else:
        stage = choose_stage()
    cfg = STAGE_CONFIG[stage]

    self_started = start_mosquitto()
    if self_started:
        atexit.register(stop_mosquitto)

    client = mqtt.Client(userdata={"cfg": cfg, "stage": stage})
    client.on_connect = on_connect
    client.on_message = on_message

    if cfg["use_tls"]:
        client.tls_set(ca_certs=CA_CERT_PATH)
        client.tls_insecure_set(True)  # bo qua kiem tra IP/hostname trong cert, van xac thuc CA

    print(f"\nDang ket noi toi MQTT broker ({cfg['label']})...")
    try:
        client.connect(MQTT_BROKER, cfg["port"], keepalive=60)
        client.loop_forever()
    except KeyboardInterrupt:
        print("\n[...] Nhan Ctrl+C, dang dung chuong trinh...")
    finally:
        client.disconnect()
        stop_mosquitto()


if __name__ == "__main__":
    main()