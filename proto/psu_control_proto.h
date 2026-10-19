#pragma once
// Điều khiển PSU, CMD 2 byte 01 01, 13 byte, CRC phủ từ byte CMD:
//   [AB CD][01 01][Addr][Mask][ClearTrip][Dự phòng 2B][CRC][E1 E2]
// Mask: 4 bit thấp = 4 cụm DCM, 1 = bật, 0 = tắt (mỗi lệnh ghi đè trạng thái). Firmware APSU hiện chỉ có MỘT chân PWR_EN (đóng cả 4 DCM)
// và chỉ lấy bit0; bit 1..3 bị bỏ qua nên app gửi 0x0F khi bật, 0x00 khi tắt.
// ClearTrip = 0x01: PSU xóa các trip code. Đang TRIPPED thì lệnh bật trong cùng khung bị bỏ qua (phải gửi lệnh bật ở khung sau);
// LOCKOUT (>= 3 lần trip liên tiếp) bỏ qua mọi lệnh. Không có ACK.
#include <QByteArray>

namespace proto::psuctl {

constexpr int kLength = 13, kOffAddr = 4, kOffMask = 5, kOffClearTrip = 6;
constexpr quint8 kMaskOn = 0x0F, kMaskOff = 0x00;

struct ControlCmd {
    quint8 clusterMask = 0; // bit0..3 = cụm 1..4
    bool clearTrip = false;
};

QByteArray cmd();   // 01 01
QByteArray buildControl(int addr, const ControlCmd &c);

} // namespace proto::psuctl
