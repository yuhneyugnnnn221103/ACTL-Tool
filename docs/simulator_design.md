# Thiết kế module giả lập TRB/PSU tích hợp trong app

Trạng thái: **bản thiết kế, chưa code**. Các điểm cần chốt nằm ở mục 9.

## 1. Mục tiêu

- Chạy app mà không cần Gateway hay phần cứng: có đủ 20 MB × 8 TRB và 5 PSU phản hồi như thiết bị thật.
- Thay `tools/mock_gateway.py` cho các việc hằng ngày (xem giao diện, demo, test tự động), vì giả lập dùng chính bộ mã hóa/giải mã của app nên giao thức đổi là giả lập tự theo.
- Bơm được lỗi theo yêu cầu để test: vượt ngưỡng, trip, mất kết nối, nhấp nháy, nhiễu, rớt/hỏng/chậm bản tin RS485.
- Chạy được trong `ctest` (không cần cổng COM, không cần socket) để test luồng cấu hình, debug, nạp code từ đầu đến cuối.

Không làm: mô phỏng vật lý (nhiệt, dòng thật), mô phỏng thời gian xóa flash thật (7 phút) trừ khi chọn "thời gian thật".

## 2. Vị trí trong kiến trúc

App hiện có hai đường truyền, mỗi đường một `Link` (thread I/O riêng):

| Link | Thật | Chiều dữ liệu |
|---|---|---|
| `monitor` | TCP, PC là server, Gateway kết nối vào | Gateway đẩy `11 11` / `81 81`; PC gửi điều khiển `A2 A2`, `14 14`, `01 01` |
| `service` | RS485 (cổng COM) | PC hỏi - thiết bị đáp: cấu hình `A1/A3/A4`, `03/04/82`, nạp FPGA `55..99`, nạp STM32 `90..95` |

Giả lập chèn ở tầng **`core::Transport`**, tức dưới `Link`, nên toàn bộ phần còn lại của app (parser, services, store, UI) chạy y như với thiết bị thật:

```
UI → services → Link ──► SimTransport ──► SimWorld ──► TrbModel ×160 / PsuModel ×5
                  ▲            │
                  └── bytes ◄──┘   (khung hợp lệ, CRC đúng, qua FrameParser như thật)
```

Tương tự `ReplayTransport` đã có, nhưng hai chiều và có trạng thái.

## 3. Thành phần (thư mục `sim/`)

| Lớp | Việc |
|---|---|
| `SimWorld` | Sở hữu toàn bộ thiết bị giả, đồng hồ mô phỏng, nguồn ngẫu nhiên (seed cố định để lặp lại được). Sống trên thread I/O. |
| `TrbModel` | Trạng thái 1 TRB: giá trị giám sát (vector theo `trbmon::table()`), cấu hình 520 B (theo `trbcfg::table()`), cờ debug, trip code, trạng thái nạp FPGA. Hàm `handle(frame) → trả lời hoặc rỗng`. |
| `PsuModel` | Tương tự cho PSU: 4 cụm DCM, supply, RTC, trip, cấu hình 915 B, trạng thái nạp STM32 (BEGIN/DATA/END/COMMIT/INFO, 4 slot). |
| `Scenario` | Hành vi theo thời gian của từng thiết bị: bình thường, cảnh báo, trip, mất kết nối, nhấp nháy, nhiễu. Ghi dưới dạng danh sách `(thiết bị, điều kiện, hiệu ứng)`, nạp từ JSON; có bộ dựng sẵn tương ứng các kịch bản của mock Python (`normal, warning, trip, leds, lost, flap, noise, stress`). |
| `FaultInjector` | Lớp bọc quanh đường trả lời: rớt, hỏng CRC, trễ, cắt khung, chèn byte rác. Áp cho từng link. |
| `SimTransport` | Lớp con của `core::Transport`, có hai vai: `Monitor` (tự đẩy khung giám sát theo chu kỳ, nhận lệnh điều khiển) và `Service` (nhận yêu cầu, trả lời sau một độ trễ có thể chỉnh). Hai vai dùng chung một `SimWorld`. |

