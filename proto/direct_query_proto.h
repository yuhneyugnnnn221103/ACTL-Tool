#pragma once
// Lệnh hỏi giám sát gửi thẳng xuống thiết bị trong chế độ Debug COM (không có Gateway tự hỏi và đẩy bản tin giám sát lên).
//   TRB: AB CD 23 23 | MB | TRB | dự phòng 2 byte | CRC | E1 E2            (12 byte), trả lời là bản tin giám sát 23 23 (310 byte)
//   PSU: AB CD 02 02 | địa chỉ PSU | dự phòng 4 byte | CRC | E1 E2         (13 byte), trả lời là bản tin giám sát PSU (psumon::cmd())
// CRC16 tính từ byte địa chỉ (byte thứ 5) tới hết dự phòng, như mọi bản tin khác.
#include <QByteArray>

namespace proto::directq {

QByteArray trbCmd();   // 23 23
QByteArray psuCmd();   // 02 02
constexpr int kTrbLength = 12, kPsuLength = 13;

QByteArray buildTrb(int mb, int trb);
QByteArray buildPsu(int addr);

} // namespace proto::directq
