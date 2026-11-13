# Bản tin điều khiển PC -> Gateway

Không còn khung điều khiển tổng 107 byte có mask: mỗi lệnh điều khiển là một **bản tin độc lập**, gửi như mọi bản tin khác qua đường **Ethernet** tới Gateway, có thêm phần đầu `DEV + LEN` sau `AB CD` (xem `proto/gateway_header_proto.h`): `[AB CD][DEV][LEN 2B big-endian][CMD ...][...][CRC][E1 E2]`, LEN = tổng độ dài cả khung sau khi chèn, CRC vẫn tính từ byte địa chỉ. Chế độ Debug COM gửi khung gốc, không có DEV + LEN.

| Bản tin | CMD | Khung gốc | DEV | LEN (cả DEV + LEN) |
|---|---|---|---|---|
| Điều khiển TRB | `A2 A2` | 14 byte | `01` | 17 |
| Beam TRB | `14 14` | 17 byte | `01` | 20 |
| Điều khiển beam (RDPU, 3 khung x 14 byte, mục dưới) | không có CMD chung | 42 byte | TYPE `05` (đặt 5 byte `AB CD 05 LEN` trước khối) | 47 (5 + 42) |
| Điều khiển PSU | `01 01` | 13 byte | `02` | 16 |

- Mọi khung TRB và PSU tính CRC từ byte địa chỉ (ngay sau CMD); khung beam tính CRC từ header (mục dưới).
- Bản tin beam (RDPU) không chèn vào giữa: phần mềm đặt `AB CD 05 LEN(2, big-endian)` (5 byte) trước nguyên khối 42 byte, LEN = 47; Gateway bỏ 5 byte đầu. Debug COM gửi khối 42 byte trần.

## Phần điều khiển beam (42 byte)

Ba khung 14 byte nối nhau, ứng với ba mode (byte 10 của MODE lần lượt `01`, `10`, `11`); mỗi khung có ESP, BETA, FREQ và MODE (byte 9) **riêng**, nhập ở ba cột trên trang Giả lập beam RDPU:

`AB CD | ESP(2) | BETA(2) | FREQ(2) | MODE(2) | CRC(2) | EF FF`, mọi trường 2 byte **little endian**.

**CRC16-CCITT-FALSE của khung beam tính từ header (`AB CD`) tới hết MODE (byte 1..10)**, ghi byte thấp trước. Chỉ riêng khung beam tính như vậy.

# Bản tin Calib (CMD 18 18, 150 byte)

PC gửi qua đường Ethernet tới Gateway: khung gốc 150 byte, có thêm DEV `01` và LEN (2 byte, big-endian, tổng cả khung = 153) sau `AB CD` khi gửi xuống Gateway (xem `proto/gateway_header_proto.h`); chế độ Debug COM gửi khung gốc. Không có trả lời.

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
