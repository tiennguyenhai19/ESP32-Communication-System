# Dự án: Huffman + Hamming (7,4) + BSC trên 3 ESP32

## 1. Cấu trúc thư mục

- `01_uart_test/`: ba sketch tối giản để kiểm tra đường truyền UART trước khi tích hợp thuật toán.
- `02_final/`: ba sketch hoàn chỉnh cho kiến trúc Huffman → Hamming(7,4) → kênh BSC → giải mã Hamming → giải mã Huffman.
- Trong mỗi thư mục sketch hoàn chỉnh có một bản `common.h`. Giữ nguyên nội dung các bản này và không chỉ sửa `TRAINING_CORPUS` ở một bo mạch.

## 2. Kiến trúc cuối cùng

**ESP32 #1 — Bộ phát**

1. Chọn một bản tin ASCII từ danh sách mẫu.
2. Mã hóa nguồn bằng Huffman với bảng mã cố định.
3. Chia chuỗi bit Huffman thành các nhóm 4 bit và mã hóa mỗi nhóm thành một từ Hamming 7 bit.
4. Đóng gói frame và gửi qua UART.

**ESP32 #2 — Kênh truyền mô phỏng BSC**

1. Đọc một frame hoàn chỉnh.
2. Với từng bit thuộc trường payload Hamming, thực hiện phép thử Bernoulli độc lập với xác suất `P_ERROR`.
3. Nếu phép thử thành công thì đảo đúng bit đó.
4. Chuyển tiếp nguyên header và payload đã bị tác động. Không làm nhiễu header và không đảo các bit đệm ở cuối payload.

**ESP32 #3 — Bộ thu**

1. Đọc frame, giải mã Hamming và đếm số từ mã có syndrome khác 0.
2. Giải mã Huffman bằng cùng bảng mã cố định đã dùng ở bộ phát.
3. Tính CRC16 của nội dung sau giải mã và so với CRC gốc nằm trong header.
4. In dữ liệu nhận được, bảng so sánh từng frame và thống kê tích lũy trên Serial Monitor.

## 3. Sơ đồ nối dây UART

| Kết nối | Chân phát (TX) | Chân nhận (RX) |
|---|---:|---:|
| ESP32 #1 → ESP32 #2 | #1 GPIO17 | #2 GPIO16 |
| ESP32 #2 → ESP32 #3 | #2 GPIO26 | #3 GPIO16 |
| Mass chung | GND | GND của cả 3 bo |

- Tốc độ UART: **115200 baud, 8-N-1**.
- Nối GND chung giữa cả ba bo.
- Chỉ nối TX → RX, không nối TX → TX.
- Cấp nguồn từng bo qua USB. Không nối chung chân 5V/VIN của các bo.
- GPIO17 của ESP32 #1 dùng làm TX UART2; GPIO16 của ESP32 #2 dùng làm RX UART1. ESP32 #2 dùng GPIO26 làm TX UART2 đến ESP32 #3.

## 4. Chạy theo từng giai đoạn

### Giai đoạn 1 — Kiểm tra UART thô

1. Mở và nạp `01_uart_test/TX_UART_TEST/TX_UART_TEST.ino` vào ESP32 #1.
2. Nạp `01_uart_test/CHANNEL_UART_TEST/CHANNEL_UART_TEST.ino` vào ESP32 #2.
3. Nạp `01_uart_test/RX_UART_TEST/RX_UART_TEST.ino` vào ESP32 #3.
4. Mở Serial Monitor của từng bo ở 115200 baud.
5. Xác nhận ESP32 #1 phát các dòng `UART_TEST`, ESP32 #2 ghi nhận dòng chuyển tiếp, ESP32 #3 nhận đúng nội dung. Đây là phép thử đường truyền, chưa thêm nhiễu.

### Giai đoạn 2 — Bật mô hình BSC trên trường dữ liệu đã mã hóa kênh

Chuyển sang ba sketch trong `02_final/`. Trong file `ESP32_CHANNEL_BSC.ino`, đặt:

```cpp
static const float P_ERROR = 0.00f;
```

Trước tiên, `P_ERROR = 0.00` để xác nhận frame đi xuyên suốt và ESP32 #3 báo `CRC PASS`. Sau đó thử lần lượt `0.01`, `0.02` và `0.05`, mỗi lần sửa thì nạp lại sketch cho ESP32 #2. ESP32 #2 sẽ in số bit đã đảo, số bit được xét và BER mẫu quan sát.

**Lưu ý:** BSC chỉ có ý nghĩa đúng theo yêu cầu khi làm việc trên payload đã mã hóa Hamming. Vì vậy, sau phép thử UART thô ở giai đoạn 1, ta bật BSC trong pipeline đã có Hamming; không đảo trực tiếp bit của chuỗi ASCII chưa mã hóa.

### Giai đoạn 3 — Huffman + Hamming + thống kê

Nạp các sketch sau:

- ESP32 #1: `02_final/ESP32_TX_HUFFMAN_HAMMING/ESP32_TX_HUFFMAN_HAMMING.ino`
- ESP32 #2: `02_final/ESP32_CHANNEL_BSC/ESP32_CHANNEL_BSC.ino`
- ESP32 #3: `02_final/ESP32_RX_HUFFMAN_HAMMING/ESP32_RX_HUFFMAN_HAMMING.ino`

