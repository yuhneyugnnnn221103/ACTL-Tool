#pragma once
// Điều khiển beam (chỉ gửi, không có trả lời), đi chung đường Ethernet giám sát/điều khiển. Một lần gửi là 42 byte = 3 khung 14 byte nối nhau:
//   khung 14 byte: [AB CD][ESP 2B][BETA 2B][FREQ 2B][MODE 2B][CRC 2B][EF FF]   (byte đếm từ 1: ESP 3-4, BETA 5-6, FREQ 7-8, MODE 9-10, CRC 11-12)
//   Ba khung có cùng dữ liệu, chỉ khác byte 10 của MODE, lần lượt 0x01, 0x10, 0x11; mỗi khung có CRC và đuôi riêng.
//   Riêng khung này gửi little endian (byte thấp trước) cho mọi trường 2 byte, kể cả CRC.
//   ESP, BETA: số có dấu bù 2, 1 chữ số sau dấu phẩy, gửi giá trị x 10
//   FREQ: số nguyên MHz (không dấu)
//   MODE: byte 9 = số nguyên 0..255 (do người dùng nhập, giống nhau ở cả 3 khung); byte 10 = 0x01 / 0x10 / 0x11 theo thứ tự khung
//   CRC16-CCITT-FALSE tính từ HEADER (AB CD) tới hết MODE (byte 1..10), ghi byte thấp trước; riêng khung beam tính như vậy. Đuôi khung là EF FF (khác E1 E2 của các khung khác).
// Khi gửi, 42 byte này được đặt vào phần "điều khiển beam" của khung điều khiển tổng 107 byte (control_frame_proto.h).
#include <QByteArray>
#include <QString>

namespace proto::beameth {

constexpr int kFrameLength = 14, kFrameCount = 3, kLength = kFrameLength * kFrameCount;   // kLength = 42 byte mỗi lần gửi
constexpr double kAngleMax = 3276.7, kAngleMin = -3276.8;   // giới hạn của số 16 bit có dấu chia 10
constexpr int kFreqMax = 65535, kModeValueMax = 255;

struct Params {
    double esp = 0, beta = 0;          // đơn vị như người dùng nhập (một chữ số sau dấu phẩy)
    int freqMhz = 0;
    int modeValue = 0;                 // byte 9 của MODE
};

quint8 modeByte(int index);            // byte 10 của khung thứ index (0..2): 0x01, 0x10, 0x11
qint16 toTenths(double v);             // làm tròn v x 10 tới số nguyên gần nhất

// Rỗng nếu hợp lệ, ngược lại là câu báo lỗi.
QString validate(const Params &p);
// Dựng bản tin 42 byte (đã kiểm tra hợp lệ); trả rỗng nếu tham số không hợp lệ.
QByteArray buildFrame(const Params &p);

} // namespace proto::beameth
