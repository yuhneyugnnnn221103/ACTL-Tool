#pragma once
// Phân nhóm dòng log để mỗi trang chỉ hiện nội dung của mình trong khung Sự kiện / Hex thô.
#include <QByteArray>
#include <QString>

namespace ui {

enum LogGroup {
    LogSystem = 0,          // kết nối, lỗi đường truyền, khóa kỹ sư: hiện ở mọi trang
    LogTrbMonitor = 1,      // giám sát, điều khiển, beam, cảnh báo, trip của TRB
    LogTrbConfig = 2,
    LogPsuMonitor = 4,
    LogPsuConfig = 8,
    LogFirmware = 16,       // nạp FPGA và STM32
    LogAll = 31
};

// Mã loại sự kiện đặt ở đầu mỗi dòng trong khung Sự kiện, ví dụ "[TRIP] MB0 / TRB3: Trip".
enum class EventKind { Trip, Warn, Ok, Pg, Ctrl, Cfg, Debug, Ota, Net, Err, Auth };
QString eventTag(EventKind k);                          // "TRIP", "WARN", ...
QString tagged(EventKind k, const QString &text);       // "[TRIP] text"

struct FrameClass {
    int group = LogSystem;
    int mb = -1, trb = -1;  // địa chỉ TRB trong khung (giám sát, điều khiển, beam), -1 nếu không có
};

// Phân loại một khung nguyên (kể cả header) theo CMD 2 byte.
FrameClass classifyFrame(const QByteArray &raw);

} // namespace ui
