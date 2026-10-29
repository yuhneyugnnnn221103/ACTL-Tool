# Bản tin PSU chi tiết

Tài liệu này tổng hợp mọi bản tin liên quan tới PSU trong ACTL Tool: giám sát, điều khiển, cấu hình và nạp code STM32. Bảng trường được sinh trực tiếp từ bảng định nghĩa trong mã nguồn (`proto/psu_*_proto.cpp`, `proto/stm_ota_proto.cpp`) nên khớp với phần mềm.

## 1. Quy ước chung

| Mục | Nội dung |
|---|---|
| Khung | `AB CD` + **CMD 2 byte** + địa chỉ PSU + dữ liệu + **CRC16** (2 byte) + `E1 E2` |
| CRC | CRC16-CCITT-FALSE (đa thức 0x1021, khởi tạo 0xFFFF), **tính từ byte địa chỉ (ngay sau CMD, bỏ CMD ra như TRB) đến hết dữ liệu**, ghi **byte cao trước** |
| Thứ tự byte | Mọi số nhiều byte đều **big-endian** (byte cao trước), **không dấu**, trừ dòng Supply (`I_*`) và ngưỡng dòng Supply có dấu bù 2 |
| Vị trí byte | Trong tài liệu này byte đếm **từ 1**: `AB` = 1, `CD` = 2, CMD = 3-4, địa chỉ = 5 |
| Địa chỉ PSU | 1 byte, mặc định 5 PSU địa chỉ 1 đến 5 (cài đặt `psu/count`, `psu/firstAddr`) |
| Giá trị | Giá trị giám sát là **số thô** (chưa có công thức quy đổi); riêng Supply là thanh ghi INA228 và được dịch phải 4 bit khi hiển thị (mục 2.2) |
| ACK | Điều khiển và ghi cấu hình **không có ACK**; ACK chỉ có trong nạp code STM32 |

### Đường truyền

| Chức năng | Đường | Ghi chú |
|---|---|---|
| Giám sát `81 81` | Ethernet (TCP, app là client, Gateway là server) | Gateway chuyển tiếp lên app |
| Điều khiển `01 01` | Ethernet (cùng đường giám sát) | Ưu tiên cao hơn mọi lệnh khác trong hàng đợi |
| Hỏi/ghi cấu hình `03 03`, `04 04`, `82 82` | Ethernet (cùng đường giám sát) | Hỏi đáp: mỗi lúc chỉ một chức năng cấu hình hoặc nạp code chạy |
| Nạp code STM32 `90`-`95` | Ethernet (cùng đường giám sát) | Lần lượt từng PSU |

### Bảng tổng hợp bản tin

| Bản tin | CMD | Hướng | Độ dài (byte) | Ghi chú |
|---|---|---|---|---|
| Giám sát PSU | `81 81` | PSU → app | 310 | Số liệu 4 cụm DCM, nguồn phụ, RTC, trip |
| Điều khiển PSU | `01 01` | app → PSU | 13 | Bật/tắt 4 cụm, Clear trip; không ACK |
| Hỏi giám sát PSU | `02 02` | app/Gateway → PSU | 13 | Trả lời `81 81`; app có hàm dựng khung nhưng chưa tự gửi |
| Hỏi cấu hình PSU | `03 03` | app → PSU | 13 | Trả lời bằng `82 82` |
| Trả lời cấu hình PSU | `82 82` | PSU → app | 915 | Cùng bố cục khung ghi |
| Ghi cấu hình PSU | `04 04` | app → PSU | 915 | Không ACK; app đọc lại để kiểm tra |
| STM32 BEGIN | `90 90` | app → PSU | 21 | Kích thước, CRC32, version ảnh |
| STM32 DATA | `91 91` | app → PSU | 269 | Mỗi gói 256 byte |
| STM32 END | `92 92` | app → PSU | 9 | PSU kiểm CRC32 toàn ảnh |
| STM32 COMMIT | `93 93` | app → PSU | 9 | Kích hoạt ảnh mới, PSU reset |
| STM32 ACK | `94 94` | PSU → app | 17 | Trả lời ERASE/BEGIN/DATA/END/COMMIT/INFO/ROLLBACK |
| STM32 INFO | `95 95` | app → PSU | 9 | Hỏi slot đang chạy, version và trạng thái trial |
| STM32 ERASE | `96 96` | app → PSU | 9 | Xóa slot đích; một ACK duy nhất khi xóa xong |
| STM32 ROLLBACK | `97 97` | app → PSU | 9 | Quay về slot kia khi slot đang chạy hỏng; có ACK |

Firmware PSU **không tự phát**: chỉ trả lời, và trả ra đúng cổng đã nhận lệnh. Khung hỏi giám sát `02 02` (13 byte: `AB CD 02 02 Addr 00 00 00 00 CRC E1 E2`) do Gateway gửi; app chỉ nhận khung `81 81` Gateway chuyển lên (có sẵn hàm dựng `02 02` trong `proto/psu_monitor_proto`). Khung gửi sai địa chỉ thì PSU bỏ qua, không phản hồi.

## 2. Giám sát `81 81` (310 byte)

Bố cục: `[AB CD][81 81][Addr][CỤM 1..4: 4 x 48 B][SUPPLY 24 B][RTC 7 B][TRIP 10 B][Mở rộng firmware 24 B][CRC 2 B][E1 E2]`

