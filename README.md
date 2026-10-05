# ACTL Tool

Phần mềm PC (Qt 6, C++17, QWidget) giám sát, điều khiển, cấu hình và nạp code cho hệ thống gồm 20 mother board × 8 TRB và 5 PSU, kết nối qua một Gateway.

## Tình trạng

| Chức năng | Tình trạng |
|---|---|
| Giám sát TRB (bản tin 280 byte), lưới tổng quan 20 × 8 | Xong |
| Chi tiết TRB, lệnh điều khiển và beam | Xong, chưa chờ ACK |
| Cảnh báo quá ngưỡng theo từng TRB, log CSV | Xong |
| Cấu hình TRB (đơn lẻ, hàng loạt), khóa mật khẩu | Xong |
| Nạp code FPGA trên TRB (broadcast, gửi riêng) | Xong |
| Nạp code STM32 trên PSU (slot A/B, CRC32) | Xong |
| Giám sát, điều khiển, cấu hình PSU (4 cụm DCM) | Xong, hiển thị số thô, chưa chờ ACK |
| Module mô phỏng TRB/PSU phía dưới Gateway | Chưa làm |

Code đã biên dịch và chạy test trên Linux với Qt 6.4, dùng thiết bị giả lập trong test. Chưa thử trên Windows và chưa thử với Gateway, TRB, PSU thật.

## Kết nối

```
PC ── TCP (PC là server) ── Gateway ── MB 0..19 ── TRB 0..7
PC ── RS485 (cổng COM)  ──    │
                              └────── PSU
```

| Đường | Dùng cho |
|---|---|
| TCP, link `monitor` | Giám sát và điều khiển. Gateway tự hỏi từng thiết bị rồi chuyển bản tin lên PC |
| RS485, link `service` | Cấu hình và nạp code, hỏi-đáp, mỗi lúc một thao tác |

Địa chỉ TRB là cặp (MB 0–19, TRB 0–7); `FF FF` là broadcast.

## Biên dịch

Cần Qt 6 (Core, Network, SerialPort, Widgets, Test), CMake ≥ 3.16 và trình biên dịch C++17.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

Test nằm trong `tests/` (cần Qt Test), hiện có test phần PSU.

File chạy là `build/actl_tool`.

## Sử dụng

- **Tổng quan**: mỗi ô là một TRB, tô màu theo trạng thái (tốt, quá ngưỡng, trip, mất kết nối, chưa có dữ liệu). Bấm vào ô để mở trang chi tiết.
- **Chi tiết TRB**: toàn bộ số liệu giám sát, giá trị quá ngưỡng được tô màu, rê chuột để xem min/max. Bên phải là lệnh điều khiển và beam.
- **Cấu hình TRB**: đọc, sửa, ghi và đọc lại để kiểm tra. Đánh dấu nhiều TRB để áp các trường đã sửa cho hàng loạt; các trường khác của từng TRB được giữ nguyên.
- **PSU**: chọn PSU ở hàng nút trên cùng; số liệu thô của 4 cụm DCM, nguồn phụ, RTC và trip code. Bên phải là lệnh điều khiển: mỗi lệnh ghi đè bật/tắt cả 4 cụm (ô chọn = bật) nên luôn có hộp xác nhận. Clear trip làm PSU xóa hết trip code. Giá trị không có ngưỡng trong cấu hình (PEAK, VALLEY, RMS, nhiệt độ FET/XDP, trip) chỉ hiển thị, không so sánh.
- **Cấu hình PSU**: giống cấu hình TRB (đọc, sửa, ghi, đọc lại; hàng loạt). Thanh ghi hiển thị dạng hex, rê chuột để xem mô tả. Nút "Mặc định" điền giá trị hãng gợi ý (OPERATION, ENABLE_FAULTS, MASK_FAULTS, RETRY), hoặc lấy từ `psu_default_config.json` nếu có file này cạnh file chạy.
- **Nạp code**: tab FPGA (kiểm tra kết nối, xóa flash và nạp, boot và xác nhận) và tab STM32 (nạp lần lượt từng PSU).
- **Khung log**: tab Sự kiện và tab Hex thô (bật bằng ô "Hiện bản tin hex").

Trang Cấu hình và Nạp code cần mở khóa chế độ kỹ sư. Mật khẩu mặc định là `admin`, đổi được trong trang Cấu hình. Đây là khóa chống thao tác nhầm, không phải cơ chế bảo mật.

Lưu ý khi vận hành:

- Lệnh điều khiển TRB luôn ghi đè cả PA, Start/Stop và chế độ. Gửi "Clear trip" khi ô Start bỏ trống cũng là ra lệnh Stop.
- Nạp FPGA ở chế độ broadcast tác động tới mọi TRB, và không phát hiện được gói bị mất cho tới bước boot. TRB lỗi cần nạp lại bằng chế độ gửi riêng.
- Nạp STM32 cần hai file ảnh, link cho slot A và slot B. App tự chọn ảnh của slot không đang chạy.

## File cấu hình và dữ liệu

Các file nằm cạnh file chạy và được tạo ở lần chạy đầu.

| File | Nội dung |
|---|---|
| `actl_tool.ini` | Cổng, số lượng thiết bị, timeout, log, hash mật khẩu |
| `thresholds.json` | Ngưỡng min/max theo từng TRB, tự cập nhật mỗi lần đọc cấu hình |
| `psu_thresholds.json` | Như trên, theo địa chỉ PSU |
| `psu_default_config.json` | Tùy chọn: cấu hình mặc định PSU, cùng định dạng file "Lưu file…" của trang Cấu hình PSU |
| `logs/yyyy-MM-dd/trb_*.csv` | Số liệu giám sát, mặc định 10 giây một dòng cho mỗi TRB, ghi ngay khi đổi trạng thái |
| `logs/yyyy-MM-dd/psu_*.csv` | Số liệu giám sát PSU, cùng chu kỳ ghi |
| `logs/yyyy-MM-dd/events_*.csv` | Sự kiện |

