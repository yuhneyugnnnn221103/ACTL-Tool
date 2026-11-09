#include "stm_ota_proto.h"
#include <QFileInfo>

namespace proto::stmota {

QByteArray cmdBytes(quint8 code) { return QByteArray(2, char(code)); }

core::FrameSpec ackSpec() { return {cmdBytes(kAck), 17, 4, true, QStringLiteral("STM32 ACK nạp code")}; }

quint32 crc32(const QByteArray &data)
{
    quint32 crc = 0xFFFFFFFFu;
    for (unsigned char byte : data) {
        crc ^= byte;
        for (int b = 0; b < 8; ++b) crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    return ~crc;
}

static void put(QByteArray &f, int off, quint32 v, int bytes)
{
    for (int i = bytes - 1; i >= 0; --i, v >>= 8) f[off + i] = char(v & 0xFF);
}

static QByteArray sealed(QByteArray f, quint8 cmd, quint8 addr)
{
    f.replace(2, 2, cmdBytes(cmd));
    f[kOffAddr] = char(addr);
    core::seal(f, 4);
    return f;
}

QByteArray buildBegin(quint8 addr, quint32 size, quint32 crc)
{
    QByteArray f(17, 0);
    put(f, 5, size, 4);
    put(f, 9, crc, 4);
    return sealed(f, kBegin, addr);
}

static bool printable(unsigned char c) { return c >= 0x20 && c <= 0x7E; }

QByteArray buildSetVersion(quint8 addr, const QString &version)
{
    QByteArray f(kVersionLength, 0);
    const QByteArray ascii = version.toLatin1().left(kVersionChars);
    for (int i = 0; i < ascii.size(); ++i) f[5 + i] = printable(quint8(ascii.at(i))) ? ascii.at(i) : '\0';   // ký tự lạ thành 00: firmware cắt chuỗi tại đó
    return sealed(f, kSetVersion, addr);
}

core::FrameSpec versionReplySpec() { return {cmdBytes(kVersionReply), kVersionLength, 4, true, QStringLiteral("STM32 trả lời version")}; }

QString parseVersionReply(const QByteArray &f)
{
    QString s;
    for (int i = 5; i < 5 + kVersionChars && i < f.size(); ++i) {
        if (!printable(quint8(f.at(i)))) break;
        s += QChar(quint8(f.at(i)));
    }
    return s;
}

VersionName versionFromFileName(const QString &path)
{
    VersionName v;
    const QString base = QFileInfo(path).completeBaseName();
    for (const QChar &c : base) {
        const bool ok = c.unicode() >= 0x20 && c.unicode() <= 0x7E;
        v.text += ok ? c : QLatin1Char('_');
        v.replaced = v.replaced || !ok;
    }
    if (v.text.isEmpty()) v.error = QStringLiteral("tên file rỗng");
    else if (v.text.size() > kVersionChars)
        v.error = QStringLiteral("tên file (không kể đuôi) dài %1 ký tự, tối đa %2: hãy đổi tên file (không tự cắt vì dễ mất mã commit ở cuối)").arg(v.text.size()).arg(kVersionChars);
    return v;
}

QByteArray buildData(quint8 addr, quint16 seq, const QByteArray &chunk)
{
    QByteArray f(269, char(0xFF)); // chunk cuối đệm 0xFF
    put(f, 5, seq, 2);
    put(f, 7, quint32(chunk.size()), 2);
    f.replace(9, chunk.size(), chunk);
    return sealed(f, kData, addr);
}

QByteArray buildCtrl(quint8 addr, quint8 cmd) { return sealed(QByteArray(9, 0), cmd, addr); }

Ack parseAck(const QByteArray &f)
{
    Ack a;
    a.addr = quint8(f[kOffAddr]);
    a.ackCmd = quint8(f[5]);
    a.status = quint8(f[6]);
    a.info = quint32(quint8(f[7])) << 24 | quint32(quint8(f[8])) << 16 | quint32(quint8(f[9])) << 8 | quint8(f[10]);
    a.slot = f[11];
    a.trial = quint8(f[12]);
    return a;
}

QString statusText(quint8 s)
{
    switch (s) {
    case Ok:              return QStringLiteral("OK");
    case Refused:         return QStringLiteral("từ chối (còn cụm DCM chưa OFF)");
    case BadSize:         return QStringLiteral("kích thước ảnh quá lớn (tối đa 639 KB)");
    case BadSeq:          return QStringLiteral("sai thứ tự chunk");
    case CrcFail:         return QStringLiteral("CRC32 không khớp");
    case Busy:            return QStringLiteral("thiết bị đang bận / sai thời điểm (SET_VERSION chỉ sau ERASE và trước END)");
    case NoCommitPending: return QStringLiteral("chưa có END thành công để commit");
    case EraseFail:       return QStringLiteral("xóa sector thất bại");
    case WrongSlot:       return QStringLiteral("SAI VÙNG: Reset_Handler của ảnh nằm ngoài vùng app (ảnh link sai địa chỉ)");
    case ProgFail:        return QStringLiteral("lỗi ghi flash");
    case Reserved10:      return QStringLiteral("mã dự phòng 10");
    case NotErased:       return QStringLiteral("chưa ERASE vùng app");
    case NotRecovery:     return QStringLiteral("app đang chạy không nhận lệnh nạp: phải vào recovery trước (ENTER_REC)");
    default:              return QStringLiteral("mã lạ %1").arg(s);
    }
}

} // namespace proto::stmota