| Byte | Đoạn | Nội dung |
|---|---|---|
| 1-2 | Header | `AB CD` |
| 3-4 | CMD | `81 81` |
| 5 | Địa chỉ | Địa chỉ PSU |
| 6-53 | Cụm 1 | 48 byte (xem bảng bên dưới) |
| 54-101 | Cụm 2 | Cùng bố cục cụm 1, mọi byte cộng thêm 48 |
| 102-149 | Cụm 3 | Cộng thêm 96 |
| 150-197 | Cụm 4 | Cộng thêm 144 |
| 198-221 | Supply | 8 giá trị x 3 byte |
| 222-228 | RTC | 7 byte, mỗi trường 1 byte |
| 229-238 | Trip | 10 trip code, mỗi cái 1 byte |
| 239-262 | Mở rộng firmware | 24 byte (xem 2.5): ACTL chỉ dùng 4 trạng thái an toàn, 4 cờ EN, 4 bộ đếm trip (theo cụm), slot, version; còn lại dự phòng |
| 263-306 | Dự phòng | 44 byte |
| 307-308 | CRC | CRC16 từ byte 5 (địa chỉ) đến byte 306 |
| 309-310 | Đuôi | `E1 E2` |

### 2.1 Một cụm DCM (cụm 1, byte 6-53)

Mỗi cụm có 8 giá trị 3 byte (6 giá trị ADC và 2 giá trị AMC) rồi 12 giá trị 2 byte (XDP). Với cụm n, tên trường là `Cn.<tên>` và vị trí cộng thêm 48 x (n - 1).

| Byte | Dài | Tên trường | Nhãn giao diện |
|---|---|---|---|
| 6-8 | 3 | I_OUT_ADC1 | Dòng Output ADC 1 |
| 9-11 | 3 | TEMP_ADC1 | Nhiệt độ ADC 1 |
| 12-14 | 3 | I_OUT_ADC2 | Dòng Output ADC 2 |
| 15-17 | 3 | TEMP_ADC2 | Nhiệt độ ADC 2 |
| 18-20 | 3 | I_OUT_ADC3 | Dòng Output ADC 3 |
| 21-23 | 3 | TEMP_ADC3 | Nhiệt độ ADC 3 |
| 24-26 | 3 | I_IN_AMC | Dòng Input AMC |
| 27-29 | 3 | V_IN_AMC | Áp Input AMC |
| 30-31 | 2 | V_IN_XDP | Áp XDP (VIN) |
| 32-33 | 2 | V_IN_XDP_PEAK | Áp XDP PEAK (VIN) |
| 34-35 | 2 | V_IN_XDP_VALLEY | Áp XDP VALLEY (VIN) |
| 36-37 | 2 | V_OUT_XDP | Áp XDP (VOUT) |
| 38-39 | 2 | V_OUT_XDP_PEAK | Áp XDP PEAK (VOUT) |
| 40-41 | 2 | V_OUT_XDP_VALLEY | Áp XDP VALLEY (VOUT) |
| 42-43 | 2 | I_OUT_XDP | Dòng tổng XDP (IOUT) |
| 44-45 | 2 | I_OUT_XDP_PEAK | Dòng tổng XDP PEAK (IOUT) |
| 46-47 | 2 | I_OUT_XDP_VALLEY | Dòng tổng XDP VALLEY (IOUT) |
| 48-49 | 2 | I_OUT_XDP_RMS | Dòng tổng XDP RMS (IOUT) |
| 50-51 | 2 | TEMP_FET | Nhiệt độ FET |
| 52-53 | 2 | TEMP_XDP | Nhiệt độ XDP |

### 2.2 Nguồn phụ (Supply, byte 198-221)

Supply lấy từ 4 INA228: INA1 = `I_DCM`/`V_DCM`, INA2 = `I_5V`/`V_5V`, INA3 = `I_3V3_1`/`V_3V3_1`, INA4 = `I_3V3_2`/`V_3V3_2`. Mỗi giá trị là **3 byte thanh ghi CURRENT / VBUS nguyên bản** của INA228, dữ liệu nằm ở **bit 23..4**: giao diện **dịch phải 4 bit** để hiển thị; **dòng (`I_*`) có dấu** (bù 2, 24 bit), áp (`V_*`) không dấu. Kênh INA mất dữ liệu gửi `00 00 00`. App lưu và so ngưỡng bằng giá trị thô đủ 24 bit (cùng đơn vị với ngưỡng Supply trong cấu hình); tooltip ô Supply ghi cả giá trị thô.

| Byte | Dài | Tên trường | Nhãn giao diện |
|---|---|---|---|
| 198-200 | 3 | SUPPLY.I_DCM | Dòng DCM Supply |
| 201-203 | 3 | SUPPLY.V_DCM | Áp DCM Supply |
| 204-206 | 3 | SUPPLY.I_5V | Dòng 5V |
| 207-209 | 3 | SUPPLY.V_5V | Áp 5V |
| 210-212 | 3 | SUPPLY.I_3V3_1 | Dòng 3V3_1 |
| 213-215 | 3 | SUPPLY.V_3V3_1 | Áp 3V3_1 |
| 216-218 | 3 | SUPPLY.I_3V3_2 | Dòng 3V3_2 |
| 219-221 | 3 | SUPPLY.V_3V3_2 | Áp 3V3_2 |