Các khóa hay dùng trong `actl_tool.ini`:

| Khóa | Mặc định | Ý nghĩa |
|---|---|---|
| `monitor/address`, `monitor/port` | `0.0.0.0`, `5000` | Địa chỉ PC lắng nghe TCP |
| `monitor/replayFile` | rỗng | Phát lại file hex thay cho TCP để thử không cần phần cứng |
| `service/port`, `service/baud` | rỗng, `1000000` | Cổng COM RS485 |
| `system/staleMs` | `3000` | Quá thời gian này không có bản tin thì coi là mất kết nối |
| `trb/checkCrc` | `true` | Kiểm tra CRC bản tin giám sát và cấu hình TRB |
| `log/periodMs`, `log/maxFileMb` | `10000`, `100` | Chu kỳ ghi và cỡ file log tối đa |
| `fpgaOta/eraseWaitSec` | `420` | Thời gian chờ sau khi xóa flash FPGA |
| `psu/firstAddr`, `psu/count` | `1`, `5` | Dải địa chỉ PSU |
| `psu/checkCrc` | `true` | Kiểm tra CRC bản tin giám sát và cấu hình PSU |
| `auth/lockMinutes` | `10` | Tự khóa chế độ kỹ sư sau thời gian không thao tác |

## Kiến trúc

```
ui/        Giao diện, chỉ đọc model và gọi service
services/  Mỗi chức năng một lớp: trb_monitor, trb_control, trb_config,
           psu_monitor, psu_control, psu_config,
           fpga_ota, stm_ota, alarm_engine, csv_logger
model/     device_store (TRB), psu_store (PSU), thresholds
proto/     Định nghĩa bản tin; field_table mô tả trường dùng chung cho
           giải mã, đóng gói, hiển thị, CSV và ngưỡng
core/      transport (TCP server, serial, phát lại file), frame_parser, link
app/       main, settings, context
tests/     Test cho từng lớp, dùng transport và thiết bị giả lập
```

Mỗi `Link` gồm một transport, bộ tách khung và hàng đợi gửi có ưu tiên, timeout và gửi lại. Các link chạy trên một thread I/O riêng; service và giao diện chạy trên thread chính.

Mở rộng:

- **Thêm cổng**: tạo một transport và một `Link` trong `app/main.cpp`.
- **Thêm chức năng**: viết một service, đăng ký các CMD nhận của nó vào `FrameRegistry`, thêm một trang trong `ui/`.
- **Thêm trường vào bản tin**: thêm một dòng trong bảng trường ở `proto/`.

## Khung bản tin

PSU: giám sát (`81`) và điều khiển (`01`) đi qua TCP như TRB; cấu hình (`03`, `04`, `82`) đi qua RS485. PC không phát bản tin hỏi giám sát `02`. Khi ghi cấu hình, Config Mask luôn gửi `FFFF`.

Mọi bản tin có dạng `AB CD | CMD | địa chỉ | dữ liệu | CRC16 | E1 E2`, độ dài cố định theo CMD, CRC16-CCITT-FALSE, số nhiều byte theo big-endian.

| Bản tin | CMD | Độ dài | CRC tính từ byte |
|---|---|---|---|
| Giám sát TRB | `11 11` | 280 | 4 |
| Điều khiển TRB | `A2 A2` | 14 | 4 |
| Beam TRB | `14 14` | 17 | 4 |
| Ghi cấu hình TRB | `A1 A1` | 520 | 4 |
| Hỏi / trả lời cấu hình TRB | `A3 A3` / `A4 A4` | 10 / 520 | 4 |
| Nạp FPGA: nạp, xóa, hỏi, boot | `55`, `66`, `88`, `AA` (lặp 2 byte) | 269 | 4 |
| Trạng thái nạp FPGA | `99 99` | 25 | 4 |
| Nạp STM32: BEGIN, DATA, END, COMMIT, INFO | `90`, `91`, `92`, `93`, `95` | 20, 268, 8, 8, 8 | 2 |
| Điều khiển PSU | `01` | 12 | 2 |
| Hỏi cấu hình PSU | `03` | 12 | 2 |
| Ghi cấu hình PSU | `04` | 914 | 2 |
| Giám sát PSU | `81` | 265 | 2 |
| Trả lời cấu hình PSU | `82` | 914 | 2 |
| ACK nạp STM32 | `94` | 16 | 2 |

Bản tin TRB dùng CMD 2 byte và địa chỉ 2 byte; bản tin STM32/PSU dùng CMD 1 byte và địa chỉ 1 byte. `FrameRegistry` từ chối đăng ký nếu một CMD 1 byte trùng byte đầu của một CMD 2 byte.

## Việc còn lại

- PSU: ACK cho lệnh điều khiển, bảng trip code, công thức quy đổi và dấu của giá trị 3 byte, định dạng RTC (mili giây 1 byte), tách bit các thanh ghi XDP/ADS/INA.
- ACK cho lệnh điều khiển TRB.
- Công thức quy đổi giá trị thô sang đơn vị vật lý cho TRB; hiện hiển thị số thô.
- Ánh xạ ngưỡng `TEMP_TRB` sang trường giám sát.
- Module mô phỏng TRB/PSU tích hợp trong app.
- Trạng thái "Đang nạp" trên lưới tổng quan khi nạp FPGA.
