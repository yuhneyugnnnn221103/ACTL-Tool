# ACTL Tool

Phần mềm PC (Qt 6, C++17, QWidget) giám sát, điều khiển, cấu hình và nạp code cho hệ thống gồm 20 mother board × 8 TRB và 5 PSU, kết nối qua một Gateway.

## Tình trạng

| Chức năng | Tình trạng |
|---|---|
| Giám sát TRB (bản tin 310 byte), lưới tổng quan 20 × 8 | Xong |
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
PC ── TCP (Gateway là server, PC là client) ── Gateway ── MB 1..20 ── TRB 1..8
PC ── RS485 (cổng COM)  ──    │
                              └────── PSU
```

| Đường | Dùng cho |
|---|---|
| TCP, link `monitor` | Giám sát và điều khiển. App nối tới Gateway (tự nối lại khi mất kết nối). Gateway tự hỏi từng thiết bị rồi chuyển bản tin lên PC, mỗi lần gửi dồn 8 bản tin TRB của một MB |
| RS485, link `service` | Cấu hình và nạp code, hỏi-đáp, mỗi lúc một thao tác |

Địa chỉ TRB là cặp (MB 0–19, TRB 0–7) trên đường truyền, hiển thị là MB1–MB20, TRB1–TRB8; `FF FF` là broadcast.

## Biên dịch

Cần Qt 6 (Core, Network, SerialPort, Widgets, Test), CMake ≥ 3.16 và trình biên dịch C++17.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

Test nằm trong `tests/` (cần Qt Test): `trb_test` (bản tin, tách khung, giám sát, cảnh báo, điều khiển, cấu hình với TRB giả lập, ngưỡng, log CSV) và `psu_test` (phần PSU). Dùng chung transport giả ở `tests/test_support.h`.

File chạy là `build/actl_tool`.

## Sử dụng

- **Tổng quan**: mỗi ô là một TRB, tô màu theo trạng thái (tốt, quá ngưỡng, trip, mất kết nối, chưa có dữ liệu). Bấm vào ô để mở trang chi tiết.
  Bên dưới lưới TRB là lưới PSU 5 × 4: mỗi hàng một PSU, mỗi cột một cụm DCM. Ô cụm tô theo giá trị của chính cụm đó (quá ngưỡng, mất kết nối...); ô tên PSU tô theo trạng thái cả PSU, kể cả Trip. Trip chưa gán được cho từng cụm vì chưa có bảng trip code. Bấm vào ô để mở trang PSU. PSU bất thường cũng hiện trong danh sách bên phải.
- **Chi tiết TRB**: trip code 1-4 báo vượt max I_SEN1-8 của TRM1-4, 5-6 vượt max dòng PA (5: TRM1-2, 6: TRM3-4), 7 System 1 (nhiệt độ TRM, V/I TRB), 9 System 2 (Pulse/Period của TXEN và BSYNC, max/min), 10-15 tương tự 1-6 nhưng dưới min, 8, 16 dự phòng; rê chuột vào ô bit để xem ý nghĩa. Còn lại: toàn bộ số liệu giám sát, giá trị quá ngưỡng được tô màu, rê chuột để xem min/max. Bên phải là lệnh điều khiển và beam.
- **Cấu hình TRB**: đọc, sửa, ghi và đọc lại để kiểm tra (chỉ bước ghi hỏi mật khẩu). "Đọc" chỉ cập nhật cột "Trên thiết bị" và giữ nguyên cột "Giá trị mới"; ô ở cột "Trên thiết bị" khác ô tương ứng trong "Giá trị mới" được tô vàng. Nút "Lấy từ thiết bị" chép cột "Trên thiết bị" sang "Giá trị mới" làm điểm xuất phát khi sửa. Đánh dấu nhiều TRB rồi "Ghi toàn bộ" để ghi nguyên cột "Giá trị mới" (từ thiết bị, file hoặc mặc định), ghi đè cấu hình của từng TRB. Sau khi ghi, tool gửi bản tin đọc để kiểm tra; hết thời gian chờ thì hỏi lại tối đa 2 lần nữa.
- **PSU**: chọn PSU ở hàng nút trên cùng; số liệu thô của 4 cụm DCM, nguồn phụ, RTC và trip code. Bên phải là lệnh điều khiển: mỗi lệnh ghi đè bật/tắt cả 4 cụm (ô chọn = bật) nên luôn có hộp xác nhận. Clear trip làm PSU xóa hết trip code. Giá trị không có ngưỡng trong cấu hình (PEAK, VALLEY, RMS, nhiệt độ FET/XDP, trip) chỉ hiển thị, không so sánh.
- **Cấu hình PSU**: giống cấu hình TRB (đọc, sửa, ghi, đọc lại; hàng loạt bằng "Ghi toàn bộ"; hết thời gian chờ khi đọc lại thì hỏi lại tối đa 2 lần nữa). Thanh ghi hiển thị dạng hex, rê chuột để xem mô tả. Nút "Mặc định" điền giá trị hãng gợi ý (OPERATION, ENABLE_FAULTS, MASK_FAULTS, RETRY), hoặc lấy từ `psu_default_config.json` nếu có file này cạnh file chạy.
- **Chi tiết TRB** có thêm: INIT DATA (6 ô: eMMC Init = bit 0, DDR3 Calib = 1, eMMC Config = 2, Array Calib = 4, Amp Calib = 5, LUT = 6; xám = 0, xanh = 1), "Bản tin lỗi" (tổng của 12 bộ đếm 2 byte, nằm phía trên bên phải thẻ PA, bấm để mở bảng số bản tin từng loại: Beam, Calib PG, Config ADAR, ghi LUT, Control, tra LUT, dịch pha/biên, ghi / kiểm tra calib toàn mảng, ghi config, hỏi cấu hình, hỏi giám sát; tên đặt trong `proto::trbmon::errName`) và 4 thẻ số thô Period/Pulse của TXEN và BEAMSYNC.
- **Cỡ cửa sổ**: cửa sổ nhỏ nhất 1100 x 560; mỗi trang nằm trong vùng cuộn nên màn hình nhỏ hơn kích thước thiết kế thì cuộn, không tràn ra ngoài màn hình. Thanh trên cùng một hàng: thẻ đường kết nối, ô kết nối của trang, bên phải là các thẻ đếm tốt / quá ngưỡng / trip / mất kết nối.
- **Kết nối theo trang**: thanh trên cùng chỉ hiện đường kết nối mà trang đang mở dùng: Tổng quan, Chi tiết TRB, PSU hiện đường giám sát (Ethernet tới Gateway); Cấu hình TRB, Cấu hình PSU, Nạp code hiện đường RS485; Beam Ethernet hiện đường beam; Giả lập tự có cổng COM riêng nên không hiện ô nào. Bên trái thanh, ngay trước ô kết nối, có một thẻ trạng thái của đường mà trang đang mở dùng (màu cho biết đã kết nối / đang nối / đóng); bấm vào thì thả ra trạng thái đủ 4 đường: Giám sát (ETH), Beam (ETH), Cấu hình (485) và RS485 #2 (dành cho chức năng chưa bổ sung). Trang Giả lập không có đường riêng nên thẻ hiện "Các cổng".
- **Beam Ethernet**: trang riêng điều khiển beam qua một đường Ethernet khác (PC là TCP client, nhập IP/cổng ở thanh trên cùng rồi bấm Kết nối; mặc định `beam/address` 192.168.1.20, `beam/port` 6000). Nhập Esp, Beta (số thập phân có dấu, 1 chữ số sau dấu phẩy, bản tin gửi giá trị x 10 dạng 16 bit bù 2), Freq (MHz, số nguyên), Mode byte 9 (số nguyên 0..255) và Mode byte 10 (chọn một trong A0 = 0x01, A1 = 0x10, A2 = 0x11). Bản tin 14 byte `AB CD | ESP | BETA | FREQ | MODE | CRC16 (từ ESP tới MODE) | EF FF`, **little endian** (byte thấp trước) cho Esp, Beta, Freq và CRC (riêng khung này, các khung khác vẫn byte cao trước), không chờ trả lời; ô hex phía dưới cho xem trước đúng bản tin sẽ gửi.
- **Nạp code**: tab FPGA (chọn nhanh TRB: chọn tất cả, bỏ chọn, đảo chọn, theo từng MB; 4 bước riêng: kiểm tra kết nối, xóa flash, nạp code, boot và xác nhận; nạp khi chưa xóa thì có cảnh báo; cột "Phiên bản code" đọc từ bản tin `99 99`. Nạp xong không lỗi thì ghi version mới bằng bản tin `44 44` rồi hỏi lại để đối chiếu. Tên file .bin toàn chữ số, đặt theo ngày `YYYYMMDD` (cùng ngày thì thêm số thứ tự cuối, ví dụ `2026101302.bin`); tên được đổi thành số nguyên (≤ 4294967294), ghi 4 byte big-endian và hiển thị lại đúng số đó. Tên có chữ hoặc quá lớn thì không ghi version) và tab STM32 (nạp lần lượt từng PSU).
- **Ghi log CSV**: ở góc khung log có nút "Ghi log" (không màu khi không ghi; nền xanh và chữ "Đang ghi log" khi đang ghi; bấm để bật/tắt, rê chuột xem giờ hẹn) và nút "Hẹn giờ…" (chọn thời điểm bắt đầu và dừng, mỗi mốc dùng một lần). Khi dừng, mọi file log (kể cả sự kiện) được đóng và không ghi thêm.
- **Khung log**: tab Sự kiện và tab Hex thô (bật bằng ô "Hiện bản tin hex"). Mỗi trang chỉ hiện log của mình: Chi tiết TRB hiện giám sát, điều khiển, beam và sự kiện của TRB đang xem; Cấu hình TRB hiện khung `A1/A3/A4`; PSU hiện `81 81` và `01 01`; Cấu hình PSU hiện `03 03`, `04 04`, `82 82`; Nạp code hiện khung nạp FPGA, STM32 và tiến độ nạp; Tổng quan hiện tất cả. Lỗi kết nối và lỗi đường truyền hiện ở mọi trang. File CSV `events.csv` vẫn ghi đủ mọi sự kiện.

Mọi trang đều xem được không cần mật khẩu, kể cả đọc cấu hình, kiểm tra kết nối FPGA và đọc slot STM32. Mật khẩu chế độ kỹ sư chỉ được hỏi khi ghi cấu hình xuống TRB/PSU (ghi một thiết bị hoặc hàng loạt), xóa flash FPGA, nạp FPGA, boot FPGA, nạp STM32. Mật khẩu mặc định là `admin`, đổi được ở nút "Đổi mật khẩu" trong trang Cấu hình; sau `auth/lockMinutes` phút không thao tác thì hỏi lại, hoặc bấm "Khóa". Đây là khóa chống thao tác nhầm, không phải cơ chế bảo mật.

Lưu ý khi vận hành:

- Chỉ một TRB được ở chế độ Debug (TRB Debug tự phát bản tin giám sát, hai TRB sẽ tranh bus). App từ chối Debug cho "Tất cả TRB"; đặt Debug cho TRB thứ hai thì hỏi có chuyển không (TRB cũ về Normal, giữ nguyên PA và Start). TRB đang Debug có viền xanh trên lưới tổng quan và nhãn "Debug: MBx / TRBy" ở trang chi tiết. App chỉ biết các lệnh nó đã gửi trong phiên này: TRB đã ở Debug từ trước (hoặc do tool khác đặt) thì không biết được, nên sau khi mở app nên gửi Normal cho "Tất cả TRB" nếu không chắc.
- Lệnh điều khiển TRB luôn ghi đè cả PA, Start/Stop và chế độ. Gửi "Clear trip" khi ô Start bỏ trống cũng là ra lệnh Stop.
- Nạp FPGA (cả broadcast lẫn gửi riêng) gửi liên tục mọi gói, chỉ hỏi trạng thái `88` từng TRB một lượt sau gói cuối, nên lỗi ghi flash hay gói bị mất chỉ được phát hiện ở lượt hỏi đó (không thử gửi lại gói). Broadcast tác động tới mọi TRB. TRB lỗi cần nạp lại bằng chế độ gửi riêng.
- Nạp STM32 cần hai file ảnh, link cho slot A và slot B. App tự chọn ảnh của slot không đang chạy.

## File cấu hình và dữ liệu

Các file nằm cạnh file chạy và được tạo ở lần chạy đầu.

| File | Nội dung |
|---|---|
| `actl_tool.ini` | Cổng, số lượng thiết bị, timeout, log, hash mật khẩu |
| `thresholds.json` | Ngưỡng min/max theo từng TRB, tự cập nhật mỗi lần đọc cấu hình |
| `psu_thresholds.json` | Như trên, theo địa chỉ PSU |
| `psu_default_config.json` | Tùy chọn: cấu hình mặc định PSU, cùng định dạng file "Lưu file…" của trang Cấu hình PSU |
| `logs/<phiên>/trb/MB03_TRB05.csv` | Số liệu giám sát, mỗi TRB một file riêng (MB, TRB đếm từ 1), mặc định mỗi bản tin nhận được ghi một dòng (`log/minIntervalMs` = 0); `<phiên>` là thư mục đặt tên theo giờ bắt đầu lần ghi (`yyyy-MM-dd_HH-mm-ss`), mỗi lần bấm "Ghi log", hẹn giờ hoặc mở app là một thư mục mới chứa đủ log TRB, PSU và sự kiện của lần đó. File chỉ được tạo khi TRB có dữ liệu |
| `logs/<phiên>/psu/PSU2.csv` | Số liệu giám sát PSU, mỗi PSU một file, cùng chu kỳ ghi |
| `logs/<phiên>/events.csv` | Sự kiện |

Các khóa hay dùng trong `actl_tool.ini`:


## Giả lập bản tin để thử giao diện

`tools/mock_gateway.py` (chỉ cần Python 3) bơm bản tin giả vào app, không cần phần cứng:

```
python3 tools/mock_gateway.py thresholds --dir build          # ghi ngưỡng mẫu cạnh file chạy (làm trước khi mở app)
python3 tools/mock_gateway.py tcp --scenario all --rate 2      # giả Gateway: là TCP server cổng 5000; trong app nhập IP máy này rồi Kết nối
python3 tools/mock_gateway.py serial --drop 0.2                # RS485 giả (pty) để thử đọc/ghi cấu hình
python3 tools/mock_gateway.py hex replay.hex --scenario trip   # file hex cho monitor/replayFile
python3 tools/mock_gateway.py list                             # các kịch bản
python3 tools/mock_gateway.py selftest
```

Kịch bản: `normal`, `warning` (vượt ngưỡng), `trip` (các trip code TRB và PSU), `leds` (ADAR/PG/PA lỗi), `lost` (thiết bị im lặng rồi hồi phục), `flap`, `noise` (rác, sai CRC, khung cụt hoặc chia mảnh, địa chỉ ngoài dải), `stress`, `all`. Lệnh điều khiển và beam mà app gửi xuống được in ra ở cửa sổ chạy lệnh `tcp`.

## Giao diện

Style Fusion cộng một file QSS duy nhất (`resources/style.qss`, nạp từ resource khi khởi động). Bảng màu khai báo một chỗ ở `ui/theme.h`; QSS dùng token `@tên@` và được thay màu khi nạp. Nút phụ và nút nguy hiểm đặt bằng thuộc tính `role` (`theme::setRole(widget, "secondary" | "danger")`). Đèn LED, ô trạng thái và lưới tổng quan tự vẽ bằng QPainter (`ui/led_indicator`, `ui/status_pill`, `ui/overview_grid`, `ui/psu_grid`). Màu trạng thái cố định ở `theme::statusColor`. Ô lưới tổng quan và đèn LED chỉ dùng màu (không ký hiệu trong ô); các nhãn trạng thái dạng chữ ở thanh trên cùng có kèm ký hiệu. Trip code hiện thành 8 ô bit mỗi byte (`ui/bit_cells`).

## Kiến trúc

```
ui/        Giao diện, chỉ đọc model và gọi service
services/  Mỗi chức năng một lớp: trb_monitor, trb_control, trb_config,
           psu_monitor, psu_control, psu_config,
           fpga_ota, stm_ota, alarm_engine, csv_logger
