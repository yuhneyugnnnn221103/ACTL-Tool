#include "psu_monitor_proto.h"
#include <cmath>

namespace proto::psumon {

namespace {
struct Def { const char *name; const char *label; int size; };

const Def kCluster[kClusterFields] = {
    {"I_OUT_ADC1", "Dòng Output ADC 1", 3},   {"TEMP_ADC1", "Nhiệt độ ADC 1", 3},
    {"I_OUT_ADC2", "Dòng Output ADC 2", 3},   {"TEMP_ADC2", "Nhiệt độ ADC 2", 3},
    {"I_OUT_ADC3", "Dòng Output ADC 3", 3},   {"TEMP_ADC3", "Nhiệt độ ADC 3", 3},
    {"I_IN_AMC", "Dòng Input AMC", 3},        {"V_IN_AMC", "Áp Input AMC", 3},
    {"V_IN_XDP", "Áp XDP (VIN)", 2},          {"V_IN_XDP_PEAK", "Áp XDP PEAK (VIN)", 2},
    {"V_IN_XDP_VALLEY", "Áp XDP VALLEY (VIN)", 2},
    {"V_OUT_XDP", "Áp XDP (VOUT)", 2},        {"V_OUT_XDP_PEAK", "Áp XDP PEAK (VOUT)", 2},
    {"V_OUT_XDP_VALLEY", "Áp XDP VALLEY (VOUT)", 2},
    {"I_OUT_XDP", "Dòng tổng XDP (IOUT)", 2}, {"I_OUT_XDP_PEAK", "Dòng tổng XDP PEAK (IOUT)", 2},
    {"I_OUT_XDP_VALLEY", "Dòng tổng XDP VALLEY (IOUT)", 2},
    {"I_OUT_XDP_RMS", "Dòng tổng XDP RMS (IOUT)", 2},
    {"TEMP_FET", "Nhiệt độ FET", 2},          {"TEMP_XDP", "Nhiệt độ XDP", 2},
};
const Def kSupply[kNumSupply] = {
    {"I_DCM", "Dòng DCM Supply", 3}, {"V_DCM", "Áp DCM Supply", 3}, {"I_5V", "Dòng 5V", 3}, {"V_5V", "Áp 5V", 3},
    {"I_3V3_1", "Dòng 3V3_1", 3},    {"V_3V3_1", "Áp 3V3_1", 3},    {"I_3V3_2", "Dòng 3V3_2", 3},
    {"V_3V3_2", "Áp 3V3_2", 3},
};
const char *const kRtc[kNumRtc] = {"YEAR", "MONTH", "DAY", "HOUR", "MINUTE", "SECOND", "MS"};
const char *const kRtcLabel[kNumRtc] = {"Năm", "Tháng", "Ngày", "Giờ", "Phút", "Giây", "Mili giây"};
}

QByteArray cmd() { return QByteArray::fromHex("8181"); }

core::FrameSpec spec(bool checkCrc)
{
    return {cmd(), kLength, kCrcStart, checkCrc, QStringLiteral("PSU giám sát")};
}

static FieldTable build()
{
    FieldTable t;
    int off = kOffAddr + 1;
    auto add = [&](const QString &name, int size, const QString &group) { t.add(name, off, size, 1.0, {}, group); off += size; };

    for (int c = 0; c < kNumCluster; ++c) {
        const QString group = QStringLiteral("Cụm %1").arg(c + 1);
        for (const Def &d : kCluster) add(QStringLiteral("C%1.%2").arg(c + 1).arg(d.name), d.size, group);
    }
    for (const Def &d : kSupply) {
        add(QStringLiteral("SUPPLY.") + d.name, d.size, QStringLiteral("Supply"));
        if (d.name[0] == 'I') t.setSigned(t.size() - 1);                   // dòng có dấu, áp không dấu
    }
    for (const char *n : kRtc) add(QStringLiteral("RTC.") + n, 1, QStringLiteral("RTC"));
    for (int i = 1; i <= kNumTrip; ++i) add(QStringLiteral("TRIP%1").arg(i), 1, QStringLiteral("Trip"));
    // Mở rộng firmware (byte 239..260); 2 byte cuối vùng dự phòng chưa dùng.
    const QString fw = QStringLiteral("Firmware");
    for (const char *n : {"SAFE_STATE", "PWR_FLAGS", "TRIP_COUNT", "FAULT_CODE", "INA_FRESH", "INA_CFG_OK", "DCM_FT", "CFG_SAVED"})
        add(QStringLiteral("FW.") + n, 1, fw);
    for (int i = 1; i <= 4; ++i) add(QStringLiteral("FW.DIAG_ALRT%1").arg(i), 2, fw);
    add(QStringLiteral("FW.SLOT"), 1, fw);
    add(QStringLiteral("FW.TRIAL"), 1, fw);
    add(QStringLiteral("FW.VERSION"), 4, fw);
    Q_ASSERT(t.size() == kNumFields && off == kLength - 4 - 2);
    return t;
}

const FieldTable &table()
{
    static const FieldTable t = build();
    return t;
}

QByteArray pollCmd() { return QByteArray::fromHex("0202"); }

core::FrameSpec pollSpec(bool checkCrc) { return {pollCmd(), kPollLength, kCrcStart, checkCrc, QStringLiteral("PSU hỏi giám sát")}; }

QByteArray buildPoll(int addr)
{
    QByteArray f(kPollLength, 0);
    f.replace(2, 2, pollCmd());
    f[kOffAddr] = char(addr);
    core::seal(f, kCrcStart);
    return f;
}

QByteArray buildFrame(int addr, const QList<double> &values)
{
    QByteArray f(kLength, 0);
    f.replace(2, 2, cmd());
    f[kOffAddr] = char(addr);
    table().encode(values, f);
    core::seal(f, kCrcStart);
    return f;
}

QString safeStateText(int state)
{
    switch (state) {
    case SafeBoot:    return QStringLiteral("BOOT");
    case SafeOff:     return QStringLiteral("OFF");
    case SafeArming:  return QStringLiteral("ARMING");
    case SafeOn:      return QStringLiteral("ON");
    case SafeTripped: return QStringLiteral("TRIPPED");
    case SafeLockout: return QStringLiteral("LOCKOUT");
    default:          return QStringLiteral("? (%1)").arg(state);
    }
}

double supplyShown(double raw) { return std::floor(raw / 16.0); }

// Bảng trip TẠM THỜI theo firmware (chờ bảng chính thức): Trip 1-2 Supply vượt MAX / dưới MIN, 3 INA SHNT, 4 INA BUS, 5 nhiệt INA và chân FT của DCM, 6 hệ thống.
QString tripName(int i)
{
    static const char *const kNames[kNumTrip] = {"Supply vượt MAX", "Supply dưới MIN", "INA quá dòng/công suất", "INA điện áp bus",
                                                 "INA nhiệt / DCM FT", "Hệ thống", "Dự phòng", "Dự phòng", "Dự phòng", "Dự phòng"};
    return i >= 0 && i < kNumTrip ? QString::fromUtf8(kNames[i]) : QString();
}

QString tripBitName(int i, int bit)
{
    static const char *const kSupply[kNumSupply] = {"I_DCM", "V_DCM", "I_5V", "V_5V", "I_3V3_1", "V_3V3_1", "I_3V3_2", "V_3V3_2"};
    const int c = bit % 4 + 1;                      // INA / DCM c
    switch (i) {
    case 0: return QStringLiteral("%1 vượt MAX").arg(QLatin1String(kSupply[bit]));
    case 1: return QStringLiteral("%1 dưới MIN").arg(QLatin1String(kSupply[bit]));
    case 2: return bit < 4 ? QStringLiteral("INA%1 SHNTOL/POL (quá dòng/công suất)").arg(c) : QStringLiteral("INA%1 SHNTUL").arg(c);
    case 3: return bit < 4 ? QStringLiteral("INA%1 BUSOL (quá áp bus)").arg(c) : QStringLiteral("INA%1 BUSUL (sụt áp bus)").arg(c);
    case 4: return bit < 4 ? QStringLiteral("INA%1 TMPOL (quá nhiệt)").arg(c) : QStringLiteral("DCM%1 chân FT báo lỗi").arg(c);
    case 5: {
        static const char *const kSys[] = {"Mất liên lạc I²C", "Cắt do ALERT, không xác nhận nguyên nhân", "LOCKOUT (trip liên tiếp ≥ 3 lần)", "Cấu hình INA không hợp lệ"};
        return bit < 4 ? QString::fromUtf8(kSys[bit]) : QString();
    }
    default: return {};
    }
}

QString clusterFieldLabel(int f) { return QString::fromUtf8(kCluster[f].label); }
QString supplyLabel(int i) { return QString::fromUtf8(kSupply[i].label); }
QString rtcLabel(int i) { return QString::fromUtf8(kRtcLabel[i]); }

} // namespace proto::psumon
