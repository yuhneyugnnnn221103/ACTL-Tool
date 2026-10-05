#include "fpga_ota_proto.h"
#include <QStringList>

namespace proto::fpgaota {

QByteArray replyCmd() { return QByteArray::fromHex("9999"); }

core::FrameSpec replySpec() { return {replyCmd(), 25, 4, true, QStringLiteral("FPGA trạng thái nạp")}; }

static QByteArray request(quint8 cmd, int mb, int trb, quint16 id, quint8 length, const QByteArray &data)
{
    QByteArray f(269, char(0xFF)); // phần dữ liệu không dùng để 0xFF
    f[2] = f[3] = char(cmd);
    f[4] = char(mb);
    f[5] = char(trb);
    f[6] = char(id >> 8);
    f[7] = char(id & 0xFF);
    f[8] = char(length);
    f.replace(9, data.size(), data);
    core::seal(f, 4);
    return f;
}

QByteArray buildErase(int mb, int trb) { return request(0x66, mb, trb, 0, 0, {}); }
QByteArray buildQuery(int mb, int trb) { return request(0x88, mb, trb, 0, 0, {}); }
QByteArray buildBoot(int mb, int trb) { return request(0xAA, mb, trb, 0, 0, {}); }

QByteArray buildLoad(int mb, int trb, quint16 packetId, const QByteArray &chunk)
{
    Q_ASSERT(!chunk.isEmpty() && chunk.size() <= kChunk);
    return request(0x55, mb, trb, packetId, quint8(chunk.size() - 1), chunk); // Length = số byte - 1
}

Status parseStatus(const QByteArray &raw)
{
    auto be32 = [&](int o) {
        return quint32(quint8(raw[o])) << 24 | quint32(quint8(raw[o + 1])) << 16
             | quint32(quint8(raw[o + 2])) << 8 | quint8(raw[o + 3]);
    };
    return {quint8(raw[4]), quint8(raw[5]), be32(6), be32(10), be32(14)};
}

QString errorText(quint8 code)
{
    QStringList l;
    if (code & ErrUartFrame) l << QStringLiteral("khung UART");
    if (code & ErrFlashWrite) l << QStringLiteral("ghi flash");
    if (code & ErrFlashErase) l << QStringLiteral("xóa flash");
    if (code & ErrHwicapRead) l << QStringLiteral("đọc HWICAP");
    if (code & ErrIprogReboot) l << QStringLiteral("IPROG reboot");
    return l.isEmpty() ? QStringLiteral("không lỗi") : QStringLiteral("lỗi ") + l.join(", ");
}

} // namespace proto::fpgaota