### 2.3 RTC (byte 222-228)

| Byte | Dài | Tên trường | Nhãn giao diện |
|---|---|---|---|
| 222 | 1 | RTC.YEAR | Năm |
| 223 | 1 | RTC.MONTH | Tháng |
| 224 | 1 | RTC.DAY | Ngày |
| 225 | 1 | RTC.HOUR | Giờ |
| 226 | 1 | RTC.MINUTE | Phút |
| 227 | 1 | RTC.SECOND | Giây |
| 228 | 1 | RTC.MS | Mili giây |

### 2.4 Trip code (byte 229-238)

Mỗi trip code là một byte. Bảng bit dưới đây là **TẠM THỜI** theo firmware (chờ bảng chính thức); giao diện hiện ý nghĩa từng bit ở tooltip:

| Byte | Ý nghĩa |
|---|---|
| TRIP1 | bit k = giá trị Supply k vượt **MAX** (k: 0 `I_DCM`, 1 `V_DCM`, 2 `I_5V`, 3 `V_5V`, 4 `I_3V3_1`, 5 `V_3V3_1`, 6 `I_3V3_2`, 7 `V_3V3_2`) |
| TRIP2 | bit k = giá trị Supply k dưới **MIN** |
| TRIP3 | bit c = INA c+1 báo SHNTOL/POL (quá dòng/công suất); bit 4+c = SHNTUL |
| TRIP4 | bit c = BUSOL (quá áp bus); bit 4+c = BUSUL (sụt áp bus) |
| TRIP5 | bit c = TMPOL (quá nhiệt INA); bit 4+c = chân FT của DCM c+1 |
| TRIP6 | bit0 mất liên lạc I²C; bit1 cắt do ALERT nhưng không xác nhận nguyên nhân; bit2 LOCKOUT; bit3 cấu hình INA không hợp lệ |
| TRIP7..10 | 0 |

Trip code **latch** cho tới khi nhận Clear trip (không tự xóa khi giá trị trở lại bình thường).

| Byte | Dài | Tên trường | Nhãn giao diện |
|---|---|---|---|
| 229 | 1 | TRIP1 | Trip code 1 |
| 230 | 1 | TRIP2 | Trip code 2 |
| 231 | 1 | TRIP3 | Trip code 3 |
| 232 | 1 | TRIP4 | Trip code 4 |
| 233 | 1 | TRIP5 | Trip code 5 |
| 234 | 1 | TRIP6 | Trip code 6 |
| 235 | 1 | TRIP7 | Trip code 7 |
| 236 | 1 | TRIP8 | Trip code 8 |
| 237 | 1 | TRIP9 | Trip code 9 |
| 238 | 1 | TRIP10 | Trip code 10 |

### 2.5 Mở rộng firmware (byte 239-262)

Vùng dự phòng 24 byte được firmware APSU dùng làm vùng mở rộng. Offset tính từ byte 239:

| Offset | Byte | Dài | Tên trường | Ý nghĩa |
|---|---|---|---|---|
| +0..+2 | 239-241 | 3 x 1 | `FW.SAFE_STATE1`, `FW.EN1`, `FW.TRIP_COUNT1` | Cụm 1: trạng thái an toàn (0 BOOT, 1 OFF, 2 ARMING, 3 ON, 4 TRIPPED, 5 LOCKOUT), PWR_EN (1/0), số lần trip liên tiếp |
| +3..+5 | 242-244 | 3 x 1 | `FW.SAFE_STATE2`, `FW.EN2`, `FW.TRIP_COUNT2` | Cụm 2 (cùng 3 byte) |
| +6..+8 | 245-247 | 3 x 1 | `FW.SAFE_STATE3`, `FW.EN3`, `FW.TRIP_COUNT3` | Cụm 3 |
| +9..+11 | 248-250 | 3 x 1 | `FW.SAFE_STATE4`, `FW.EN4`, `FW.TRIP_COUNT4` | Cụm 4 |
| +12 | 251 | 1 | `FW.SLOT` | Slot đang chạy `A` / `B` |
| +13..+16 | 252-255 | 4 | `FW.VERSION` | Version ảnh đang chạy (u32 BE) |
| +17..+23 | 256-262 | 7 | | 0 (dự phòng) |

Giao diện hiện các giá trị này ở khung "Trạng thái PSU" (cột phải) trên trang PSU. Hai trạng thái **TRIPPED** và **LOCKOUT** ảnh hưởng tới điều khiển (mục 3).

### 2.5a Khung rỗng (X) và quá hạn (?)

Như TRB: PSU không phản hồi thì Gateway thay bằng **khung rỗng** 310 byte `AB CD 81 81 ADDR`, dữ liệu và CRC toàn `00`, `E1 E2` (giả định cùng quy ước với TRB). Nhận khung rỗng thì ô PSU hiện **X** ngay từ khung đầu, kể cả khi chưa có dữ liệu; đủ `system/emptyFramesToLost` khung rỗng liên tiếp (mặc định 3) thì PSU chuyển Mất kết nối và giữ số liệu cuối. Có bản tin thật thì X mất. Quá `system/staleMs` không có khung mới thì hiện **?**.

### 2.5b Trạng thái từng cụm trên trang tổng quan

Mỗi cụm DCM (và ô Supply) có trạng thái riêng, dựa vào 3 byte theo cụm của vùng mở rộng:

| Điều kiện của cụm | Hiện |
|---|---|
| `safe_state` là TRIPPED hoặc LOCKOUT (kể cả khi chân EN đã về 0) | **Trip** (đỏ) |
| Chân EN tắt và không TRIPPED/LOCKOUT | **OFF** (xám nhạt, chữ "OFF"); không so ngưỡng, không tính vào cảnh báo của PSU |
| Chân EN bật, có giá trị của cụm vượt ngưỡng | **Quá ngưỡng** (cam) |
| Chân EN bật, trong ngưỡng | **Tốt** (xanh) |

- Ô **Supply** luôn so ngưỡng, không phụ thuộc EN.
- Ô tên PSU là **Trip** khi có một cụm TRIPPED/LOCKOUT hoặc có trip code khác 0, còn lại theo cảnh báo của các cụm đang bật và Supply.
- Trip theo cụm tạm lấy từ `safe_state` vì bảng trip code chưa chuẩn; khi có trip code theo từng cụm thì xác định cụm lỗi qua trip code.

### 2.6 Cách app xử lý

- **Trip:** có bất kỳ trip code nào khác 0 thì PSU ở trạng thái Trip (ưu tiên hơn Quá ngưỡng). Trip chưa gán được cho từng cụm.
- **Quá ngưỡng:** giá trị nào có ngưỡng trong cấu hình (xem mục 4.4) được so với ngưỡng đã lưu phía app; vượt thì PSU ở trạng thái Quá ngưỡng. Các giá trị PEAK, VALLEY, RMS, nhiệt độ FET/XDP, RTC, trip và vùng firmware chỉ hiển thị, không so sánh. Giá trị `0` ở một cận ngưỡng nghĩa là **tắt cận đó** (không so phía đó).
- **Mất kết nối:** quá `system/staleMs` (mặc định 3000 ms) không có khung mới thì PSU bị đánh dấu mất kết nối (hiện `?`), số liệu cuối vẫn được giữ.
- **Địa chỉ ngoài dải:** khung có địa chỉ không thuộc `psu/firstAddr` đến `psu/firstAddr + psu/count - 1` bị bỏ và đếm là "địa chỉ sai".
- **CRC:** kiểm CRC mỗi khung, bật tắt bằng `psu/checkCrc` (mặc định bật). Khung sai CRC bị bỏ và đếm vào "Lỗi CRC".

## 3. Điều khiển `01 01` (13 byte)

`[AB CD][01 01][Addr][Mask][ClearTrip][Dự phòng 2 B][CRC 2 B][E1 E2]`

| Byte | Dài | Trường | Ý nghĩa |
|---|---|---|---|
| 1-2 | 2 | Header | `AB CD` |
| 3-4 | 2 | CMD | `01 01` |
| 5 | 1 | Addr | Địa chỉ PSU |
| 6 | 1 | Mask | Bit 0..3 = cụm 1..4; **1 = bật, 0 = tắt**. Mỗi cụm (3 DCM) có một chân EN riêng, bit k điều khiển EN cụm k+1 (firmware APSU hiện tại mới lấy bit0, chờ cập nhật theo cụm). App gửi mask theo 4 ô đã tích |
| 7 | 1 | ClearTrip | `0x01` = PSU xóa các trip code trong bản tin giám sát; `0x00` = không |
| 8-9 | 2 | Dự phòng | `00 00` |
| 10-11 | 2 | CRC | CRC16 từ byte 5 (địa chỉ) đến byte 9 |
| 12-13 | 2 | Đuôi | `E1 E2` |

- Mỗi lệnh **ghi đè trạng thái bật/tắt**, nên giao diện luôn hỏi xác nhận trước khi gửi. Mỗi cụm DCM (3 DCM) có một chân EN riêng; bit k của Mask điều khiển chân EN cụm k+1. Khi chọn PSU, 4 ô "Bật cụm" được điền sẵn theo `FW.EN1..4` bit0. ****
- **ClearTrip = `01`:** đang TRIPPED thì xóa trip code và các cờ, về OFF; **lệnh bật trong cùng khung bị bỏ qua** (phải gửi ở khung sau). App tự gửi Clear trip (tắt) rồi, sau 300 ms, gửi lệnh bật ở khung riêng khi PSU đang TRIPPED. Ở trạng thái khác thì xóa trip code và bật/tắt theo Mask trong một khung.
- **LOCKOUT** (≥ 3 lần trip liên tiếp): mọi lệnh bị bỏ qua, trip code giữ nguyên, chỉ reset phần cứng mới thoát; giao diện chặn gửi và báo rõ.
- Lệnh bật bị từ chối (đang TRIPPED/LOCKOUT hoặc đang nạp code) thì PSU bỏ qua im lặng.
- Không có ACK: app chỉ báo "đã gửi", không biết PSU đã thực hiện hay chưa; kết quả thấy qua bản tin giám sát kế tiếp.

## 4. Cấu hình

### 4.1 Hỏi cấu hình `03 03` (13 byte)

`[AB CD][03 03][Addr][Dự phòng 4 B][CRC 2 B][E1 E2]`: byte 5 là địa chỉ, byte 6-9 là `00 00 00 00`, CRC từ byte 5 (địa chỉ) đến byte 9.

### 4.2 Ghi `04 04` và trả lời `82 82` (915 byte)