Trong Arduino IDE, mỗi sketch được mở từ thư mục riêng có tên trùng với file `.ino`. Chọn đúng board ESP32 và cổng COM trước khi nạp.

## 5. Thông tin frame

Header có 13 byte và được giữ nguyên bởi mô hình kênh:

| Trường | Kích thước | Ý nghĩa |
|---|---:|---|
| Magic | 2 byte | Đồng bộ frame: `A5 5A` |
| Version | 1 byte | Phiên bản giao thức |
| Sequence | 2 byte | Số thứ tự frame |
| Original length | 2 byte | Số byte dữ liệu nguồn |
| Huffman bit length | 2 byte | Số bit có nghĩa sau mã hóa Huffman |
| Channel bit length | 2 byte | Số bit mã Hamming, chưa tính bit đệm cuối byte |
| CRC16 original | 2 byte | CRC16-CCITT của dữ liệu nguồn |

Payload là các bit Hamming đã được đóng gói theo thứ tự MSB-first. CRC nằm trong header không được BSC làm nhiễu theo giả định của mô hình này; CRC dùng để kiểm tra xem payload có bị hỏng sau giải mã hay không.

## 6. Những số liệu cần ghi vào báo cáo

Bộ thu tự động in các cột:

- `Raw(bit)`: số bit của dữ liệu gốc, bằng `originalLen × 8`.
- `Huff(bit)`: số bit thực tế sau mã hóa Huffman, không tính padding.
- `Ham(bit)`: số bit sau mã Hamming(7,4).
- `CR_Huff = Raw / Huff`: tỷ số nén chỉ xét mã hóa nguồn; lớn hơn 1 nghĩa là Huffman giảm số bit.
- `CR_Frame`: tỷ số dữ liệu gốc trên tổng bit của header và payload UART; chỉ tiêu này phản ánh overhead frame nhưng không tính bảng mã vì hai đầu đã chia sẻ bảng mã cố định.
- `CW_syn`: số từ mã Hamming có syndrome khác 0. Đây là số từ mã bị bộ giải mã phát hiện cần hiệu chỉnh, không phải phép đo chính xác số bit lỗi BSC.
- `CRC PASS/FAIL`: so sánh CRC16 tính lại từ dữ liệu giải mã với CRC gốc trong header.

ESP32 #2 in `flipped / channelBits` để thống kê BER mẫu do chính mô hình BSC tạo ra. Do BSC là ngẫu nhiên, số bit đảo mỗi frame sẽ thay đổi dù `P_ERROR` không đổi.

## 7. Giới hạn và lưu ý kỹ thuật

- **Bảng Huffman cố định:** bộ phát và bộ thu cùng tạo bảng từ `TRAINING_CORPUS` trong `common.h`. Các bản tin mẫu chỉ dùng ký tự có trong corpus. Nếu sửa nội dung bản tin, hãy bảo đảm mọi ký tự đã có trong corpus và đồng bộ cùng corpus trên ESP32 #1 và #3. Đây là thiết kế phù hợp bản thử nghiệm; chưa gửi bảng mã thích nghi trong từng frame.
- **Hamming(7,4):** có thể sửa một lỗi bit trong mỗi từ mã 7 bit. Hai lỗi trở lên trong cùng một từ mã có thể gây giải mã sai hoặc miscorrection; CRC giúp phát hiện nhiều trường hợp payload sai.
- **Tỷ lệ nén toàn hệ thống:** Hamming thêm bit kiểm tra và header tạo overhead. Vì vậy, `CR_Huff` có thể lớn hơn 1 nhưng `CR_Frame` vẫn nhỏ hơn 1. Đây không phải lỗi, mà là khác biệt giữa tỷ số nén nguồn và kích thước truyền thực tế.
- **Mô hình lý tưởng hóa:** yêu cầu đặt ra là chỉ nhiễu payload; header, cáp UART và bản thân ESP32 không bị mô phỏng lỗi. Một kênh vật lý thực sẽ có thể làm hỏng cả header và cần thêm cơ chế đồng bộ/kiểm tra header.
- **Tăng xác suất lỗi:** ở mức `P_ERROR` cao, CRC FAIL là kết quả có thể xảy ra và dùng để minh họa giới hạn sửa lỗi của Hamming(7,4), không nhất thiết là lỗi chương trình.

## 8. Xử lý nhanh khi không chạy

- Nếu ESP32 #2 không thấy frame: kiểm tra dây #1 GPIO17 → #2 GPIO16, GND chung và baud 115200.
- Nếu ESP32 #3 không nhận: kiểm tra dây #2 GPIO26 → #3 GPIO16.
- Nếu `CRC FAIL` ngay cả khi `P_ERROR = 0.00`: kiểm tra cả ba board đều đang chạy sketch trong `02_final/`, và không sửa `TRAINING_CORPUS` riêng ở TX/RX.
- Nếu `P_ERROR > 0`: thử giảm xuống `0.005` hoặc `0.01` để quan sát các frame được Hamming sửa thành công trước khi tăng lên mức cao hơn.

## 9. Mô phỏng phần mềm

Báo cáo và kết quả mô phỏng nằm trong `03_simulation/`. Đây là kết quả mô phỏng thuật toán trên máy tính, không phải phép đo trên phần cứng ESP32 thật.
