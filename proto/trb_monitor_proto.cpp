#include "trb_monitor_proto.h"

namespace proto::trbmon {

QByteArray cmd() { return QByteArray::fromHex("1111"); }

core::FrameSpec spec(bool checkCrc)
{
    return {cmd(), kLength, kCrcStart, checkCrc, QStringLiteral("TRB giám sát")};
}

// Hệ số quy đổi đang là 1 (giá trị thô) như tool cũ; có công thức thì sửa scale/unit tại đây.
static FieldTable build()
{
    FieldTable t;
    int off = 6;
    auto u16 = [&](const QString &name) { t.add(name, off, 2); off += 2; };
    auto u8 = [&](const QString &name) { t.add(name, off, 1); off += 1; };

    for (int m = 1; m <= kNumTrm; ++m) {
        const QString p = QStringLiteral("TRM%1.").arg(m);
        for (int i = 1; i <= 8; ++i) u16(p + QStringLiteral("I_SEN%1").arg(i));
        for (int i = 1; i <= 8; ++i) u16(p + QStringLiteral("DET%1").arg(i));
        for (int i = 1; i <= 4; ++i) u16(p + QStringLiteral("TEMP%1").arg(i));
        for (int i = 1; i <= 4; ++i) u16(p + QStringLiteral("I_PA%1").arg(i));
        u16(p + QStringLiteral("I_LNA"));
    }
    u16(QStringLiteral("TRB.V"));
    u16(QStringLiteral("TRB.I"));
    u16(QStringLiteral("TRB.TEMP_POWER"));
    for (int i = 1; i <= kNumTrip; ++i) u8(QStringLiteral("TRIP%1").arg(i));
    for (int m = 1; m <= kNumTrm; ++m) u8(QStringLiteral("STATE_TRM%1").arg(m));
    u8(QStringLiteral("INIT_ADAR"));
    u8(QStringLiteral("PA"));
    u8(QStringLiteral("PG"));
    u16(QStringLiteral("TEMP_MCU"));
    u16(QStringLiteral("HUMIDITY_POWER"));
    Q_ASSERT(t.size() == kNumFields && off == kLength - 4 - 37);
    return t;
}

const FieldTable &table()
{
    static const FieldTable t = build();
    return t;
}

} // namespace proto::trbmon
