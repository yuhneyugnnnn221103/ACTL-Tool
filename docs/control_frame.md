# Khung điều khiển tổng PC -> Gateway (107 byte)

Mọi lệnh điều khiển của app đi trong một khung cố định 107 byte. **Không có CRC tổng; đuôi 3 byte `E1 E2 E3`.**

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
- Khung con TRB tính CRC từ byte địa chỉ (như cũ); khung PSU tính từ CMD (như cũ).

## Phần điều khiển beam (42 byte)

Ba khung 14 byte nối nhau, cùng dữ liệu, chỉ khác byte 10 của MODE (`01`, `10`, `11`):

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
- Gateway đọc DEV để biết gửi xuống thiết bị nào, **bỏ byte này đi**, ghép lại khung gốc rồi gửi xuống TRB/PSU. DEV không nằm trong phạm vi CRC nên CRC của khung gốc không đổi (TRB tính từ byte địa chỉ, PSU tính từ CMD).
- Chiều trả lời (thiết bị về PC) là khung gốc, không có DEV. Đây là giả định của ACTL, Gateway cần giữ nguyên.
- Độ dài mỗi khung PC gửi xuống tăng thêm 1 byte (ví dụ ghi cấu hình PSU 916 byte, ghi cấu hình TRB 521 byte, gói DATA STM32 270 byte).
