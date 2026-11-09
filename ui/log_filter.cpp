#include "log_filter.h"
#include "../proto/gateway_header_proto.h"

namespace ui {

QString eventTag(EventKind k)
{
    switch (k) {
    case EventKind::Trip:  return QStringLiteral("TRIP");   // TRB/PSU vào trạng thái Trip
    case EventKind::Warn:  return QStringLiteral("WARN");   // giá trị vượt ngưỡng
    case EventKind::Ok:    return QStringLiteral("OK");     // trở lại bình thường
    case EventKind::Pg:    return QStringLiteral("PG");     // Power Good
    case EventKind::Ctrl:  return QStringLiteral("CTRL");   // lệnh điều khiển / beam đã gửi
    case EventKind::Cfg:   return QStringLiteral("CFG");    // đọc/ghi cấu hình
    case EventKind::Debug: return QStringLiteral("DBG");    // chế độ Debug
    case EventKind::Ota:   return QStringLiteral("OTA");    // nạp code FPGA / STM32
    case EventKind::Net:   return QStringLiteral("NET");    // kết nối, ngắt kết nối Gateway
    case EventKind::Err:   return QStringLiteral("ERR");    // lỗi đường truyền, file, nhập liệu
    case EventKind::Auth:  return QStringLiteral("AUTH");   // khóa / mở khóa chế độ kỹ sư
    }
    return {};
}

QString tagged(EventKind k, const QString &text) { return QStringLiteral("[%1] %2").arg(eventTag(k), text); }

FrameClass classifyFrame(const QByteArray &raw)
{
    FrameClass c;
    const QByteArray inner = proto::gwhdr::strip(raw);   // khung PC gửi xuống Gateway có DEV + LEN sau AB CD: phân loại theo khung gốc
    if (!inner.isEmpty()) return classifyFrame(inner);
    if (raw.size() < 4) return c;
    if (raw.size() == 42 && quint8(raw.at(12)) == 0xEF && quint8(raw.at(13)) == 0xFF) { c.group = LogBeam; return c; }   // bản tin beam: 3 khung 14 byte, đuôi EF FF
    const quint16 cmd = quint16(quint8(raw.at(2)) << 8 | quint8(raw.at(3)));
    switch (cmd) {
    case 0x2323:            // giám sát TRB
    case 0xA2A2:            // điều khiển TRB
    case 0x1414:            // beam
        c.group = LogTrbMonitor;
        if (raw.size() >= 6) { c.mb = quint8(raw.at(4)); c.trb = quint8(raw.at(5)); }
        break;
    case 0xA1A1: case 0xA3A3: case 0xA4A4:
        c.group = LogTrbConfig;
        break;
    case 0x1818:            // Calib
        c.group = LogCalib;
        if (raw.size() >= 6) { c.mb = quint8(raw.at(4)); c.trb = quint8(raw.at(5)); }
        break;
    case 0x8181: case 0x0101:
        c.group = LogPsuMonitor;
        break;
    case 0x0303: case 0x0404: case 0x8282:
        c.group = LogPsuConfig;
        break;
    case 0x5555: case 0x6666: case 0x8888: case 0xAAAA: case 0x9999:   // nạp FPGA
    case 0x9090: case 0x9191: case 0x9292: case 0x9393: case 0x9494: case 0x9595: case 0x9696: case 0x9898: case 0x9A9A: case 0xA9A9:   // nạp STM32
        c.group = LogFirmware;
        break;
    default:
        break;
    }
    return c;
}

} // namespace ui
