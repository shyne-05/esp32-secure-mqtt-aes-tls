# KỊCH BẢN BẢO VỆ ĐỒ ÁN

## Đề tài

**Application of AES-256-GCM and TLS 1.3 for Secure MQTT Communication on ESP32-S3**

Nhóm thực hiện:

- Nguyễn Anh Mẫn: firmware ESP-IDF, phần cứng ESP32-S3–DHT11, cấu hình MQTT/TLS và bốn Stage.
- Trần Tấn Ngọc Tính: listener Python, xử lý AES-GCM, lưu CSV, tổng hợp số liệu và biểu đồ.

---

## 1. Cách trình bày tổng quát trong 3–5 phút đầu

Đồ án xây dựng một hệ thống IoT đọc nhiệt độ và độ ẩm từ DHT11 trên ESP32-S3, sau đó truyền dữ liệu qua MQTT broker. Điểm chính của đồ án là so sánh tác động của hai lớp bảo mật: mã hóa payload bằng AES-256-GCM và bảo vệ kênh truyền bằng TLS.

Nhóm chia hệ thống thành bốn Stage:

1. Stage 1: MQTT plaintext, dùng làm baseline.
2. Stage 2: mã hóa payload bằng AES-256-GCM, sau đó mã hóa Base64 để truyền qua MQTT.
3. Stage 3: MQTT chạy trên TLS, sử dụng cổng 8883 và chứng thư CA.
4. Stage 4: kết hợp AES-256-GCM ở payload với TLS ở kênh truyền.

Mỗi Stage đều dùng cùng pipeline cảm biến–ESP32–MQTT–listener. Nhóm ghi nhận payload size, heap, thời gian AES, thời gian TLS, CPU, nhiệt độ và độ ẩm. Kết quả được lưu trong CSV và tổng hợp bằng `compare_stages.py`.

Điểm quan trọng của đồ án là không chỉ chứng minh dữ liệu truyền thành công, mà còn đo chi phí bảo mật trên thiết bị nhúng có tài nguyên giới hạn.

---

## 2. Sơ đồ giải thích hệ thống

```text
DHT11
  │ GPIO4
  ▼
ESP32-S3 firmware
  │ Đọc cảm biến, tạo JSON, đo heap/thời gian
  │
  ├─ Stage 1: JSON plaintext
  ├─ Stage 2: AES-GCM → Base64
  ├─ Stage 3: TLS/MQTTS
  └─ Stage 4: AES-GCM → Base64 + TLS/MQTTS
  │
  ▼
Mosquitto MQTT Broker
  │
  ▼
Python Listener
  │ Subscribe, giải mã, kiểm tra tag, parse JSON
  ▼
CSV → compare_stages.py → bảng và biểu đồ
```

Cách giải thích ngắn:

> ESP32-S3 chịu trách nhiệm tạo dữ liệu và publish. Broker chịu trách nhiệm nhận và phân phối bản tin. Listener chịu trách nhiệm xác thực, giải mã, lưu trữ và phân tích. Nhờ tách các thành phần này, nhóm có thể đo riêng tác động của AES-GCM và TLS.

---

## 3. Quy trình thực hành trực tiếp

### 3.1. Kiểm tra phần cứng

Trình bày ảnh mạch thật và giải thích:

- ESP32-S3 được cấp nguồn qua USB.
- DHT11 nối VCC và GND đúng cực.
- Chân DATA của DHT11 nối vào GPIO4.
- GPIO4 được cấu hình trong firmware bằng `GPIO_NUM_4`.
- DHT11 cần điện trở kéo lên hoặc module đã tích hợp điện trở kéo lên.

Câu nói khi bảo vệ:

> Nhóm chọn GPIO4 vì đây là GPIO được dùng thống nhất trong firmware. Khi thay đổi chân phần cứng, phải thay đổi cả cấu hình GPIO và kiểm tra lại dây DATA, nếu không listener có thể nhận dữ liệu lỗi hoặc không có dữ liệu.

### 3.2. Kiểm tra firmware

Firmware thực hiện các bước:

1. Khởi tạo NVS và Wi-Fi.
2. Kết nối MQTT broker.
3. Đọc nhiệt độ, độ ẩm từ DHT11.
4. Tạo JSON gồm `id`, `temp`, `humid`, `ts`, `heap` và các trường đo hiệu năng.
5. Tùy Stage, giữ plaintext hoặc mã hóa AES-GCM/Base64.
6. Publish lên topic tương ứng.
7. Lặp lại theo chu kỳ khoảng 10 giây.

### 3.3. Nạp từng Stage

Quy trình chung:

```powershell
cd C:\doan2
.\flash_tool.ps1
```