Tất cả dùng lại `proto/*`: giá trị giám sát đưa qua `FieldTable::encode`, khung dựng bằng `FrameRegistry`/hàm `build*` có sẵn, nên không có bảng offset thứ hai để lệch với app. Chỗ thiếu: các hàm dựng khung giám sát (`11 11`, `81 81`), khung trả lời (`A4 A4`, `82 82`, `99 99`, ACK STM32); các hàm này cần thêm vào `proto/` (cũng dùng được cho test).

## 4. Hành vi theo từng chức năng

**Giám sát.** Vai `Monitor` đẩy khung `11 11` cho từng TRB và `81 81` cho từng PSU như Gateway: chu kỳ chu trình (mặc định 1 s cho cả hệ thống, tùy chỉnh), rải đều thay vì bơm cả 160 khung cùng lúc. Giá trị lấy từ `TrbModel`/`PsuModel` cộng nhiễu nhỏ. TRB mất kết nối thì ngừng đẩy khung của nó (app tự chuyển "mất kết nối" theo timeout).

**Điều khiển.** `A2 A2` đổi trạng thái `TrbModel` (mask PA, start/stop, debug, clear trip); `01 01` đổi mask cụm DCM và xóa trip của `PsuModel` (đúng quy ước: clear trip thì xóa hết trip code). Lệnh sai địa chỉ thì bỏ qua.

**Luật Debug.** Giả lập cố ý **không tự chặn** hai TRB cùng debug: nó phát hiện và ghi cờ "xung đột bus" (trong ngữ cảnh thật hai TRB cùng tự đẩy khung sẽ trùng nhau). Nhờ đó test được rằng app (`TrbControl::check`) không bao giờ để chuyện đó xảy ra. Việc TRB đang debug tự đẩy khung giám sát lên đường nào cần xác nhận với phần cứng (mục 9, điểm 6).

**Cấu hình.** `A3 A3`/`03 03` trả `A4 A4`/`82 82` từ cấu hình đang giữ; `A1 A1`/`04 04` ghi vào `Model`, không có ACK (đúng giao thức). Có tùy chọn "ghi lỗi" để test luồng đọc lại và so sánh (xem mục 6).

**Nạp FPGA.** Máy trạng thái theo `99 99`: nhận erase → bận trong `T` giây (thời gian giả lập, có chế độ nhanh) → nhận gói theo thứ tự, ghi `CODE/STAT/BOOTSTS/WBSTAR`; có thể bơm lỗi ghi flash ở gói N, gói mất, UART frame error; boot đổi `BOOTSTS`.

**Nạp STM32.** BEGIN → DATA (kiểm seq, CRC khối) → END → COMMIT → INFO, ACK 17 B theo `stm_ota_proto`; có thể bơm NAK/timeout/CRC lỗi.

## 5. Luồng và thread

- `SimWorld` và hai `SimTransport` sống trên **thread I/O** cùng `Link`, nên không cần khóa khi `Link` gọi `write()` / nhận `bytesReceived`.
- UI không chạm trực tiếp mô hình; gọi qua `QMetaObject::invokeMethod(world, ..., Qt::QueuedConnection)` (đặt TRB vào trạng thái nào đó, đổi kịch bản, bật lỗi) và nhận tín hiệu thống kê về qua signal.
- Khung phản hồi luôn phát qua `QTimer::singleShot(0 hoặc độ trễ)`, không phát ngay trong `write()` (lỗi đã gặp ở `FakeTransport`: trả lời ngay trong `write` lẫn với việc `Link` đang gửi).

## 6. Bơm lỗi và kiểm thử

Danh sách lỗi cấu hình được qua UI, JSON hoặc API test:
- đường truyền: rớt N%, hỏng CRC N%, trễ `a..b` ms, cắt khung giữa chừng, byte rác chen giữa;
- thiết bị: không trả lời, trả lời sai địa chỉ, trả lời cấu hình khác với giá trị vừa ghi (test "đọc lại KHÔNG khớp"), TRB trip/vượt ngưỡng/mất/nhấp nháy, PSU cụm DCM lỗi;
- nạp code: lỗi ghi flash, mất gói, NAK.

