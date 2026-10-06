#include "psu_monitor_proto.h"

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
    for (const Def &d : kSupply) add(QStringLiteral("SUPPLY.") + d.name, d.size, QStringLiteral("Supply"));
    for (const char *n : kRtc) add(QStringLiteral("RTC.") + n, 1, QStringLiteral("RTC"));
    for (int i = 1; i <= kNumTrip; ++i) add(QStringLiteral("TRIP%1").arg(i), 1, QStringLiteral("Trip"));
    Q_ASSERT(t.size() == kNumFields && off == kLength - 4 - 24);
    return t;
}

const FieldTable &table()
{
    static const FieldTable t = build();
    return t;
}

QString clusterFieldLabel(int f) { return QString::fromUtf8(kCluster[f].label); }
QString supplyLabel(int i) { return QString::fromUtf8(kSupply[i].label); }
QString rtcLabel(int i) { return QString::fromUtf8(kRtcLabel[i]); }

} // namespace proto::psumon