Nếu script yêu cầu lựa chọn Stage, chọn đúng thư mục:

```text
stage1_baseline
stage2_aesgcm
stage3_tls
stage4_aesgcm_tls
```

Khi flash thành công cần quan sát:

- Firmware được build thành công.
- Đúng target ESP32-S3.
- Đúng cổng COM.
- Flash không báo lỗi kết nối.
- Serial monitor xuất hiện log Wi-Fi/MQTT.

Nếu thầy hỏi vì sao phải flash từng Stage:

> Mỗi Stage là một cấu hình firmware độc lập. Flash riêng giúp nhóm kiểm soát biến thí nghiệm và so sánh chính xác chi phí của từng lớp bảo mật.

### 3.4. Chạy broker và listener

Broker cần chạy trước listener. Sau đó listener subscribe đúng topic của Stage đang thử nghiệm.

Các cổng chính:

| Stage | Giao thức | Cổng | Payload |
|---|---|---:|---|
| 1 | MQTT | 1883 | Plaintext JSON |
| 2 | MQTT | 1883 | AES-GCM + Base64 |
| 3 | MQTTS | 8883 | JSON qua TLS |
| 4 | MQTTS | 8883 | AES-GCM + Base64 qua TLS |

Log listener cần chứng minh:

- Đã kết nối broker.
- Đã subscribe đúng topic.
- Nhận đúng bản tin.
- Giải mã thành công nếu là Stage 2/4.
- Kiểm tra authentication tag thành công.
- Parse JSON thành công.
- Ghi được dòng CSV.

---

## 4. Cách giải thích từng Stage

### Stage 1 – Baseline

Stage 1 không mã hóa payload và không dùng TLS. Đây không phải là cấu hình an toàn, mà là mốc chuẩn để đo.

Payload mẫu có kích thước khoảng **94 B**, heap trung bình khoảng **265.220 B**, CPU khoảng **0,2%**.

Câu trả lời:

> Stage 1 cần thiết vì nếu không có baseline, nhóm không thể biết AES-GCM và TLS làm tăng thêm bao nhiêu payload, thời gian xử lý và bộ nhớ.

### Stage 2 – AES-256-GCM

Payload JSON được mã hóa bằng AES-GCM. Gói dữ liệu có thể mô tả:

```text
nonce || ciphertext || authentication_tag
```

Sau đó dữ liệu nhị phân được Base64 để truyền dưới dạng chuỗi MQTT.

Kết quả đo:

- Payload trung bình: **168 B**.
- Thời gian AES trung bình: **530,3 µs**.
- Heap trung bình: **265.336 B**.
- CPU: khoảng **0,3%**.

Nếu payload bị sửa, authentication tag không còn hợp lệ và listener phải từ chối dữ liệu trước khi sử dụng.

### Stage 3 – TLS

Stage 3 giữ payload ứng dụng ở dạng JSON nhưng truyền qua MQTTS cổng 8883. TLS bảo vệ kênh truyền và xác thực broker theo CA.

Kết quả đo:

- Payload ứng dụng: **96 B**.
- TLS handshake trung bình: **755 ms**.
- Heap trung bình: **227.896 B**.
- CPU: khoảng **0,2%**.

Cần phân biệt `raw_payload_size` với kích thước TLS record. `raw_payload_size` chỉ phản ánh payload ứng dụng được listener ghi nhận, không bao gồm toàn bộ overhead của TLS ở tầng vận chuyển.

### Stage 4 – AES-GCM + TLS

Stage 4 kết hợp hai lớp:

- AES-GCM bảo vệ nội dung payload.
- TLS bảo vệ đường truyền giữa client và broker/listener.

Kết quả đo:

- Payload trung bình: **168 B**.
- AES trung bình: **491,7 µs**.
- TLS handshake trung bình: **757 ms**.
- Heap trung bình: **227.868 B**.
- CPU: khoảng **0,3%**.

Câu trả lời:

> Stage 4 có mức bảo vệ nhiều lớp nhất trong phạm vi đồ án. Tuy nhiên, nó cũng có chi phí bộ nhớ và thời gian lớn hơn vì phải duy trì cả ngữ cảnh TLS và quá trình mã hóa payload.

---

## 5. Cách giải thích số liệu

### Payload

Baseline là 94 B. AES-GCM làm payload tăng lên 168 B, tương đương tăng khoảng:

```text
(168 - 94) / 94 × 100 ≈ 78,7%
```

Nguyên nhân là nonce, authentication tag và Base64.

### Heap

Stage 1 có heap trung bình 265.220 B. Stage 3 có 227.896 B.

Mức giảm:

```text
265.220 - 227.896 = 37.324 B ≈ 36,45 KB
```

