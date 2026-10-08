# Thiết kế module giả lập TRB/PSU

Trạng thái: **bản thiết kế, chưa code**. Điểm còn thiếu thông tin nằm ở mục 9.

## 1. Mục tiêu và vị trí

Module giả lập là **một chương trình riêng chạy trên PC** (`actl_sim`), cắm vào phía thiết bị của Gateway qua cổng COM RS485 hoặc Ethernet và đóng vai 160 TRB + 5 PSU. App ACTL Tool **không đổi**: nó vẫn nói chuyện với Gateway như với hệ thống thật.

```
ACTL Tool  <──TCP (monitor) + RS485 (service)──>  Gateway  <──COM RS485 / Ethernet──>  actl_sim
 (hiển thị, điều khiển,                          (tự hỏi vòng,                         (160 TRB + 5 PSU giả,
  cấu hình, nạp code)                             chuyển tiếp)                          kịch bản, bơm lỗi)
```

- Gateway tự hỏi vòng giám sát; `actl_sim` trả lời bằng khung giám sát (`11 11`, `81 81`) của từng thiết bị, Gateway chuyển lên app.
- Điều khiển, cấu hình, nạp code đi từ app qua Gateway xuống `actl_sim`; `actl_sim` xử lý và trả lời; Gateway chuyển lên app.
- Khung hỏi/đáp giữa Gateway và thiết bị giống khung app thấy (xác nhận của bạn), nên dùng lại nguyên bộ mã hóa/giải mã trong `proto/`.

Mục đích: kiểm tra cả chuỗi **app + Gateway** khi chưa đủ phần cứng, tái hiện lỗi hiện trường, test tải (160 TRB + 5 PSU), test nạp code. Khác với `tools/mock_gateway.py`: mock Python đóng vai **Gateway** nói với app; `actl_sim` đóng vai **thiết bị** nói với Gateway thật.

Không làm: mô phỏng vật lý (nhiệt, dòng thật); đóng vai Gateway (đã có mock Python).

## 2. Tái sử dụng mã hiện có

`actl_sim` là chương trình Qt console (không Widgets) trong cùng repo, dùng chung với app:

| Dùng lại | Dùng làm gì |
|---|---|
| `core/` (`Transport`, `SerialTransport`, `TcpServerTransport`, `FrameParser`, `Link`) | Nhận byte, tách khung, kiểm CRC; thêm `TcpClientTransport` |
| `proto/*` (bảng trường, `FieldTable::encode`, `build*`) | Dựng khung giám sát/cấu hình/trả lời, đọc yêu cầu từ Gateway. Chỗ thiếu: hàm dựng `11 11`, `81 81`, `A4 A4`, `82 82`, `99 99`, ACK STM32 (thêm vào `proto/`, cũng dùng cho test) |

Giao thức đổi trong `proto/` thì app và giả lập đổi theo, không có bảng offset thứ hai để lệch.

## 3. Cấu trúc

| Lớp | Việc |
|---|---|
| `SimWorld` | Sở hữu 160 `TrbModel` + 5 `PsuModel`, đồng hồ mô phỏng, nguồn ngẫu nhiên (seed cố định để lặp lại). |
| `TrbModel` | Giá trị giám sát (theo `trbmon::table()`), cấu hình 520 B (theo `trbcfg::table()`), cờ debug, trip code, trạng thái nạp FPGA. Hàm `handle(khung) → trả lời hoặc rỗng`. |
| `PsuModel` | 4 cụm DCM, supply, RTC, trip, cấu hình 915 B, trạng thái nạp STM32 (BEGIN/DATA/END/COMMIT/INFO). |
| `BusPort` | Một cổng nối với Gateway: transport (COM hoặc TCP) + `FrameParser`. Nhận khung hỏi/điều khiển/cấu hình, chuyển cho `SimWorld`, gửi trả lời. Có thể mở một hoặc hai cổng (mục 9). |
| `Scenario` | Hành vi theo thời gian: bình thường, cảnh báo, trip, mất kết nối, nhấp nháy, nhiễu, stress. Dựng sẵn các kịch bản của mock Python; tùy chỉnh bằng JSON. |
| `FaultInjector` | Trên đường trả lời: rớt, hỏng CRC, trễ, cắt khung, byte rác. Cấu hình theo cổng và theo thiết bị. |

`actl_sim` chạy trên một thread sự kiện duy nhất (timer + I/O), nên không cần khóa giữa `SimWorld` và các cổng.

## 4. Hành vi theo chức năng

**Giám sát.** Gateway hỏi từng TRB/PSU; `actl_sim` trả khung giám sát từ `TrbModel`/`PsuModel` cộng nhiễu nhỏ. TRB đặt "mất kết nối" thì không trả lời nên Gateway báo timeout và app chuyển sang "mất kết nối" theo cơ chế sẵn có.

**Điều khiển.** `A2 A2` đổi trạng thái `TrbModel` (mask PA, start/stop, debug, clear trip); `01 01` đổi mask cụm DCM và xóa trip của `PsuModel` (clear trip xóa hết trip code). Địa chỉ không tồn tại thì bỏ qua.

**Luật Debug.** TRB đang debug tự gửi khung giám sát không cần Gateway hỏi. `actl_sim` bắt chước điều này (chu kỳ cấu hình được) và **đếm xung đột** nếu có từ hai TRB trở lên cùng debug, để test rằng app (`TrbControl::check`) không bao giờ để chuyện đó xảy ra.

