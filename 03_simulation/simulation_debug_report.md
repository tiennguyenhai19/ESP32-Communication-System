# Báo cáo mô phỏng và debug — Huffman + Hamming (7,4) + BSC trên 3 ESP32

## 1. Phạm vi kiểm thử

Mô phỏng phần mềm bám theo thuật toán trong `common.h`: cùng `TRAINING_CORPUS`, Huffman cố định, Hamming (7,4), frame có header 13 byte, CRC16-CCITT, và BSC đảo từng bit độc lập chỉ trong `channelBits` của payload. Mô phỏng giả định hai đường UART hoạt động hoàn hảo; không mô phỏng sai baud, nhiễu điện, mất byte, dây nối hay thời gian thực của bo ESP32. Vì vậy đây là **kiểm thử thuật toán và giao thức trên máy tính**, chưa phải kết quả đo từ phần cứng thật.

## 2. Kết quả kiểm tra chức năng

| Hạng mục | Kết quả | Diễn giải |
|---|---:|---|
| Hamming unit test | **128/128 = 100%** | 16 giá trị nibble; mỗi giá trị thử trường hợp không lỗi và 7 vị trí lỗi một bit. Tất cả phục hồi đúng. |
| Huffman → Hamming → Hamming decode → Huffman decode, kênh sạch | **4/4 = 100%** | Cả bốn thông điệp mẫu trả về đúng từng byte và CRC đúng khi `P_ERROR=0`. |
| Syntax check các sketch C++ | **6/6 pass** | Kiểm tra cú pháp với Arduino API stub tối thiểu. Đây không phải biên dịch bằng Arduino-ESP32 core và không xác nhận đã nạp/chạy trên bo thật. |
| UART thuần trong mô hình | **100% byte được chuyển tiếp theo thiết kế** | ESP32 #2 relay byte-for-byte, không gây nhiễu ở giai đoạn 1. Đây là kết quả mô hình lý tưởng, chưa xác nhận dây UART vật lý. |

## 3. Kích thước dữ liệu và tỷ lệ nén

| Thông điệp | Raw bit | Huffman bit | Hamming bit | Payload byte | Frame byte | CR_Huff | CR_Frame | Hiệu suất UART 8N1 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| ESP32 UART TEST MESSAGE 001 | 216 | 118 | 210 | 27 | 40 | 1.831 | 0.675 | 54.00% |
| HUFFMAN SOURCE CODING TEST | 208 | 109 | 196 | 25 | 38 | 1.908 | 0.684 | 54.74% |
| HAMMING 7,4 WITH BSC NOISE | 208 | 120 | 210 | 27 | 40 | 1.733 | 0.650 | 52.00% |
| BER CORRECTION AND DATA FRAME | 232 | 117 | 210 | 27 | 40 | 1.983 | 0.725 | 58.00% |
| **Tổng 4 bản tin** | **864** | **464** | **826** | **106** | **158** | **1.862** | **0.684** | **54.68%** |

- Với corpus thử này, riêng Huffman giảm số bit nguồn từ 864 xuống 464: **tiết kiệm 46,30%**. Đây không phải mức nén đại diện cho mọi văn bản: bảng mã được dựng từ chính tập câu thử nên có lợi thế với các ký tự/từ xuất hiện lặp lại.
- Hamming làm số bit mã hóa tăng từ 464 lên 826, vì mỗi 4 bit được mã thành 7 bit. Sau khi thêm 13 byte header cho từng frame, 108 byte dữ liệu nguồn trở thành 158 byte frame: **tăng 46,30% số byte phải truyền** trong thử nghiệm ngắn này.
- `CR_Huff = Raw/Huff`; lớn hơn 1 nghĩa là riêng Huffman nén được. `CR_Frame = Raw bits / (header + payload, tính 8 bit mỗi byte)` nên nhỏ hơn 1 nghĩa là frame lớn hơn dữ liệu gốc. `Hiệu suất UART 8N1` tính `raw information bits / [(header + payload bytes) × 10]`, vì mỗi byte UART 8-N-1 chiếm 10 bit đường truyền (start + 8 data + stop). Các chỉ số này không tính thời gian giữa các frame.

## 4. Mô phỏng kênh BSC — 10.000 frame cho mỗi xác suất

Mỗi xác suất sử dụng seed cố định để có thể tái tạo kết quả; thông điệp luân phiên qua bốn câu mẫu. `Success` yêu cầu nội dung giải mã khớp chính xác với bản gốc **và** CRC hợp lệ.

| P_ERROR | BER quan sát | Nhận đúng | Lỗi frame | Tỷ lệ nhận đúng |
|---:|---:|---:|---:|---:|
| 0% | 0.00000 | 10.000/10.000 | 0 | **100.00%** |
| 0.1% | 0.00102 | 9.995/10.000 | 5 | **99.95%** |
| 0.5% | 0.00501 | 9.859/10.000 | 141 | **98.59%** |
| 1% | 0.00987 | 9.394/10.000 | 606 | **93.94%** |
| **2%** | **0.01992** | **7.969/10.000** | **2.031** | **79.69%** |
| 3% | 0.02991 | 6.021/10.000 | 3.979 | **60.21%** |
| 5% | 0.04981 | 2.722/10.000 | 7.278 | **27.22%** |
| 10% | 0.09990 | 97/10.000 | 9.903 | **0.97%** |