Đây là chi phí của TLS context, CA certificate, certificate chain, buffer và task liên quan đến TLS.

### AES và TLS

AES xử lý trong khoảng vài trăm microsecond. TLS handshake tính bằng millisecond vì phải thực hiện quá trình bắt tay, xác thực chứng thư và tạo khóa phiên.

Cần nhấn mạnh:

> TLS handshake không xảy ra lại ở mỗi bản tin nếu kết nối được duy trì. Vì vậy chu kỳ publish 10 giây không bị cộng thêm 755–757 ms cho từng bản tin.

### CPU

CPU đo được từ 0,2–0,3%. Kết quả này chỉ đúng trong điều kiện thí nghiệm hiện tại: một cảm biến, chu kỳ 10 giây, số mẫu giới hạn và không có tải mạng lớn.

---

## 6. Câu hỏi phản biện thường gặp

### Câu 1: Vì sao chọn MQTT thay vì HTTP?

MQTT dùng mô hình publish/subscribe, có broker trung gian và header nhỏ, phù hợp thiết bị IoT truyền dữ liệu định kỳ. HTTP cũng có thể dùng, nhưng thường có overhead request/response lớn hơn và không thuận tiện bằng MQTT khi có nhiều subscriber.

### Câu 2: Base64 có phải là mã hóa bảo mật không?

Không. Base64 chỉ là cách biểu diễn dữ liệu nhị phân thành chuỗi ký tự. Ai cũng có thể decode Base64. Bảo mật trong Stage 2/4 đến từ AES-GCM; Base64 chỉ giúp payload dễ truyền qua MQTT.

### Câu 3: AES-GCM khác gì AES thường?

AES là thuật toán mã hóa khối. GCM là mode vận hành cung cấp cả confidentiality và authentication. Vì vậy AES-GCM không chỉ che nội dung mà còn tạo authentication tag để phát hiện dữ liệu bị sửa.

### Câu 4: TLS đã bảo mật rồi, tại sao còn cần AES-GCM?

TLS bảo vệ dữ liệu trên một kết nối cụ thể. Nếu dữ liệu được giải mã tại broker hoặc đi qua hệ thống trung gian sau điểm kết thúc TLS, payload có thể trở lại dạng rõ. AES-GCM bảo vệ payload ở tầng ứng dụng và bổ sung lớp bảo vệ độc lập.

### Câu 5: Tại sao Stage 2 không dùng TLS?

Để đo riêng chi phí mã hóa payload. Nếu Stage 2 đã dùng TLS thì không thể tách được overhead của AES-GCM khỏi overhead của TLS.

### Câu 6: Tại sao Stage 3 payload không tăng nhiều như Stage 2?

Vì Stage 3 chỉ bảo vệ kênh truyền. Payload ứng dụng vẫn gần với JSON ban đầu. Phần overhead TLS nằm ở tầng record/transport và không được tính đầy đủ trong trường `raw_payload_size` của listener.

### Câu 7: Khóa AES đang được lưu như thế nào?

Trong mô hình lab, khóa được cấu hình cố định để hai phía có thể mã hóa và giải mã. Đây là hạn chế nếu triển khai thực tế. Hệ thống thật nên dùng khóa riêng từng thiết bị, Secure Element như ATECC608A, provisioning an toàn, xoay vòng khóa và thu hồi khóa.

### Câu 8: Nếu có 1.000 thiết bị thì quản lý khóa ra sao?

Mỗi thiết bị cần identity và khóa riêng. Khóa có thể được sinh trong Secure Element, đăng ký với KMS hoặc server provisioning, sau đó cấp session key động bằng ECDH. Khi thiết bị bị mất, chỉ thu hồi khóa của thiết bị đó thay vì thay khóa toàn hệ thống.

### Câu 9: Hệ thống chống replay chưa?

Trong bản hiện tại, payload có `id` và `ts`, nhưng cơ chế anti-replay đầy đủ chưa được triển khai hoàn chỉnh. Giải pháp là listener lưu `Last_Seen_ID` theo từng thiết bị và chỉ chấp nhận `id` tăng dần, đồng thời từ chối timestamp quá cũ trong một cửa sổ thời gian ΔT.

### Câu 10: `insecure=True` có ý nghĩa gì?

Nó bỏ qua hostname verification. TLS vẫn có thể mã hóa kênh, nhưng client chưa xác nhận chắc chắn broker đúng tên máy chủ. Khi triển khai thật cần đặt chứng thư có CN/SAN đúng hostname, bật kiểm tra hostname và dùng `tls_insecure_set(False)`.

### Câu 11: Nếu sửa một byte ciphertext thì chuyện gì xảy ra?