**Cấu hình.** `A3 A3`/`03 03` trả `A4 A4`/`82 82` từ cấu hình đang giữ; `A1 A1`/`04 04` ghi vào model, không có ACK (đúng giao thức). Có tùy chọn "ghi sai" để test luồng đọc lại và so sánh, và "bỏ trả lời" để test thử lại 3 lần.

**Nạp FPGA.** Máy trạng thái theo `99 99`: erase → bận `T` giây (có hệ số tăng tốc) → nhận gói theo thứ tự, cập nhật `CODE/STAT/BOOTSTS/WBSTAR`; có thể bơm lỗi ghi flash ở gói N, mất gói, UART frame error; boot đổi `BOOTSTS`. Hỗ trợ cả broadcast (`FF/FF`: nhận mà không trả lời) và gửi riêng.

**Nạp STM32.** BEGIN → DATA (kiểm seq, CRC) → END → COMMIT → INFO, ACK 17 B theo `stm_ota_proto`; có thể bơm NAK, timeout, CRC lỗi.

## 5. Điều khiển giả lập

Ở GĐ đầu chỉ dùng dòng lệnh và file kịch bản, không có giao diện đồ họa:

```
actl_sim --port COM5 --baud 1000000              # RS485
actl_sim --tcp 192.168.1.10:5000                 # Ethernet, kết nối tới Gateway (hoặc --listen 5000)
         --scenario normal|warning|trip|lost|flap|noise|stress|file.json
         --drop 2 --corrupt 1 --delay 5..30       # bơm lỗi đường truyền (% và ms)
         --seed 42 --fast-erase 5                 # lặp lại được; rút thời gian chờ xóa flash
```

Trong lúc chạy có một dòng lệnh tương tác (stdin): `trip 1 3`, `lost 2 5`, `over 1 3 TRM1.I SEN2`, `fault drop 10`, `status`, `scenario trip`. Giao diện đồ họa nhỏ là việc sau nếu cần.

## 6. Kiểm thử

- `tests/sim_test.cpp` (chạy trong `ctest`): nối `Link + services` của app trực tiếp vào `SimWorld` qua một transport trong bộ nhớ, không cần Gateway, COM hay mạng. Kiểm giám sát 160 TRB + 5 PSU, điều khiển, luật debug, đọc-ghi-đọc lại cấu hình (kể cả retry và lệch), nạp FPGA/STM32, bơm lỗi và xem app báo đúng.
- Kiểm tay với Gateway thật: chạy `actl_sim` phía thiết bị, mở app phía người dùng, xem toàn chuỗi.

## 7. Kế hoạch

| GĐ | Nội dung | Kết quả |
|---|---|---|
| 1 | Thư viện `SimWorld`/model, `BusPort` (COM + TCP), trả lời giám sát, điều khiển, debug; hàm dựng khung giám sát trong `proto/`; dòng lệnh cơ bản | Gateway thật hỏi vòng, app thấy 160 TRB + 5 PSU |
| 2 | Cấu hình TRB/PSU đọc/ghi/xác minh | Cấu hình hàng loạt qua chuỗi thật |
| 3 | Nạp FPGA, nạp STM32 | Chạy thử 4 bước nạp và các lỗi nạp |
| 4 | Kịch bản JSON, `FaultInjector`, dòng lệnh tương tác | Tái hiện kịch bản hiện trường |
| 5 | `tests/sim_test.cpp` đầy đủ trong `ctest` | Test đầu-cuối tự động |

Mỗi giai đoạn chạy được và có test riêng; GĐ 5 có thể làm song song từ GĐ 1 để test mỗi khi thêm chức năng.

## 8. Quyết định đã chốt

- Module là chương trình PC riêng, nối phía thiết bị của Gateway qua COM RS485 hoặc Ethernet.
- Khung hỏi giám sát giống khung app thấy.
- Gateway tự hỏi vòng đều.
- App không đổi; không làm module giả lập trong app.

## 9. Cần xác nhận trước khi code

1. **Khung Gateway hỏi giám sát**: app chỉ thấy khung trả lời (`11 11`, `81 81`), chưa thấy khung hỏi. Khung hỏi dài bao nhiêu, CMD gì, mang địa chỉ nào? Nếu khung hỏi chính là khung giám sát rỗng cùng CMD thì `actl_sim` chỉ cần nhận khung cùng CMD rồi trả lời, nhưng cần xác nhận.
2. **Số cổng phía thiết bị của Gateway**: một cổng chung cho giám sát lẫn cấu hình/nạp code, hay hai cổng như phía app (monitor + service)? `actl_sim` sẽ mở một hoặc hai `BusPort` tương ứng.
3. **Ethernet**: `actl_sim` là client (tự nối tới Gateway) hay server (Gateway nối tới)? Cổng và địa chỉ?
4. **Gateway gửi gì xuống thiết bị với lệnh điều khiển**: chuyển nguyên khung `A2 A2` / `01 01` của app, hay đổi dạng? Gateway có chờ trả lời (ACK) từ thiết bị khi điều khiển không? (App hiện chưa có ACK cho điều khiển.)
5. **Địa chỉ trên bus**: 20 MB × 8 TRB đánh địa chỉ (MB, TRB) trong khung như app thấy, và 5 PSU địa chỉ 1 byte 1..5 (hay dải khác)? `actl_sim` mặc định theo cấu hình `mbCount/trbPerMb/psuFirstAddr/psuCount` của app.
6. **TRB debug** tự gửi khung giám sát lên cổng nào của Gateway và chu kỳ bao nhiêu (cần cho mục luật Debug ở trên).
