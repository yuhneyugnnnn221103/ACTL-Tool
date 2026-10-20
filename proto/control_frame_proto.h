#pragma once
// Khung điều khiển tổng PC -> Gateway, độ dài cố định 105 byte, không CRC tổng và không đuôi:
//   [AB CD EF][Mask 1B][A2: 14B][14: 17B][Beam: 42B][PSU: 13B][Dự phòng 14B][Đệm 1B]
//   Mask: bit0 = bản tin điều khiển TRB A2, bit1 = TRB 14 (beam TRB), bit2 = điều khiển beam, bit3 = điều khiển PSU.
//   Mỗi lần gửi chỉ phần có lệnh được điền (nguyên khung con, giữ header, CRC, đuôi riêng của nó) và đặt bit mask tương ứng;
//   mọi phần còn lại đệm 0x00 và bit mask về 0.
#include <QByteArray>

namespace proto::ctlframe {

enum Section { SecTrbControl = 0, SecTrbBeam = 1, SecBeam = 2, SecPsu = 3, kSections = 4 };

constexpr quint8 kCmd = 0xEF;
constexpr int kOffMask = 3;
constexpr int kSectionLength[kSections] = {14, 17, 42, 13};
constexpr int kReserved = 14;                       // dự phòng cho một module sau này
constexpr int kOffSection0 = 4;
constexpr int kLength = 105;                        // 4 + 14 + 17 + 42 + 13 + 14 = 104, thêm 1 byte đệm cuối cho đủ 105
constexpr quint8 kPad = 0x00;

int sectionOffset(Section s);
// Đặt khung con vào đúng phần của nó trong khung 105 byte (rỗng nếu độ dài khung con không khớp).
QByteArray wrap(Section s, const QByteArray &inner);
// Tách khung con ra từ khung tổng; trả về rỗng nếu bit mask của phần đó không bật hoặc khung tổng sai độ dài.
QByteArray section(const QByteArray &frame, Section s);

} // namespace proto::ctlframe
