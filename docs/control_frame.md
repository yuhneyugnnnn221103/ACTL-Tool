# Khung điều khiển tổng PC -> Gateway (107 byte)

Mọi lệnh điều khiển của app đi trong một khung cố định 107 byte; gửi qua đường **Ethernet** tới Gateway (cùng đường với giám sát, cấu hình, nạp code và Calib; không còn đường RS485 và không có byte thiết bị). **Không có CRC tổng; đuôi 3 byte `E1 E2 E3`.**

| Byte (đếm từ 1) | Dài | Nội dung |
|---|---|---|
| 1-3 | 3 | `AB CD EF` |
| 4 | 1 | Mask: bit0 = A2, bit1 = 14, bit2 = beam, bit3 = PSU |
| 5-18 | 14 | Bản tin điều khiển TRB `A2 A2` |
| 19-35 | 17 | Bản tin điều khiển TRB `14 14` (beam TRB) |
| 36-77 | 42 | Bản tin điều khiển beam (3 khung x 14 byte, mục dưới) |
| 78-90 | 13 | Bản tin điều khiển PSU `01 01` |
| 91-104 | 14 | Dự phòng cho một module sau này |
| 105-107 | 3 | Đuôi `E1 E2 E3` |

- Mỗi lần gửi chỉ **một** phần có lệnh: phần đó chứa nguyên khung con (giữ header, CRC, đuôi riêng của nó), bit mask tương ứng = 1, các bit còn lại = 0, mọi phần khác đệm `00`.
- Tổng 4 + 14 + 17 + 42 + 13 + 14 + 3 (đuôi) = 107 byte (`kLength` trong `proto/control_frame_proto.h`).
- Khung con TRB tính CRC từ byte địa chỉ (như cũ); khung PSU cũng tính từ byte địa chỉ (ngay sau CMD), giống TRB.

## Phần điều khiển beam (42 byte)

Ba khung 14 byte nối nhau, ứng với ba mode (byte 10 của MODE lần lượt `01`, `10`, `11`); mỗi khung có ESP, BETA, FREQ và MODE (byte 9) **riêng**, nhập ở ba cột trên trang Beam Ethernet:

`AB CD | ESP(2) | BETA(2) | FREQ(2) | MODE(2) | CRC(2) | EF FF`, mọi trường 2 byte **little endian**.

**CRC16-CCITT-FALSE của khung beam tính từ header (`AB CD`) tới hết MODE (byte 1..10)**, ghi byte thấp trước. Chỉ riêng khung beam tính như vậy.

# Bản tin Calib (CMD 18 18, 150 byte)

PC gửi qua đường Ethernet tới Gateway, đúng 150 byte (không có byte thiết bị). Không có trả lời.

| Byte (đếm từ 1) | Dài | Nội dung |
|---|---|---|
| 1-2 | 2 | `AB CD` |
| 3-4 | 2 | `18 18` |
| 5 | 1 | MB (`FF` = tất cả) |
| 6 | 1 | TRB (`FF` = tất cả) |
| 7-10 | 4 | RX_CHN_EN: byte 7 = kênh 24-31, byte 10 = kênh 0-7; bit 0 của mỗi byte = kênh thấp nhất của byte đó |
| 11-14 | 4 | TX_CHN_EN, bố cục như RX |
| 15-142 | 128 | 32 kênh x 4 byte: phase TX, phase RX, amp TX, amp RX (kênh k bắt đầu ở byte 15 + 4k) |
| 143-146 | 4 | Dự phòng |
| 147-148 | 2 | CRC16 (CRC16-CCITT-FALSE, byte cao trước), phủ từ byte 5 (MB) đến byte 146, như các khung TRB khác |
| 149-150 | 2 | `E1 E2` |

Tab **Calib**: giao diện đánh số kênh **1-32** (kênh hiển thị n ứng với kênh n-1 trong bản tin và bit n-1 của RX/TX_CHN_EN; file JSON giữ chỉ số 0-31). Tích chọn kênh cho RX CHN EN và TX CHN EN (ô tích = bit 1), nhập 4 giá trị số nguyên 0..255 cho từng kênh, nút "Xóa" đưa mọi ô tích về bỏ chọn và mọi giá trị về 0; chọn TRB đích bằng ô MB/TRB hoặc nút Trước/Sau (không còn broadcast). "Nhập file…" và "Xuất file…" đọc ghi file JSON (`rx_enabled`, `tx_enabled` là danh sách kênh bật, `channels` có `ch`, `phase_tx`, `phase_rx`, `amp_tx`, `amp_rx`).
