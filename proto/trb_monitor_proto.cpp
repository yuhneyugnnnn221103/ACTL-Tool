#include "trb_monitor_proto.h"

namespace proto::trbmon {

QByteArray cmd() { return QByteArray::fromHex("1111"); }

core::FrameSpec spec(bool checkCrc)
{
    // Gateway thay khung của TRB không phản hồi / sai CRC / lỗi bằng khung chỉ có địa chỉ: AB CD 11 11 MB TRB, data và CRC toàn 0x00, E1 E2.
    return {cmd(), kLength, kCrcStart, checkCrc, QStringLiteral("TRB giám sát"), 6};
}

// Hệ số quy đổi đang là 1 (giá trị thô) như tool cũ; có công thức thì sửa scale/unit tại đây.
static const char *const kDebugNames[kNumDebug] = {
    "MEM_ERR_1", "MEM_ERR_2", "MEM_ERR_CODE", "MEM_ERR_CNT_HI", "MEM_ERR_CNT_LO", "BEAM_DONE_CNT", "BEAM_NOT_READY_CNT",
    "BEAM_LAST_TIME", "FREQ_IDX", "TEMP_IDX_1", "TEMP_IDX_2", "MODE_AMP", "LUT_ERR_TIMEOUT_CNT", "CALIB_AMP_RETRY_FAIL_CNT",
    "WRITE_DROP_BUSY_CNT", "ID_TRB", "STATUS_TEMP"};

static FieldTable build()
{
    FieldTable t;
    int off = 6;
    auto u16 = [&](const QString &name) { t.add(name, off, 2); off += 2; };
    auto u8 = [&](const QString &name) { t.add(name, off, 1); off += 1; };
    auto u32 = [&](const QString &name) { t.add(name, off, 4); off += 4; };

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
    u8(QStringLiteral("INIT DATA"));                       // byte 240 (đếm từ 1)
    for (int i = 1; i <= kNumErr; ++i) u16(QStringLiteral("BẢN TIN LỖI %1").arg(i));   // 12 x 2 byte = 24 byte (241..264)
    u32(QStringLiteral("PERIOD TXEN"));                    // byte 265-268
    u16(QStringLiteral("PULSE TXEN"));                     // 269-270
    u32(QStringLiteral("PERIOD BEAMSYNC"));                // 271-274
    u16(QStringLiteral("PULSE BEAMSYNC"));                 // 275-276
    // Từ byte 277: 17 trường debug (19 byte; BEAM_DONE_CNT và BEAM_LAST_TIME 2 byte, còn lại 1 byte), chỉ ghi log (CSV) và không hiện trên giao diện.
    for (const char *n : kDebugNames) {                    // BEAM_DONE_CNT và BEAM_LAST_TIME 2 byte, còn lại 1 byte
        const QString name = QString::fromLatin1(n);
        if (name == QLatin1String("BEAM_DONE_CNT") || name == QLatin1String("BEAM_LAST_TIME")) u16(name); else u8(name);
    }
    Q_ASSERT(t.size() == kNumFields && off == kLength - 4 - 11);   // còn 11 byte dự phòng, CRC, E1 E2
    return t;
}

QString errName(int i)
{
    static const char *const kNames[kNumErr] = {
        "Bản tin Beam", "Bản tin Calib PG", "Bản tin Config ADAR", "Bản tin ghi LUT", "Bản tin Control", "Bản tin tra LUT",
        "Bản tin dịch pha/biên", "Bản tin ghi calib toàn mảng", "Bản tin kiểm tra calib toàn mảng", "Bản tin ghi config",
        "Bản tin hỏi cấu hình", "Bản tin hỏi giám sát"};
    return i >= 0 && i < kNumErr ? QString::fromUtf8(kNames[i]) : QStringLiteral("Loại lỗi %1").arg(i + 1);
}

namespace {
// Trip i (0..15) -> {loại, chỉ số trong loại}. Loại: 0 I_SEN, 1 PA, 2 system, -1 dự phòng.
struct TripKind { int kind; bool isMin; int index; };
TripKind tripKind(int i)
{
    const bool isMin = i >= 9 && i <= 14;
    const int base = isMin ? i - 9 : i;                    // 0..5 cho cả hai nhóm
    if (i == 6) return {2, false, 0};                      // Trip 7: System 1
    if (i == 8) return {2, false, 1};                      // Trip 9: System 2
    if (i == 7 || i == 15) return {-1, false, 0};
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
    case 2:  return QStringLiteral("System %1").arg(k.index + 1);
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
        static const char *const kSystem1[] = {"TRM temp max", "TRM temp min", "V TRB trip max", "V TRB trip min",
                                               "I TRB trip max", "I TRB trip min"};
        static const char *const kSystem2[] = {"Pulse TXEN trip max", "Pulse TXEN trip min", "Period BSYNC trip max", "Period BSYNC trip min",
                                               "Pulse BSYNC trip max", "Pulse BSYNC trip min", "Period TXEN trip max", "Period TXEN trip min"};
        if (k.index == 0) return bit < 6 ? QString::fromLatin1(kSystem1[bit]) : QString();   // System 1: bit 6, 7 chưa dùng
        return QString::fromLatin1(kSystem2[bit]);
    }
    default: return {};
    }
}

const FieldTable &table()
{
    static const FieldTable t = build();
    return t;
}

core::FrameSpec pollSpec(bool checkCrc)
{
    return {cmd(), kPollLength, kCrcStart, checkCrc, QStringLiteral("TRB hỏi giám sát")};
}

QByteArray buildPoll(int mb, int trb)
{
    QByteArray f(kPollLength, 0);
    f[2] = f[3] = char(0x11);
    f[kOffMb] = char(mb);
    f[kOffTrb] = char(trb);
    core::seal(f, kCrcStart);
    return f;
}

QByteArray buildFrame(int mb, int trb, const QList<double> &values)
{
    QByteArray f(kLength, 0);
    f[2] = f[3] = char(0x11);
    f[kOffMb] = char(mb);
    f[kOffTrb] = char(trb);
    table().encode(values, f);
    core::seal(f, kCrcStart);
    return f;
}

} // namespace proto::trbmon
