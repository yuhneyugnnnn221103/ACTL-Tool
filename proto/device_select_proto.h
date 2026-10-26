#pragma once
// Byte chọn thiết bị cho các bản tin PC -> Gateway thuộc nhóm cấu hình và nạp code (đường RS485):
//   [AB CD][DEV][CMD ...][...][CRC][E1 E2]
//   DEV nằm ngay sau AB CD và trước CMD: 0x01 = TRB, 0x02 = PSU, 0x03 = TRSU (sau này).
//   Gateway đọc DEV để biết gửi xuống thiết bị nào, rồi bỏ byte này đi, ghép lại bản tin gốc (AB CD CMD ...) và gửi xuống TRB/PSU.
//   DEV không nằm trong phạm vi CRC (CRC của khung gốc tính từ CMD hoặc từ địa chỉ, nên không đổi sau khi Gateway bỏ DEV).
//   Chiều trả lời (thiết bị -> PC) là khung gốc, không có DEV.
#include <QByteArray>

namespace proto::devsel {

enum Device : quint8 { Trb = 0x01, Psu = 0x02, Trsu = 0x03 };
constexpr int kOffDevice = 2;

// Chèn byte thiết bị vào khung gốc (sau AB CD).
QByteArray add(Device dev, const QByteArray &frame);
// Bỏ byte thiết bị khỏi khung đã chèn (ghép lại khung gốc); rỗng nếu khung quá ngắn.
QByteArray strip(const QByteArray &frame);

} // namespace proto::devsel