model/     device_store (TRB), psu_store (PSU), thresholds
proto/     Định nghĩa bản tin; field_table mô tả trường dùng chung cho
           giải mã, đóng gói, hiển thị, CSV và ngưỡng
core/      transport (TCP client, serial, phát lại file), frame_parser, link
app/       main, settings, context
tests/     Test cho từng lớp, dùng transport và thiết bị giả lập
```

Mỗi `Link` gồm một transport, bộ tách khung và hàng đợi gửi có ưu tiên, timeout và gửi lại. Các link chạy trên một thread I/O riêng; service và giao diện chạy trên thread chính.

Mở rộng:

- **Thêm cổng**: tạo một transport và một `Link` trong `app/main.cpp`.
- **Thêm chức năng**: viết một service, đăng ký các CMD nhận của nó vào `FrameRegistry`, thêm một trang trong `ui/`.
- **Thêm trường vào bản tin**: thêm một dòng trong bảng trường ở `proto/`.

## Kết nối mạng với Gateway

Gateway là TCP server, app là TCP client. Nhập IP và cổng của Gateway ở thanh trên cùng rồi bấm "Kết nối"; app cũng tự nối khi mở, và tự nối lại mỗi 2 giây nếu mất kết nối. IP và cổng được nhớ trong `actl_tool.ini` (`monitor/gatewayAddress`, `monitor/gatewayPort`).

Card mạng của PC cần cùng dải mạng với Gateway, ví dụ Gateway `192.168.1.10` thì PC đặt IP tĩnh `192.168.1.20`, mặt nạ `255.255.255.0` (cắm dây thẳng không cần gateway mặc định hay DNS). Kiểm tra bằng `ping 192.168.1.10`. App chỉ nối ra ngoài nên thường không bị tường lửa hỏi.

## Giả lập TRB (tab "Giả lập", `actl_sim_cli`)

Giả lập đóng vai 20 MB × 8 TRB ở **phía thiết bị của Gateway**, nối qua một cổng COM RS485 (xem `docs/simulator_design.md`); không có kết nối Ethernet.

- **Tab "Giả lập"** trong ACTL Tool: chọn cổng COM (khác cổng RS485 cấu hình ở thanh trên cùng) và baud, bấm "Bắt đầu". Mục điều hướng đổi thành "Giả lập (đang chạy)" để không nhầm với thiết bị thật. Lưới 20 × 8 cho thấy từng TRB (OK, Mất, Ép giá trị, Trip, Debug); kéo chuột hoặc bấm tiêu đề MB/TRB để chọn nhiều, rồi bấm "Mất kết nối", "Trở lại", "Bình thường", ép một trường giám sát để thử vượt ngưỡng, đặt trip code theo từng bit. Ô "Bơm lỗi đường truyền" chỉnh tỉ lệ bỏ trả lời, sai CRC và độ trễ ngay khi đang chạy. Cấu hình được nhớ trong `actl_tool.ini` (`sim/*`).
- **`actl_sim_cli`** (dòng lệnh, cho script và máy không có màn hình), dùng chung mô hình với tab Giả lập:

```
actl_sim_cli --serial COM5 --baud 1000000
             [--mb 20 --trb-per-mb 8 --seed 1 --debug-period 1000 --drop 2 --corrupt 1 --delay 5..30 --hex]
```

Gateway hỏi `AB CD 11 11 MB TRB 00 00 CRC E1 E2`, giả lập trả khung giám sát 310 byte của TRB đó. Điều khiển `A2 A2`, beam `14 14`, ghi `A1 A1` và hỏi `A3 A3` cấu hình được xử lý (không có ACK); TRB bật Debug tự gửi khung giám sát mỗi giây. Bit của `INIT_ADAR` theo mặt nạ ADAR của lệnh beam gần nhất và bit của `PA` theo mặt nạ PA của lệnh điều khiển gần nhất (1 bật, 0 tắt). Bản dòng lệnh có các lệnh khi chạy (gõ `help`): `lost 1 3`, `over 1 2 "TRM1.I SEN2" 2500`, `trip 1 2 1 0x04` (MB, TRB đếm từ 1 như trên giao diện), `faults drop 10`, `status`. Chưa có PSU, nạp FPGA/STM32 và kịch bản JSON. Test: `sim_test`.

- TRB mất kết nối vẫn giữ số liệu, đèn, trip code và ô quá ngưỡng cuối cùng nhận được (dòng "Cập nhật n s trước" cho biết đã cũ bao lâu). Trên lưới tổng quan, TRB/PSU quá hạn không nhận được bản tin mới được đánh dấu `?`, nền giữ màu theo số liệu cuối với ưu tiên Trip (đỏ) > Quá ngưỡng (cam) > Tốt (xanh lá); không có màu xám riêng cho mất kết nối. Chú thích ghi "Mất kết nối (trước đó: Trip)".

- PG (Power Good) của TRM1..4: bit nào của 4 bit thấp byte PG bằng 0 thì TRB được tính là Trip (kèm dòng sự kiện "mất PG TRMn" và chú thích ở lưới tổng quan). Mọi bảng dữ liệu tô so le màu dòng; thời gian "Cập nhật ... trước" hiện dạng giờ:phút:giây.

- Trip code hiện 8 ô theo thứ tự bit 0 (LSB) ở trái đến bit 7 (MSB) ở phải. Các thẻ đếm trạng thái ở thanh trên đếm theo điều kiện, một TRB có thể tính vào nhiều thẻ (vừa trip vừa quá ngưỡng; mất kết nối nhưng trước đó đang trip hoặc quá ngưỡng), nên tổng có thể vượt số TRB.

- Mỗi dòng trong khung Sự kiện bắt đầu bằng mã loại: `[TRIP]` vào trạng thái Trip, `[WARN]` giá trị vượt ngưỡng, `[OK]` trở lại bình thường, `[PG]` Power Good, `[CTRL]` lệnh điều khiển/beam đã gửi, `[CFG]` đọc/ghi cấu hình, `[DBG]` chế độ Debug, `[OTA]` nạp code, `[NET]` kết nối Gateway/RS485, `[ERR]` lỗi, `[AUTH]` khóa/mở khóa kỹ sư. Dòng sự kiện tô màu theo mã (TRIP/PG đỏ, WARN cam, OK xanh lá, CTRL xanh dương, CFG xanh ngọc, DBG/OTA tím, NET xám, ERR đỏ đậm, AUTH nâu; TRIP, PG, WARN có nền nhạt). Sự kiện TRB/PSU mất kết nối và có lại kết nối không được ghi (xem trạng thái ở lưới tổng quan và thẻ trạng thái); nếu TRB có lại tin mà đang Trip hoặc quá ngưỡng thì vẫn ghi sự kiện đó. Trang Chi tiết TRB hiện đủ các thẻ trạng thái của TRB đang xem (Trip, Quá ngưỡng, Mất kết nối cùng lúc).

- TRB không phản hồi, sai CRC hoặc lỗi bản tin: Gateway thay bằng **khung rỗng** 310 byte `AB CD 11 11 MB TRB`, phần data và CRC toàn `0x00`, rồi `E1 E2` (vẫn nằm trong lượt gửi 8 TRB của MB). App nhận khung này không cần CRC, biết đúng TRB nào im lặng và đánh dấu mất kết nối (giữ số liệu cuối) sau `system/emptyFramesToLost` khung rỗng liên tiếp (mặc định 3); có bản tin thật trở lại thì hồi phục và đếm lại từ đầu. TRB chưa từng có số liệu giữ "chưa có dữ liệu". `system/staleMs` (mặc định 3000 ms) vẫn là chốt chặn khi không nhận được gì cho TRB đó.

- Số MB và TRB hiển thị trên giao diện, trong sự kiện, chú thích, tên file và cột `mb`, `trb` của file CSV đếm từ **1** (MB1..MB20, TRB1..TRB8). Trên đường truyền (địa chỉ trong bản tin), file ngưỡng `thresholds.json` (khóa `"mb/trb"`) và mã nguồn vẫn đếm từ 0.

## Việc còn lại

- Giả lập: PSU, nạp FPGA/STM32, kịch bản JSON (xem `docs/simulator_design.md`).

- PSU: ACK cho lệnh điều khiển, bảng trip code, công thức quy đổi và dấu của giá trị 3 byte, định dạng RTC (mili giây 1 byte), tách bit các thanh ghi XDP/ADS/INA.
- ACK cho lệnh điều khiển TRB.
- Công thức quy đổi giá trị thô sang đơn vị vật lý cho TRB; hiện hiển thị số thô.
- Trạng thái "Đang nạp" trên lưới tổng quan khi nạp FPGA.
