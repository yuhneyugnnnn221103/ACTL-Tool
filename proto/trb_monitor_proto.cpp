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
        for (int i = 1; i <= 8; ++i) u16(p + QStringLiteral("I SEN%1").arg(i));
        for (int i = 1; i <= 8; ++i) u16(p + QStringLiteral("DET%1").arg(i));
        for (int i = 1; i <= 4; ++i) u16(p + QStringLiteral("TEMP%1").arg(i));
        for (int i = 1; i <= 4; ++i) u16(p + QStringLiteral("I PA%1").arg(i));
        u16(p + QStringLiteral("I LNA"));
    }
    u16(QStringLiteral("TRB.V"));
    u16(QStringLiteral("TRB.I"));
    u16(QStringLiteral("TRB.TEMP POWER"));
    for (int i = 1; i <= kNumTrip; ++i) u8(QStringLiteral("TRIP%1").arg(i));
    for (int m = 1; m <= kNumTrm; ++m) u8(QStringLiteral("STATE TRM%1").arg(m));
    u8(QStringLiteral("INIT ADAR"));
    u8(QStringLiteral("PA"));
    u8(QStringLiteral("PG"));
    u16(QStringLiteral("TEMP MCU"));
    u16(QStringLiteral("HUMIDITY POWER"));
    Q_ASSERT(t.size() == kNumFields && off == kLength - 4 - 37);
    return t;
}

namespace {
// Trip i (0..15) -> {loại, chỉ số trong loại}. Loại: 0 I_SEN, 1 PA, 2 system, -1 dự phòng.
struct TripKind { int kind; bool isMin; int index; };
TripKind tripKind(int i)
{
    const bool isMin = i >= 9 && i <= 14;
    const int base = isMin ? i - 9 : i;                    // 0..5 cho cả hai nhóm
    if (i == 6) return {2, false, 0};
    if (i == 7 || i == 8 || i == 15) return {-1, false, 0};
    return {base < 4 ? 0 : 1, isMin, base < 4 ? base : base - 4};
}
}

QString tripName(int i)
{
    const TripKind k = tripKind(i);
    const QString lim = k.isMin ? QStringLiteral("Min") : QStringLiteral("Max");
    switch (k.kind) {
    case 0:  return QStringLiteral("%1 I SEN TRM%2").arg(lim).arg(k.index + 1);
    case 1:  return QStringLiteral("%1 PA TRM%2-%3").arg(lim).arg(k.index * 2 + 1).arg(k.index * 2 + 2);
    case 2:  return QStringLiteral("System");
    default: return QStringLiteral("Dự phòng");
    }
}

QString tripBitName(int i, int bit)
{
    const TripKind k = tripKind(i);
    switch (k.kind) {
    case 0:  return QStringLiteral("TRM%1 I SEN%2").arg(k.index + 1).arg(bit + 1);
    case 1:  return QStringLiteral("TRM%1 PA%2").arg(k.index * 2 + 1 + bit / 4).arg(bit % 4 + 1);
    case 2: {
        static const char *const kSystem[] = {"TRM temp max", "TRM temp min", "V TRB trip max", "V TRB trip min",
                                              "I TRB trip max", "I TRB trip min"};
        return bit < 6 ? QString::fromLatin1(kSystem[bit]) : QString();   // bit 6, 7 chưa dùng
    }
    default: return {};
    }
}

const FieldTable &table()
{
    static const FieldTable t = build();
    return t;
}

} // namespace proto::trbmon