Authentication tag không còn khớp. Listener phải báo lỗi xác thực và từ chối parse dữ liệu. Đây là negative test chứng minh AES-GCM cung cấp toàn vẹn chứ không chỉ mã hóa.

### Câu 12: Vì sao heap Stage 3/4 giảm mạnh?

TLS cần bộ nhớ cho context, certificate, CA chain, buffer mã hóa record và các task mạng. Vì vậy heap tự do giảm khoảng 37 KB so với baseline.

### Câu 13: Kết quả có thể áp dụng cho mọi ESP32 không?

Không nên khẳng định tuyệt đối. Kết quả phụ thuộc model ESP32, phiên bản ESP-IDF, cấu hình compiler, chứng thư, Wi-Fi, broker, số lượng mẫu và cách đặt mốc đo. Các con số trong báo cáo là kết quả của ESP32-S3 trong cấu hình thí nghiệm của nhóm.

### Câu 14: Đóng góp chính của nhóm là gì?

Nhóm xây dựng trọn pipeline từ cảm biến đến CSV, triển khai bốn cấu hình bảo mật, viết listener xử lý dữ liệu, tạo negative test và đo được chi phí payload, AES, TLS, heap và CPU. Đóng góp chính là phương pháp so sánh có kiểm soát chứ không chỉ bật một thư viện bảo mật.

---

## 7. Tình huống lỗi khi bảo vệ

### Không nhận được dữ liệu

Kiểm tra theo thứ tự:

1. ESP32 có kết nối Wi-Fi không?
2. Broker có chạy không?
3. Đúng cổng 1883 hay 8883 chưa?
4. Listener có subscribe đúng topic không?
5. Dây DHT11 và GPIO4 có đúng không?
6. Có firewall chặn broker không?

### TLS không kết nối

Kiểm tra:

- CA trên client có đúng CA ký server certificate không.
- Broker có mở cổng 8883 không.
- Đường dẫn certificate có đúng không.
- Thời hạn certificate còn hợp lệ không.
- Hostname/IP trong certificate có khớp cấu hình không.
- Trong lab có đang bật chế độ bỏ qua hostname hay không.

### AES-GCM giải mã thất bại

Kiểm tra:

- Khóa ở firmware và listener có giống nhau không.
- Base64 có bị cắt hoặc thêm ký tự không.
- Nonce có đủ 12 byte không.
- Tag có đủ 16 byte không.
- Payload có bị sửa trong quá trình truyền không.
- Topic có đúng Stage không.

### CSV không ghi được

Kiểm tra quyền ghi thư mục, header CSV, kiểu dữ liệu và exception trong listener. Không nên sửa CSV thủ công trước khi phân tích vì sẽ làm mất khả năng đối chiếu với log gốc.

---

## 8. Những câu không nên trả lời quá mức

Không nói:

- “Hệ thống an toàn tuyệt đối.”
- “TLS bảo vệ mọi dữ liệu ở mọi nơi.”
- “Base64 là mã hóa.”
- “Kết quả này áp dụng cho mọi thiết bị IoT.”
- “Đã chống replay hoàn chỉnh” nếu chưa triển khai cửa sổ chống replay.

Nên nói:

> Trong phạm vi mô hình lab, nhóm chứng minh được tác động của AES-GCM và TLS bằng log, CSV và negative test. Các vấn đề quản lý khóa, hostname verification và anti-replay được xác định rõ là giới hạn hoặc hướng hoàn thiện tiếp theo.

---

## 9. Câu kết thúc phần bảo vệ

> Qua đồ án, nhóm không chỉ triển khai được hệ thống MQTT bảo mật trên ESP32-S3 mà còn đo được chi phí thực tế của từng lớp bảo mật. Kết quả cho thấy AES-GCM làm tăng kích thước payload và thời gian mã hóa, còn TLS làm tăng thời gian thiết lập kết nối và sử dụng bộ nhớ. Đây là cơ sở để lựa chọn mức bảo mật phù hợp giữa an toàn thông tin và tài nguyên của thiết bị IoT.

---

## 10. Checklist trước khi trình bày

- [ ] ESP32-S3, DHT11 và dây GPIO4 đã sẵn sàng.
- [ ] Biết Stage đang flash và topic tương ứng.
- [ ] Broker đã chạy.
- [ ] Listener đã chạy hoặc có log dự phòng.
- [ ] Mở sẵn CSV và biểu đồ.
- [ ] Không trình chiếu khóa AES, mật khẩu Wi-Fi, private key hoặc token thật.
- [ ] Nhớ số liệu chính: 94 B, 168 B, 96 B, 265 KB, 227 KB, 530 µs, 755–757 ms.
- [ ] Nếu chạy trực tiếp lỗi, trình bày log đã thu và giải thích nguyên nhân thay vì tự tạo số liệu.