`[AB CD][CMD][Addr][Mask 2 B][CỤM 1..4: 4 x 192 B][INA 72 B][Ngưỡng Supply 48 B][Dự phòng 16 B][CRC 2 B][E1 E2]`

| Byte | Đoạn | Nội dung |
|---|---|---|
| 1-2 | Header | `AB CD` |
| 3-4 | CMD | `04 04` (ghi) hoặc `82 82` (trả lời) |
| 5 | Địa chỉ | Địa chỉ PSU |
| 6-7 | Mask | Config Mask; khi ghi luôn gửi `FF FF` (ghi mọi nhóm); khi trả lời không dùng |
| 8-199 | Cụm 1 | 192 byte: ngưỡng ADC/AMC/XDP (60) + cấu hình XDP (38) + cấu hình ADS (94) |
| 200-391 | Cụm 2 | Cùng bố cục, cộng thêm 192 |
| 392-583 | Cụm 3 | Cộng thêm 384 |
| 584-775 | Cụm 4 | Cộng thêm 576 |
| 776-847 | INA | 4 INA x 9 thanh ghi x 2 byte = 72 byte |
| 848-895 | Ngưỡng Supply | 8 giá trị x (MAX, MIN) x 3 byte = 48 byte |
| 896-911 | Dự phòng | 16 byte |
| 912-913 | CRC | CRC16 từ byte 5 (địa chỉ) đến byte 911 |
| 914-915 | Đuôi | `E1 E2` |

Các thanh ghi XDP, ADS, INA hiển thị và nhập dạng **hex** trên giao diện; rê chuột vào trường để xem mô tả (cột Ghi chú).

#### Cụm 1, ngưỡng (byte 8-67)

Mỗi giá trị có hai trường `_MAX` rồi `_MIN`. Tên trường trùng phần sau `Cn.` của trường giám sát tương ứng.

| Byte | Dài | Tên | Ghi chú |
|---|---|---|---|
| 8-10 | 3 | I_OUT_ADC1_MAX | — |
| 11-13 | 3 | I_OUT_ADC1_MIN | — |
| 14-16 | 3 | TEMP_ADC1_MAX | — |
| 17-19 | 3 | TEMP_ADC1_MIN | — |
| 20-22 | 3 | I_OUT_ADC2_MAX | — |
| 23-25 | 3 | I_OUT_ADC2_MIN | — |
| 26-28 | 3 | TEMP_ADC2_MAX | — |
| 29-31 | 3 | TEMP_ADC2_MIN | — |
| 32-34 | 3 | I_OUT_ADC3_MAX | — |
| 35-37 | 3 | I_OUT_ADC3_MIN | — |
| 38-40 | 3 | TEMP_ADC3_MAX | — |
| 41-43 | 3 | TEMP_ADC3_MIN | — |
| 44-46 | 3 | I_IN_AMC_MAX | — |
| 47-49 | 3 | I_IN_AMC_MIN | — |
| 50-52 | 3 | V_IN_AMC_MAX | — |
| 53-55 | 3 | V_IN_AMC_MIN | — |
| 56-57 | 2 | V_IN_XDP_MAX | — |
| 58-59 | 2 | V_IN_XDP_MIN | — |
| 60-61 | 2 | V_OUT_XDP_MAX | — |
| 62-63 | 2 | V_OUT_XDP_MIN | — |
| 64-65 | 2 | I_OUT_XDP_MAX | — |
| 66-67 | 2 | I_OUT_XDP_MIN | — |

#### Cụm 1, cấu hình XDP (hot-swap, byte 68-105)

| Byte | Dài | Tên | Chức năng |
|---|---|---|---|
| 68 | 1 | OPERATION | [0x01] Bật/tắt đầu ra, quy định cơ chế tắt nguồn |
| 69 | 1 | MODE | [0xD1] Chọn mode DCM/ACM, chọn FET trong FDM (bỏ qua trong mode AADM) |
| 70 | 1 | PMBUS_CFG | [0xD0] Chọn địa chỉ slave PMBus mềm |
| 71-72 | 2 | REG_CFG | [0xD3] Khai báo giá trị R shunt, enable/disable bảo vệ OC dựa trên giá trị RMS |
| 73 | 1 | PIN_POLARITY | [0xDA] Cấu hình active High/Low của các chân FAULT, WARN, PWRGD |
| 74-75 | 2 | GPO_CFG | [0xDB] Cấu hình chức năng cho các chân GPO0-3 |
| 76-77 | 2 | I_SNS_CFG | [0xD5] Chọn ngưỡng OC, SOC, dòng khởi động (bỏ qua trong mode AADM) |
| 78-79 | 2 | V_SNS_CFG | [0xD4] Chọn ngưỡng lỗi OVIN, chọn dải đo cho VIN, VOUT (căn cứ để chọn bộ số quy đổi m, b, r) |
| 80-81 | 2 | TURN_OFF_CTRL | [0xE6] Cấu hình cơ chế ngắt và xả cổng GATE của FET khi tắt nguồn hoặc FAULT |
| 82-83 | 2 | VOUT_UV_FAULT_LIMIT | [0x44] Ngưỡng UV cho VOUT báo FAULT |
| 84-85 | 2 | VIN_OV_FAULT_LIMIT | [0x55] Ngưỡng OV cho VIN báo FAULT |
| 86-87 | 2 | VIN_UV_FAULT_LIMIT | [0x59] Ngưỡng UV cho VIN báo FAULT |
| 88-89 | 2 | OT_FAULT_LIMIT | [0x4F] Ngưỡng OT báo FAULT quá nhiệt |
| 90-91 | 2 | ENABLE_FAULTS | [0xDE] Enable/disable cơ chế xử lý FAULT, ngắt FET khi có lỗi tương ứng |
| 92-93 | 2 | MASK_FAULTS | [0xDF] Enable/disable cơ chế báo lỗi qua chân GPO hoặc SMBALERT khi có lỗi tương ứng |
| 94-95 | 2 | RETRY | [0xE7] Cấu hình số lần thử bật lại FET sau khi xảy ra lỗi cùng các tùy chọn thử lại |
| 96 | 1 | PWRGD_DG_TMR | [0xE4] Cấu hình thời gian trễ lọc nhiễu cho tín hiệu PWRGD |
| 97-98 | 2 | TELEMETRY_EN | [0xE8] Enable/disable các kênh đo lường (các lệnh READ) |
| 99-100 | 2 | TELEMETRY_AVG | [0xE9] Cấu hình số lượng lấy mẫu trung bình cho V, I, P |
| 101-102 | 2 | V_TMR | [0xD9] Đặt thời gian trễ xử lý cho lỗi UV, OV, OUV, OVIN |
| 103-104 | 2 | SOA_TMR | [0xE5] Cấu hình thời gian lọc nhiễu OC, điều chỉnh dòng SOA, lấy mẫu dòng RMS |
| 105 | 1 | WATCHDOG_TMR | [0xD8] Cấu hình thời gian cho phép nạp tụ đầu ra và mở FET |

