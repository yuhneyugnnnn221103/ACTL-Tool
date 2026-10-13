#include "fpga_ota_proto.h"
#include <QFileInfo>
#include <QStringList>

namespace proto::fpgaota {

QByteArray replyCmd() { return QByteArray::fromHex("9999"); }

core::FrameSpec replySpec() { return {replyCmd(), 40, 4, true, QStringLiteral("FPGA trạng thái nạp")}; }

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

QByteArray buildWriteVersion(int mb, int trb, const QByteArray &version)
{
    QByteArray f = request(0x44, mb, trb, 0, 0, {});
    f.replace(6, kVersionSize, version.left(kVersionSize).leftJustified(kVersionSize, '\0'));
    core::seal(f, 4);
    return f;
}

// Tên file toàn chữ số (vd 20261013, hoặc 2026101302 nếu cùng ngày có nhiều bản): đổi thành số nguyên, 0 nếu không hợp lệ.
quint32 versionFromFileName(const QString &fileName)
{
    bool ok = false;
    const QString name = QFileInfo(fileName).completeBaseName();
    const qulonglong v = name.toULongLong(&ok, 10);
    for (QChar c : name)
        if (c < QLatin1Char('0') || c > QLatin1Char('9')) return 0;
    return ok && v > 0 && v < 0xFFFFFFFFull ? quint32(v) : 0;
}

QByteArray versionBytes(quint32 v)
{
    QByteArray b(4, '\0');
    for (int i = 0; i < 4; ++i) b[i] = char(v >> (24 - 8 * i));
    return b;
}

quint32 versionValue(const QByteArray &version)
{
    if (version.size() != kVersionSize) return 0;
    quint32 v = 0;
    for (char c : version) v = v << 8 | quint8(c);
    return v;
}

QString versionText(const QByteArray &version)
{
    const quint32 v = versionValue(version);
    return v == 0 || v == 0xFFFFFFFFu ? QString() : QString::number(v);
}

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
    return {quint8(raw[4]), quint8(raw[5]), be32(6), be32(10), be32(14), raw.mid(18, kVersionSize)};
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
