# Khung điều khiển tổng PC -> Gateway (105 byte)

Mọi lệnh điều khiển của app đi trong một khung cố định 105 byte. **Không có CRC tổng và không có đuôi.**

| Byte (đếm từ 1) | Dài | Nội dung |
|---|---|---|
| 1-3 | 3 | `AB CD EF` |
| 4 | 1 | Mask: bit0 = A2, bit1 = 14, bit2 = beam, bit3 = PSU |
| 5-18 | 14 | Bản tin điều khiển TRB `A2 A2` |
| 19-35 | 17 | Bản tin điều khiển TRB `14 14` (beam TRB) |
| 36-77 | 42 | Bản tin điều khiển beam (3 khung x 14 byte, mục dưới) |
| 78-90 | 13 | Bản tin điều khiển PSU `01 01` |
| 91-104 | 14 | Dự phòng cho một module sau này |
| 105 | 1 | Đệm |

- Mỗi lần gửi chỉ **một** phần có lệnh: phần đó chứa nguyên khung con (giữ header, CRC, đuôi riêng của nó), bit mask tương ứng = 1, các bit còn lại = 0, mọi phần khác đệm `00`.
- Tổng 4 + 14 + 17 + 42 + 13 + 14 = 104; byte đệm thứ 105 thêm vào cho đủ 105 theo yêu cầu (`kLength` trong `proto/control_frame_proto.h`).
- Khung con TRB tính CRC từ byte địa chỉ (như cũ); khung PSU tính từ CMD (như cũ).

## Phần điều khiển beam (42 byte)

Ba khung 14 byte nối nhau, cùng dữ liệu, chỉ khác byte 10 của MODE (`01`, `10`, `11`):

`AB CD | ESP(2) | BETA(2) | FREQ(2) | MODE(2) | CRC(2) | EF FF`, mọi trường 2 byte **little endian**.

**CRC16-CCITT-FALSE của khung beam tính từ header (`AB CD`) tới hết MODE (byte 1..10)**, ghi byte thấp trước. Chỉ riêng khung beam tính như vậy.