#### Cụm 1, cấu hình ADS (byte 106-199)

Phần chung (14 byte) rồi 8 kênh CH0..CH7, mỗi kênh 5 thanh ghi x 2 byte = 10 byte; kênh CHk có vị trí bằng CH0 cộng thêm 10 x k.

| Byte | Dài | Tên | Chức năng |
|---|---|---|---|
| 106-107 | 2 | MODE | Bật/tắt CRC, chọn data len, SPI timeout, chọn nguồn ngắt, cực tính pinout khi data sẵn sàng/chưa sẵn sàng |
| 108-109 | 2 | CLOCK | Bật/tắt các kênh ADC, chọn nguồn dao động, nguồn REF ngoại, chọn chế độ công suất, OSR (tác động đến tốc độ lấy mẫu) |
| 110-111 | 2 | GAIN1 | Chọn độ lợi PGA gain cho kênh 0, 1, 2, 3 |
| 112-113 | 2 | GAIN2 | Chọn độ lợi PGA gain cho kênh 4, 5, 6, 7 |
| 114-115 | 2 | CFG | Bật/tắt và cấu hình cho Global-Chop (bù offset nội), Current Detect |
| 116-117 | 2 | THRSHLD_MSB | 16 bit trọng số cao của ngưỡng so sánh 24 bit trong chế độ Current Detect |
| 118-119 | 2 | THRSHLD_LSB | 8 bit trọng số thấp của ngưỡng so sánh 24 bit trong chế độ Current Detect và cấu hình cho bộ lọc DC Block |

Kênh CH0 (CH1..CH7 cùng bố cục, tên `CHk_...`):

| Byte | Dài | Tên | Chức năng |
|---|---|---|---|
| 120-121 | 2 | CH0_CFG | Cấu hình độ trễ pha của nguồn clock cấp, cấu hình tắt bộ lọc DC Block, chọn nguồn tín hiệu đầu vào MUX |
| 122-123 | 2 | CH0_OCAL_MSB | 16 bit trọng số cao của giá trị hiệu chuẩn Offset |
| 124-125 | 2 | CH0_OCAL_LSB | 8 bit trọng số thấp của giá trị hiệu chuẩn Offset |
| 126-127 | 2 | CH0_GCAL_MSB | 16 bit trọng số cao của giá trị hiệu chuẩn Gain |
| 128-129 | 2 | CH0_GCAL_LSB | 8 bit trọng số thấp của giá trị hiệu chuẩn Gain |

#### INA (byte 776-847)

4 INA, mỗi INA 9 thanh ghi x 2 byte = 18 byte. Bảng dưới là INA1; INA2 cộng thêm 18, INA3 cộng thêm 36, INA4 cộng thêm 54 (tên `INA2_...`, `INA3_...`, `INA4_...`).

| Byte | Dài | Tên | Ghi chú |
|---|---|---|---|
| 776-777 | 2 | INA1_CONFIG | — |
| 778-779 | 2 | INA1_ADC_CONFIG | — |
| 780-781 | 2 | INA1_SHUNT_CAL | — |
| 782-783 | 2 | INA1_SHUNT_TEMPCO | — |
| 784-785 | 2 | INA1_SOVL | — |
| 786-787 | 2 | INA1_SUVL | — |
| 788-789 | 2 | INA1_BOVL | — |
| 790-791 | 2 | INA1_BUVL | — |
| 792-793 | 2 | INA1_TEMP_LIMIT | — |

#### Ngưỡng Supply (byte 848-895)

