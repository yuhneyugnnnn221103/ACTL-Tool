# Khung điều khiển tổng PC -> Gateway (107 byte)

Mọi lệnh điều khiển của app đi trong một khung cố định 107 byte; điều khiển TRB (`A2`, `14`) gửi qua **RS485**, điều khiển PSU và beam gửi qua **Ethernet** giám sát. **Không có CRC tổng; đuôi 3 byte `E1 E2 E3`.**

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

# Byte chọn thiết bị cho bản tin cấu hình và nạp code

Các bản tin cấu hình và nạp code (đường RS485) có thêm **1 byte thiết bị ngay sau `AB CD` và trước CMD**:

`AB CD | DEV | CMD ... | CRC | E1 E2`

| DEV | Thiết bị |
|---|---|
| `01` | TRB |
| `02` | PSU |
| `03` | TRSU (dành cho sau này) |

- Áp dụng cho: ghi/đọc cấu hình TRB (`A1`, `A3`), ghi/đọc cấu hình PSU (`04 04`, `03 03`), nạp FPGA của TRB (`0x88`, nạp, xóa, boot, `0x44`) và nạp STM32 của PSU (`90..97`). Khung hỏi/điều khiển/giám sát không đổi.
- Gateway đọc DEV để biết gửi xuống thiết bị nào, **bỏ byte này đi**, ghép lại khung gốc rồi gửi xuống TRB/PSU. DEV không nằm trong phạm vi CRC nên CRC của khung gốc không đổi (TRB, PSU, STM32 đều tính từ byte địa chỉ, ngay sau CMD).
- Chiều trả lời (thiết bị về PC) là khung gốc, không có DEV. Đây là giả định của ACTL, Gateway cần giữ nguyên.
- Độ dài mỗi khung PC gửi xuống tăng thêm 1 byte (ví dụ ghi cấu hình PSU 916 byte, ghi cấu hình TRB 521 byte, gói DATA STM32 270 byte).

# Bản tin Calib (CMD 18 18, 150 byte)

PC gửi qua đường RS485, có byte thiết bị `01` (TRB) sau `AB CD` nên trên đường truyền xuống Gateway dài 151 byte; Gateway bỏ byte đó đi. Không có trả lời.

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

Tab **Calib**: tích chọn kênh cho RX CHN EN và TX CHN EN (ô tích = bit 1), nhập 4 giá trị số nguyên 0..255 cho từng kênh, nút "Điền" cho cả 32 kênh cùng giá trị. "Nhập file…" và "Xuất file…" đọc ghi file JSON (`rx_enabled`, `tx_enabled` là danh sách kênh bật, `channels` có `ch`, `phase_tx`, `phase_rx`, `amp_tx`, `amp_rx`).
