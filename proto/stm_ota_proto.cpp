#include "stm_ota_proto.h"

namespace proto::stmota {

QByteArray cmdBytes(quint8 code) { return QByteArray(2, char(code)); }

core::FrameSpec ackSpec() { return {cmdBytes(kAck), 17, 2, true, QStringLiteral("STM32 ACK nạp code")}; }

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
    core::seal(f, 2);
    return f;
}

QByteArray buildBegin(quint8 addr, quint32 size, quint32 crc, quint32 version)
{
    QByteArray f(21, 0);
    put(f, 5, size, 4);
    put(f, 9, crc, 4);
    put(f, 13, version, 4);
    return sealed(f, kBegin, addr);
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
    return a;
}

QString statusText(quint8 s)
{
    switch (s) {
    case Ok:              return QStringLiteral("OK");
    case Refused:         return QStringLiteral("từ chối (thiết bị chưa ở SAFE_OFF)");
    case BadSize:         return QStringLiteral("ảnh quá lớn so với slot");
    case BadSeq:          return QStringLiteral("sai thứ tự chunk");
    case CrcFail:         return QStringLiteral("CRC32 không khớp");
    case Busy:            return QStringLiteral("thiết bị đang bận");
    case NoCommitPending: return QStringLiteral("chưa có FW_END thành công để commit");
    case EraseFail:       return QStringLiteral("xóa sector thất bại");
    case WrongSlot:       return QStringLiteral("SAI SLOT: ảnh được link cho slot khác");
    default:              return QStringLiteral("mã lạ %1").arg(s);
    }
}

} // namespace proto::stmota