| Byte | Dài | Tên | Ghi chú |
|---|---|---|---|
| 848-850 | 3 | I_DCM_MAX | — |
| 851-853 | 3 | I_DCM_MIN | — |
| 854-856 | 3 | V_DCM_MAX | — |
| 857-859 | 3 | V_DCM_MIN | — |
| 860-862 | 3 | I_5V_MAX | — |
| 863-865 | 3 | I_5V_MIN | — |
| 866-868 | 3 | V_5V_MAX | — |
| 869-871 | 3 | V_5V_MIN | — |
| 872-874 | 3 | I_3V3_1_MAX | — |
| 875-877 | 3 | I_3V3_1_MIN | — |
| 878-880 | 3 | V_3V3_1_MAX | — |
| 881-883 | 3 | V_3V3_1_MIN | — |
| 884-886 | 3 | I_3V3_2_MAX | — |
| 887-889 | 3 | I_3V3_2_MIN | — |
| 890-892 | 3 | V_3V3_2_MAX | — |
| 893-895 | 3 | V_3V3_2_MIN | — |

### 4.3 Quy trình đọc và ghi (phía app)

1. **Đọc:** gửi `03 03`, chờ `82 82` (thời gian chờ `config/replyTimeoutMs`, mặc định 200 ms). Hết thời gian thì hỏi lại tối đa **2 lần nữa** (tổng 3 lần). Trả lời sai địa chỉ thì báo lỗi.
2. **Ghi:** (nếu chỉ sửa một số ô thì app đọc cấu hình hiện tại trước, áp thay đổi lên bản đọc được) gửi `04 04` với Mask `FF FF`, chờ `config/writeGapMs` (mặc định 300 ms) cho PSU lưu, rồi **đọc lại** bằng `03 03` (cũng thử tối đa 3 lần).
3. **Kiểm tra:** so từng trường của bản đọc lại với bản vừa ghi; khác thì báo "đọc lại KHÔNG khớp" kèm tối đa 5 tên trường đầu.
4. Làm lần lượt từng PSU. Mật khẩu kỹ sư chỉ được hỏi khi ghi.
6. Nút "Mặc định" điền giá trị hãng gợi ý: `OPERATION` = `0x80`, `ENABLE_FAULTS` = `0x3EFF`, `MASK_FAULTS` = `0x3EFF`, `RETRY` = `0x3FBF`, các trường khác = 0. Nếu có `psu_default_config.json` cạnh file chạy thì giá trị trong file thay cho mặc định cài sẵn.

### 4.4 Ngưỡng cảnh báo phía app

Sau mỗi lần đọc thành công, app lấy các cặp ngưỡng sau từ cấu hình PSU làm ngưỡng cảnh báo (lưu vào `psu_thresholds.json`, khóa theo địa chỉ PSU):

- Mỗi cụm (4 cụm): `I_OUT_ADC1..3`, `TEMP_ADC1..3`, `I_IN_AMC`, `V_IN_AMC`, `V_IN_XDP`, `V_OUT_XDP`, `I_OUT_XDP` (11 giá trị, so với trường giám sát cùng tên `Cn.<tên>`).
- Supply: `I_DCM`, `V_DCM`, `I_5V`, `V_5V`, `I_3V3_1`, `V_3V3_1`, `I_3V3_2`, `V_3V3_2`.

Ngưỡng Supply là 3 byte, so với **giá trị thô cùng đơn vị bản tin giám sát** (chưa dịch phải 4 bit); ngưỡng dòng (`I_*`) **có dấu 24 bit**; giá trị `0` = tắt cận đó (cả MAX lẫn MIN). Firmware PSU cũng tự so các ngưỡng này (chỉ khi SAFE_ON, cần 2 mẫu liên tiếp, ~60 ms/mẫu) và latch trip code.

Ghi chú theo firmware cho nhóm INA: `SHUNT_CAL` = 0 nghĩa là dùng giá trị mặc định của INA đó; bit RST/RSTACC của `CONFIG` bị firmware xóa; ngưỡng dưới `SUVL`, `BUVL` chỉ được nạp vào INA khi PSU ở SAFE_ON (chưa kiểm chứng trên board).

## 5. Nạp code STM32 (`90`-`97`)

Mã lệnh là một byte **lặp hai lần** ở phần CMD (ví dụ BEGIN là `90 90`). CRC phủ từ byte địa chỉ (sau CMD).

### 5.1 BEGIN `90 90` (21 byte)

`[AB CD][90 90][Addr][Size 4 B][CRC32 4 B][Version 4 B][CRC 2 B][E1 E2]`

| Byte | Dài | Trường | Ý nghĩa |
|---|---|---|---|
| 5 | 1 | Addr | Địa chỉ PSU |
| 6-9 | 4 | Size | Kích thước ảnh (byte) |
| 10-13 | 4 | CRC32 | CRC32 (IEEE 802.3) của toàn ảnh |
| 14-17 | 4 | Version | Version ảnh (mặc định app lấy thời điểm hiện tại) |

### 5.2 DATA `91 91` (269 byte)

`[AB CD][91 91][Addr][Seq 2 B][Len 2 B][Data 256 B][CRC 2 B][E1 E2]`

- Byte 6-7: số thứ tự gói (từ 0); byte 8-9: số byte hợp lệ trong gói; byte 10-265: dữ liệu. Gói cuối đệm `0xFF`.

### 5.3 END `92 92`, COMMIT `93 93`, INFO `95 95` (9 byte)

`[AB CD][CMD CMD][Addr][CRC 2 B][E1 E2]`

