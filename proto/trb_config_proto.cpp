#include "trb_config_proto.h"
#include "trb_monitor_proto.h"

namespace proto::trbcfg {

QByteArray readReplyCmd() { return QByteArray::fromHex("A4A4"); }

core::FrameSpec readReplySpec(bool checkCrc)
{
    return {readReplyCmd(), kLength, 4, checkCrc, QStringLiteral("TRB cấu hình")};
}

static FieldTable build()
{
    FieldTable t;
    int off = 6;
    auto add = [&](const QString &group, const QString &name, int size) {
        t.add(name, off, size, 1.0, {}, group);
        off += size;
    };

    for (const char *n : {"dy", "dx", "delta"}) add("CAL", n, 3);

    static const char *kAdar[] = {
        "CONFIG_RESET", "CONFIG_LDO", "LDO", "MISC_ENABLE", "CONFIG_RAM_REGISTER", "CONFIG_SWITCH",
        "CONFIG_TX_EN", "BIAS_CURRENT_TX", "BIAS_CURRENT_TX_DRV", "BIAS_CURRENT_RX_LNA", "BIAS_CURRENT_RX",
        "CONFIG_RX_EN", "CONFIG_ADC_CONTROL", "RX_TO_TX_DELAY_CTRL", "TX_TO_RX_DELAY_CTRL",
        "PA_BIAS1_OFF", "PA_BIAS2_OFF", "PA_BIAS3_OFF", "PA_BIAS4_OFF", "LNA_BIAS_OFF",
        "PA_BIAS1_ON", "PA_BIAS2_ON", "PA_BIAS3_ON", "PA_BIAS4_ON", "LNA_BIAS_ON"};
    for (int chip = 1; chip <= 8; ++chip)
        for (const char *n : kAdar) add(QStringLiteral("ADAR %1").arg(chip), n, 1);

    for (const char *kind : {"LUT", "CALIB_ARRAY"}) {
        for (const char *q : {"TEMP", "FREQ"}) {
            add("LUT", QStringLiteral("%1_MAX_%2").arg(q, kind), 2);
            add("LUT", QStringLiteral("%1_MIN_%2").arg(q, kind), 2);
            add("LUT", QStringLiteral("RESOLUTION_%1_%2").arg(q, kind), 2);
            add("LUT", QStringLiteral("NUMBER_BIT_%1_%2").arg(q, kind), 1);
        }
    }

    for (int trm = 1; trm <= 4; ++trm) {
        const QString g = QStringLiteral("TRM %1").arg(trm);
        for (const char *base : {"I_SEN_%1", "I_SEN_PA_%1"})
            for (int ch = 1; ch <= (base[6] == 'P' ? 4 : 8); ++ch) {
                add(g, QString::fromLatin1(base).arg(ch) + "_MAX", 2);
                add(g, QString::fromLatin1(base).arg(ch) + "_MIN", 2);
            }
    }

    for (const char *n : {"TEMP_TRB_MAX", "TEMP_TRB_MIN", "TEMP_POWER_MAX", "TEMP_POWER_MIN", "I_SEN_TRB_MAX",
                          "I_SEN_TRB_MIN", "VOLTAGE_TRB_MAX", "VOLTAGE_TRB_MIN", "PULSE_TR_MAX",
                          "TIME_DELAY_PROTECT_PA", "TIME_DELAY_OFF_TR", "TEMP_MCU_MAX", "TEMP_MCU_MIN",
                          "TEMP_OFFSET_TRM1", "TEMP_OFFSET_TRM2", "TEMP_OFFSET_TRM3", "TEMP_OFFSET_TRM4",
                          "TEMP_OFFSET_POWER", "TEMP_OFFSET_MCU"})
        add("GENERAL", n, 2);

    add("DEBUG", "PULSE_TR_DEBUG", 2);
    add("DEBUG", "PERIOD_TR_DEBUG", 4);
    Q_ASSERT(off == kLength - 4 - 37);
    return t;
}

const FieldTable &table()
{
    static const FieldTable t = build();
    return t;
}

QByteArray buildWrite(int mb, int trb, const QList<double> &values)
{
    QByteArray f(kLength, 0);
    f[2] = f[3] = char(0xA1);
    f[kOffMb] = char(mb);
    f[kOffTrb] = char(trb);
    table().encode(values, f);
    core::seal(f, 4);
    return f;
}

QByteArray buildReadReply(int mb, int trb, const QList<double> &values)
{
    QByteArray f = buildWrite(mb, trb, values);
    f[2] = f[3] = char(0xA4);
    core::seal(f, 4);
    return f;
}

QByteArray buildReadRequest(int mb, int trb)
{
    QByteArray f(10, 0);
    f[2] = f[3] = char(0xA3);
    f[kOffMb] = char(mb);
    f[kOffTrb] = char(trb);
    core::seal(f, 4);
    return f;
}

QJsonObject toJson(const QList<double> &values)
{
    QJsonObject root;
    for (int i = 0; i < table().size(); ++i) {
        const Field &f = table().fields().at(i);
        QJsonObject g = root.value(f.group).toObject();
        g.insert(f.name, values.at(i));
        root.insert(f.group, g);
    }
    return root;
}

void fromJson(const QJsonObject &json, QList<double> &values)
{
    for (int i = 0; i < table().size(); ++i) {
        const Field &f = table().fields().at(i);
        const QJsonValue v = json.value(f.group).toObject().value(f.name);
        const double max = f.size >= 4 ? 4294967295.0 : double((1u << (8 * f.size)) - 1);
        if (v.isDouble() && v.toDouble() >= 0 && v.toDouble() <= max) values[i] = v.toDouble();
    }
}

const QList<ThresholdMap> &thresholdMap()
{
    static const QList<ThresholdMap> map = [] {
        QList<ThresholdMap> m;
        auto pair = [&](int monitorField, const QString &group, const QString &base) {
            m.append({monitorField, table().indexOf(group, base + "_MAX"), table().indexOf(group, base + "_MIN")});
            Q_ASSERT(m.last().cfgMax >= 0 && m.last().cfgMin >= 0);
        };
        for (int trm = 0; trm < 4; ++trm) {
            const QString g = QStringLiteral("TRM %1").arg(trm + 1);
            const int base = trbmon::kIdxTrm0 + trm * trbmon::kTrmFields;
            for (int ch = 0; ch < 8; ++ch) pair(base + ch, g, QStringLiteral("I_SEN_%1").arg(ch + 1));
            for (int ch = 0; ch < 4; ++ch) pair(base + 20 + ch, g, QStringLiteral("I_SEN_PA_%1").arg(ch + 1));
            // TEMP_TRB là ngưỡng nhiệt độ chung cho cả 16 cảm biến TEMP1..4 của TRM1..4.
            for (int ch = 0; ch < 4; ++ch) m.append({base + 16 + ch, table().indexOf("GENERAL", "TEMP_TRB_MAX"), table().indexOf("GENERAL", "TEMP_TRB_MIN")});
        }
        pair(trbmon::kIdxTrbV, "GENERAL", "VOLTAGE_TRB");
        pair(trbmon::kIdxTrbI, "GENERAL", "I_SEN_TRB");
        pair(trbmon::kIdxTrbTemp, "GENERAL", "TEMP_POWER");
        pair(trbmon::kIdxMcuTemp, "GENERAL", "TEMP_MCU");
        return m;
    }();
    return map;
}

} // namespace proto::trbcfg