Test mới (`tests/sim_test.cpp`, chạy trong `ctest`, không cần mạng): dựng `Link + services + SimTransport`, kiểm
1. giám sát: 160 TRB + 5 PSU lên `DeviceStore`/`PsuStore` đúng giá trị;
2. điều khiển, luật debug một TRB, clear trip;
3. đọc - ghi - đọc lại cấu hình, retry 3 lần khi trả lời rớt, báo lệch khi giả lập ghi sai;
4. nạp FPGA 4 bước (có và không có bước xóa), nạp STM32;
5. bơm lỗi rồi kiểm app báo đúng (mất kết nối, quá ngưỡng, trip).

## 7. Giao diện

- Khung cấu hình ở dòng trạng thái trên cùng: nút "Nguồn dữ liệu": **Thiết bị thật** (hiện tại) / **Giả lập**. Khi giả lập: pill "Giả lập" màu riêng ở thanh trên để không nhầm với thiết bị thật, nhất là trước khi ghi cấu hình hoặc nạp code.
- Trang **Giả lập** (chỉ hiện khi bật): bảng chọn kịch bản, danh sách TRB/PSU với nút "ép trạng thái" (tốt, vượt ngưỡng, trip, mất), thanh trượt tỉ lệ rớt/hỏng/trễ của từng link, bộ đếm khung đã gửi/nhận.
- Ghi chú nạp code: ở chế độ giả lập, thời gian chờ xóa mặc định rút còn vài giây.

## 8. Kế hoạch theo giai đoạn

| GĐ | Nội dung | Kết quả |
|---|---|---|
| 1 | `SimWorld`, `TrbModel`, `PsuModel`, vai `Monitor`, hàm dựng khung giám sát, bật bằng `--sim` hoặc `sim/enabled` trong ini | App có đủ 160 TRB + 5 PSU giám sát được; điều khiển, debug chạy |
| 2 | Vai `Service` cho cấu hình TRB/PSU, hai chiều | Đọc/ghi/xác minh cấu hình hàng loạt |
| 3 | Nạp FPGA, nạp STM32 | Chạy thử 4 bước nạp, lỗi nạp |
| 4 | `Scenario` từ JSON + trang Giả lập + `FaultInjector` | Tái hiện các kịch bản của mock Python, bơm lỗi từ UI |
| 5 | `tests/sim_test.cpp` hoàn chỉnh; quyết định giữ hay bỏ mock Python | Test đầu-cuối trong `ctest` |

Mỗi giai đoạn đều chạy được và có test riêng, có thể dừng giữa chừng.

## 9. Điểm cần chốt

1. **Chọn nguồn dữ liệu.** Đề xuất: cả hai cách. Tham số `--sim` / khóa ini để chạy tự động và test; ở UI có nút đổi (đổi thì đóng và dựng lại hai link, vì `Link` sở hữu transport). Nếu chỉ chọn lúc khởi động thì đơn giản hơn, nhưng phải khởi động lại app mỗi lần đổi.
2. **Giữ `tools/mock_gateway.py` hay bỏ.** Đề xuất: giữ ở GĐ 1-4 (nó còn kiểm được TCP/COM thật và là đối chiếu độc lập với mã C++), quyết định bỏ ở GĐ 5.
3. **Thời gian giả lập.** Đề xuất: đồng hồ mô phỏng chạy theo thời gian thật, có hệ số tăng tốc cho riêng thời gian chờ xóa flash.
4. **Phạm vi tối thiểu cho lần đầu.** Đề xuất làm GĐ 1 và GĐ 2 trước (giám sát, điều khiển, cấu hình), nạp code để sau.
5. **Số liệu giả lập.** Đề xuất giữ phân bố giá trị y như mock Python hiện nay (đã quen khi xem giao diện); khi có công thức quy đổi vật lý sẽ cập nhật ở một chỗ.
6. **TRB ở chế độ Debug tự gửi khung giám sát** trên đường nào (RS485 hay qua Gateway) và theo chu kỳ bao nhiêu? Cần để giả lập đúng và để app nhận ra TRB nào đang debug từ khung giám sát thay vì chỉ theo lệnh đã gửi.
