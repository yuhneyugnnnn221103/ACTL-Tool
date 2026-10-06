#pragma once
// Điều khiển PSU, CMD 2 byte 01 01, 13 byte, CRC phủ từ byte CMD:
//   [AB CD][01 01][Addr][Mask][ClearTrip][Dự phòng 2B][CRC][E1 E2]
// Mask: 4 bit thấp = 4 cụm DCM, 1 = bật, 0 = tắt (mỗi lệnh ghi đè trạng thái cả 4 cụm).
// ClearTrip = 0x01: PSU xóa các trip code trong bản tin giám sát. Không có ACK.
#include <QByteArray>

namespace proto::psuctl {

constexpr int kLength = 13, kOffAddr = 4, kOffMask = 5, kOffClearTrip = 6;

struct ControlCmd {
    quint8 clusterMask = 0; // bit0..3 = cụm 1..4
    bool clearTrip = false;
};

QByteArray cmd();   // 01 01
QByteArray buildControl(int addr, const ControlCmd &c);

} // namespace proto::psuctl