Ở cấu hình hiện tại `P_ERROR=0.02`, số liệu mô phỏng cụ thể là **7.969 frame nhận đúng trên 10.000**, tức **79,69%**. Đây là một kết quả Monte Carlo có seed xác định, không phải cam kết mọi lần chạy hoặc tỷ lệ đo trên phần cứng.

### Vì sao Hamming vẫn có frame lỗi ở p=2%?

Hamming (7,4) chỉ đảm bảo sửa một bit lỗi trong mỗi từ mã 7 bit. Xác suất một từ mã có từ hai lỗi bit trở lên là:

`1 - (1-p)^7 - 7p(1-p)^6`.

Với `p=0.02`, xác suất đó xấp xỉ **0,786% cho từng từ mã**. Mỗi frame mẫu có khoảng 28–30 từ mã; vì vậy khả năng một frame có ít nhất một từ mã nhiều lỗi là đáng kể. Bộ giải mã Hamming tiêu chuẩn có thể hiệu chỉnh nhầm trong trường hợp nhiều bit lỗi. CRC16 sẽ báo lỗi với đa số trường hợp dữ liệu sai, nhưng không cung cấp khả năng sửa lỗi và cũng không đảm bảo phát hiện mọi mẫu lỗi có thể có.

## 5. Ví dụ debug từng ESP32

### ESP32 #1 — Bộ phát

Ví dụ bản tin `BER CORRECTION AND DATA FRAME`:
- Dữ liệu nguồn: 29 byte = 232 bit.
- Huffman: 117 bit.
- Hamming: 210 bit, đóng gói thành 27 byte payload.
- Header: 13 byte; tổng frame truyền là 40 byte.

### ESP32 #2 — BSC

Trong trace nhiễu có seed `12345` và `p=2%`, frame thứ tư có 8 bit bị đảo trong 210 bit mã kênh. Tỷ lệ quan sát riêng frame là `8/210 = 3,81%`; điều này hoàn toàn có thể xảy ra trong mô hình ngẫu nhiên dù p đặt là 2%. Payload HEX trước BSC:

`33 C2 19 6F FA 59 B3 E1 FC 78 F7 95 59 A5 86 AA 1A A5 4C ED 33 54 94 B7 0E 00 00`

Sau BSC:

`33 C2 19 EF FA 59 B3 E1 FC 78 F7 95 59 A5 86 A8 5A 85 4C ED 33 54 94 3F 0E 14 00`

### ESP32 #3 — Bộ thu

Trong mẫu này Hamming thấy 5 từ mã có syndrome khác 0 (không phải bằng chứng có đúng 5 bit lỗi; nhiều lỗi có thể tạo syndrome trùng hoặc bị hiệu chỉnh nhầm). Sau giải mã, Huffman không tạo được một chuỗi hợp lệ đúng độ dài, nên frame được báo `DECODE FAIL` và không được tính là nhận đúng. Trong mô phỏng 10.000 frame, kết quả này và các trường hợp giải mã ra dữ liệu khác được tính vào tỷ lệ lỗi.

## 6. Các vấn đề đã kiểm tra / chỉnh sửa

1. **Bảng Huffman:** ba bản `common.h` trong các sketch hoàn chỉnh có nội dung giống hệt nhau; TX và RX phải giữ cùng `TRAINING_CORPUS`.
2. **Hamming:** thứ tự parity/data và chỉ số syndrome đã qua bài test 128 trường hợp không lỗi/lỗi đơn.
3. **Chỉ vùng payload bị BSC:** mô hình chỉ thử đảo `channelBits` bit, giữ nguyên header và các bit padding cuối byte.
4. **UART test — log dễ đọc:** đã thêm xuống dòng sau log relay ở ESP32 #2 để Serial Monitor không ghép nhiều bản tin trên cùng một dòng.
5. **Số liệu hiệu suất:** đã bổ sung `EtaUART%` ở TX/RX, tính cả start/stop bit UART 8N1. Chỉ số cũ `CR_Frame` vẫn được giữ để so sánh theo byte/frame.
6. **Giới hạn kiểm tra biên dịch:** sáu sketch đã qua cú pháp C++ bằng stub; cần compile tiếp bằng Arduino IDE chọn đúng board ESP32 trước khi nạp. Chỉ thử phần cứng mới xác nhận pin, dây, baud và timing thực tế.

## 7. Kết luận đánh giá

- **Logic Hamming đơn lỗi:** 100% trong 128 ca kiểm thử có hệ thống.
- **Round-trip khi kênh sạch:** 100% trong bốn thông điệp mẫu.
- **Độ tin cậy packet tại `p=2%`:** 79,69% trong 10.000 frame mô phỏng; còn khoảng 20,31% frame lỗi.
- **Nén nguồn:** giảm 46,30% số bit trên corpus thử; nhưng frame truyền hoàn chỉnh tăng 46,30% số byte vì overhead Hamming/header. Dự án phù hợp minh họa đánh đổi giữa nén và sửa lỗi, không phải thiết kế tối ưu băng thông cho các bản tin ngắn.
- **Chưa kết luận về phần cứng:** cần chạy thử UART thuần trước, sau đó `P_ERROR=0`, rồi lần lượt 0,5%, 1%, 2%, 5%; ghi số frame CRC PASS/FAIL thực tế để so sánh với Monte Carlo.