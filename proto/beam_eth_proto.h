#pragma once
// Điều khiển beam qua đường Ethernet riêng (chỉ gửi, không có trả lời), 14 byte:
//   [AB CD][ESP 2B][BETA 2B][FREQ 2B][MODE 2B][CRC 2B][EF FF]      (byte đếm từ 1: ESP 3-4, BETA 5-6, FREQ 7-8, MODE 9-10, CRC 11-12)
//   ESP, BETA: số có dấu bù 2, 1 chữ số sau dấu phẩy, gửi giá trị x 10 (big-endian)
//   FREQ: số nguyên MHz (big-endian, không dấu)
//   MODE: byte 9 = số nguyên 0..255; byte 10 = A0 -> 0x01, A1 -> 0x10, A2 -> 0x11
//   CRC16-CCITT-FALSE tính từ ESP tới hết MODE (byte 3..10), big-endian. Đuôi khung là EF FF (khác E1 E2 của các khung khác).
#include <QByteArray>
#include <QString>

namespace proto::beameth {

constexpr int kLength = 14;
constexpr double kAngleMax = 3276.7, kAngleMin = -3276.8;   // giới hạn của số 16 bit có dấu chia 10
constexpr int kFreqMax = 65535, kModeValueMax = 255;

enum class ModeSel { A0, A1, A2 };

struct Params {
    double esp = 0, beta = 0;          // đơn vị như người dùng nhập (một chữ số sau dấu phẩy)
    int freqMhz = 0;
    int modeValue = 0;                 // byte 9
    ModeSel mode = ModeSel::A0;        // byte 10
};

QString modeName(ModeSel m);           // "A0", "A1", "A2"
quint8 modeByte(ModeSel m);            // 0x01, 0x10, 0x11
qint16 toTenths(double v);             // làm tròn v x 10 tới số nguyên gần nhất

// Rỗng nếu hợp lệ, ngược lại là câu báo lỗi.
QString validate(const Params &p);
// Dựng khung (đã kiểm tra hợp lệ); trả khung rỗng nếu tham số không hợp lệ.
QByteArray buildFrame(const Params &p);

} // namespace proto::beameth