- END: yêu cầu PSU kiểm CRC32 toàn ảnh. COMMIT: kích hoạt ảnh mới (PSU reset). INFO: hỏi slot đang chạy, version và trạng thái trial. **ERASE `96 96`:** xóa slot đích. **ROLLBACK `97 97`:** đánh dấu slot đang chạy hỏng rồi reset để quay về slot kia (có ACK; chỉ khi SAFE_OFF và slot kia khởi động được).

### 5.4 ACK `94 94` (17 byte)

`[AB CD][94 94][Addr][AckCmd][Status][Info 4 B][Slot][Trial][CRC 2 B][E1 E2]`

| Byte | Dài | Trường | Ý nghĩa |
|---|---|---|---|
| 5 | 1 | Addr | Địa chỉ PSU |
| 6 | 1 | AckCmd | Mã 1 byte của lệnh được trả lời (`90`..`93`, `95`, `96`, `97`) |
| 7 | 1 | Status | Mã trạng thái (bảng dưới) |
| 8-11 | 4 | Info | Tùy lệnh: ERASE `1` = đã xóa xong; DATA lỗi sai thứ tự = seq cuối đã nhận; END lỗi CRC = CRC thiết bị tính được; INFO = version; lỗi xóa = mã HAL |
| 12 | 1 | Slot | `A` hoặc `B`; **`R` khi thiết bị ở bootloader recovery** (cả hai slot hỏng). Chỉ có nghĩa với INFO |
| 13 | 1 | Trial | Chỉ có ở trả lời INFO: bit7 đã confirm, bit6..0 boot_count |

| Status | Tên | Ý nghĩa |
|---|---|---|
| 0 | OK | Thành công |
| 1 | Refused | Từ chối (thiết bị chưa ở SAFE_OFF) |
| 2 | BadSize | Ảnh quá lớn so với slot |
| 3 | BadSeq | Sai thứ tự gói |
| 4 | CrcFail | CRC32 không khớp |
| 5 | Busy | Thiết bị đang bận |
| 6 | NoCommitPending | Chưa có END thành công để commit |
| 7 | EraseFail | Xóa sector thất bại |
| 8 | WrongSlot | Ảnh được link cho slot khác |
| 9 | ProgFail | Lỗi ghi flash (info = mã HAL / địa chỉ) |
| 10 | NoTarget | ROLLBACK: slot kia không khởi động được |
| 11 | NotErased | BEGIN khi chưa ERASE |

### 5.5 Quy trình nạp (lần lượt từng PSU)

Stop-and-wait: app chờ ACK rồi mới gửi khung kế.

1. **INFO** (chờ 1 s): biết PSU đang chạy slot nào và trạng thái trial. Nạp vào slot **còn lại**: đang chạy A thì nạp ảnh slot B, và ngược lại; cần chọn file cho slot đích. Ở **recovery (`R`)** bootloader xóa cả hai slot; app chọn ảnh slot A nếu có, không thì ảnh slot B (giả định, chưa kiểm chứng).
2. **ERASE** (chờ tối đa 20 s): PSU trả **một ACK duy nhất khi xóa xong** (`Info = 1`). `EraseFail` hoặc trạng thái khác `OK` thì dừng.
3. **BEGIN** (chờ 2 s): size, CRC32, version; PSU ACK ngay. Gửi BEGIN mà chưa ERASE thì PSU trả `NotErased`.
4. **DATA** từng gói 256 byte, khung luôn 269 byte, gói cuối đệm `FF` (chờ ACK 800 ms; hết giờ thì gửi lại, tối đa 5 lần). `BadSeq` thì nhảy theo seq PSU báo (`Info + 1`), tối đa 5 lần. `ProgFail` thì dừng.
5. **END** (chờ 3 s): PSU kiểm CRC32; sai thì báo kèm CRC thiết bị tính được.
6. **COMMIT** (chờ 2 s): có tùy chọn tự kích hoạt ngay khi CRC32 đúng, hoặc hỏi người dùng trước. Hết giờ chờ ACK COMMIT được coi là thành công có điều kiện (PSU thường reset trước khi trả lời); nên đọc lại INFO để xác nhận.
7. Hủy giữa chừng không ảnh hưởng firmware đang chạy; slot đích có thể còn dữ liệu dở.

**ROLLBACK** (nút "Rollback (về slot kia)"): gửi `97 97`, chờ ACK 3 s; `OK` nghĩa là PSU đã đánh dấu slot đang chạy hỏng và đang reset; `NoTarget` nghĩa là slot kia không khởi động được; `Refused` nghĩa là PSU chưa ở SAFE_OFF.

Cần mật khẩu kỹ sư để nạp và rollback; PSU phải ở SAFE_OFF mới chấp nhận.

## 6. Khác biệt đã biết với firmware

- CRC tính từ byte địa chỉ (ngay sau CMD), giống TRB. Firmware có macro `PSU_CRC_INCLUDE_CMD`: đặt `0` để khớp (CRC không gồm CMD); app dùng `crcStart = 4` cho mọi khung PSU và STM32.
- RTC và 4 cụm DCM: firmware chưa có nguồn dữ liệu nên gửi `00`; giao diện vẫn hiện (toàn 0).
- Bảng trip code là tạm thời (mục 2.4).
- Cổng nào là đường Gateway/dự phòng của PSU chưa chốt (firmware hiện nhận mọi lệnh ở cả 3 cổng).
