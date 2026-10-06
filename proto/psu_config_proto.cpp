#include "psu_config_proto.h"
#include "psu_monitor_proto.h"
#include <QFile>
#include <QJsonDocument>

namespace proto::psucfg {

namespace {
struct Reg { const char *name; int size; const char *note; };

// Cấu hình XDP (hot-swap): thứ tự và độ rộng theo bản tin; mô tả lấy từ cột "Chức năng" của file định nghĩa.
const Reg kXdp[] = {
    {"OPERATION", 1, "[0x01] Bật/tắt đầu ra, quy định cơ chế tắt nguồn"},
    {"MODE", 1, "[0xD1] Chọn mode DCM/ACM, chọn FET trong FDM (bỏ qua trong mode AADM)"},
    {"PMBUS_CFG", 1, "[0xD0] Chọn địa chỉ slave PMBus mềm"},
    {"REG_CFG", 2, "[0xD3] Khai báo giá trị R shunt, enable/disable bảo vệ OC dựa trên giá trị RMS"},
    {"PIN_POLARITY", 1, "[0xDA] Cấu hình active High/Low của các chân FAULT, WARN, PWRGD"},
    {"GPO_CFG", 2, "[0xDB] Cấu hình chức năng cho các chân GPO0-3"},
    {"I_SNS_CFG", 2, "[0xD5] Chọn ngưỡng OC, SOC, dòng khởi động (bỏ qua trong mode AADM)"},
    {"V_SNS_CFG", 2, "[0xD4] Chọn ngưỡng lỗi OVIN, chọn dải đo cho VIN, VOUT (căn cứ để chọn bộ số quy đổi m, b, r)"},
    {"TURN_OFF_CTRL", 2, "[0xE6] Cấu hình cơ chế ngắt và xả cổng GATE của FET khi tắt nguồn hoặc FAULT"},
    {"VOUT_UV_FAULT_LIMIT", 2, "[0x44] Ngưỡng UV cho VOUT báo FAULT"},
    {"VIN_OV_FAULT_LIMIT", 2, "[0x55] Ngưỡng OV cho VIN báo FAULT"},
    {"VIN_UV_FAULT_LIMIT", 2, "[0x59] Ngưỡng UV cho VIN báo FAULT"},
    {"OT_FAULT_LIMIT", 2, "[0x4F] Ngưỡng OT báo FAULT quá nhiệt"},
    {"ENABLE_FAULTS", 2, "[0xDE] Enable/disable cơ chế xử lý FAULT, ngắt FET khi có lỗi tương ứng"},
    {"MASK_FAULTS", 2, "[0xDF] Enable/disable cơ chế báo lỗi qua chân GPO hoặc SMBALERT khi có lỗi tương ứng"},
    {"RETRY", 2, "[0xE7] Cấu hình số lần thử bật lại FET sau khi xảy ra lỗi cùng các tùy chọn thử lại"},
    {"PWRGD_DG_TMR", 1, "[0xE4] Cấu hình thời gian trễ lọc nhiễu cho tín hiệu PWRGD"},
    {"TELEMETRY_EN", 2, "[0xE8] Enable/disable các kênh đo lường (các lệnh READ)"},
    {"TELEMETRY_AVG", 2, "[0xE9] Cấu hình số lượng lấy mẫu trung bình cho V, I, P"},
    {"V_TMR", 2, "[0xD9] Đặt thời gian trễ xử lý cho lỗi UV, OV, OUV, OVIN"},
    {"SOA_TMR", 2, "[0xE5] Cấu hình thời gian lọc nhiễu OC, điều chỉnh dòng SOA, lấy mẫu dòng RMS"},
    {"WATCHDOG_TMR", 1, "[0xD8] Cấu hình thời gian cho phép nạp tụ đầu ra và mở FET"},
};

const Reg kAds[] = {
    {"MODE", 2, "Bật/tắt CRC, chọn data len, SPI timeout, chọn nguồn ngắt, cực tính pinout khi data sẵn sàng/chưa sẵn sàng"},
    {"CLOCK", 2, "Bật/tắt các kênh ADC, chọn nguồn dao động, nguồn REF ngoại, chọn chế độ công suất, OSR (tác động đến tốc độ lấy mẫu)"},
    {"GAIN1", 2, "Chọn độ lợi PGA gain cho kênh 0, 1, 2, 3"},
    {"GAIN2", 2, "Chọn độ lợi PGA gain cho kênh 4, 5, 6, 7"},
    {"CFG", 2, "Bật/tắt và cấu hình cho Global-Chop (bù offset nội), Current Detect"},
    {"THRSHLD_MSB", 2, "16 bit trọng số cao của ngưỡng so sánh 24 bit trong chế độ Current Detect"},
    {"THRSHLD_LSB", 2, "8 bit trọng số thấp của ngưỡng so sánh 24 bit trong chế độ Current Detect và cấu hình cho bộ lọc DC Block"},
};
const Reg kAdsChannel[] = {
    {"CFG", 2, "Cấu hình độ trễ pha của nguồn clock cấp, cấu hình tắt bộ lọc DC Block, chọn nguồn tín hiệu đầu vào MUX"},
    {"OCAL_MSB", 2, "16 bit trọng số cao của giá trị hiệu chuẩn Offset"},
    {"OCAL_LSB", 2, "8 bit trọng số thấp của giá trị hiệu chuẩn Offset"},
    {"GCAL_MSB", 2, "16 bit trọng số cao của giá trị hiệu chuẩn Gain"},
    {"GCAL_LSB", 2, "8 bit trọng số thấp của giá trị hiệu chuẩn Gain"},
};
const char *const kIna[] = {"CONFIG", "ADC_CONFIG", "SHUNT_CAL", "SHUNT_TEMPCO", "SOVL", "SUVL", "BOVL", "BUVL", "TEMP_LIMIT"};

// Ngưỡng của cụm: tên gốc trùng phần sau "Cn." của trường giám sát; mỗi tên có _MAX rồi _MIN.
struct Stem { const char *name; int size; };
const Stem kClusterStems[] = {
    {"I_OUT_ADC1", 3}, {"TEMP_ADC1", 3}, {"I_OUT_ADC2", 3}, {"TEMP_ADC2", 3}, {"I_OUT_ADC3", 3}, {"TEMP_ADC3", 3},
    {"I_IN_AMC", 3}, {"V_IN_AMC", 3}, {"V_IN_XDP", 2}, {"V_OUT_XDP", 2}, {"I_OUT_XDP", 2},
};
const char *const kSupplyStems[] = {"I_DCM", "V_DCM", "I_5V", "V_5V", "I_3V3_1", "V_3V3_1", "I_3V3_2", "V_3V3_2"};

QString clusterGroup(int c, const char *kind) { return QStringLiteral("Cụm %1 - %2").arg(c + 1).arg(QString::fromUtf8(kind)); }
const QString kInaGroup = QStringLiteral("INA");
const QString kSupplyGroup = QStringLiteral("Supply - Ngưỡng");
}

QByteArray writeCmd() { return QByteArray::fromHex("0404"); }
QByteArray readCmd() { return QByteArray::fromHex("0303"); }
QByteArray replyCmd() { return QByteArray::fromHex("8282"); }

core::FrameSpec readReplySpec(bool checkCrc)
{
    return {replyCmd(), kLength, kCrcStart, checkCrc, QStringLiteral("PSU cấu hình")};
}

static FieldTable build()
{
    FieldTable t;
    int off = kOffFirstField;
    auto add = [&](const QString &group, const QString &name, int size, const char *note = nullptr, bool hex = false) {
        const int i = t.add(name, off, size, 1.0, {}, group);
        if (hex) t.setHex(i);
        if (note) t.setNote(i, QString::fromUtf8(note));
        off += size;
    };

    for (int c = 0; c < kNumCluster; ++c) {
        for (const Stem &s : kClusterStems)
            for (const char *end : {"_MAX", "_MIN"}) add(clusterGroup(c, "Ngưỡng"), QString::fromLatin1(s.name) + end, s.size);
        for (const Reg &r : kXdp) add(clusterGroup(c, "XDP"), QString::fromLatin1(r.name), r.size, r.note, true);
        for (const Reg &r : kAds) add(clusterGroup(c, "ADS"), QString::fromLatin1(r.name), r.size, r.note, true);
        for (int ch = 0; ch < 8; ++ch)
            for (const Reg &r : kAdsChannel)
                add(clusterGroup(c, "ADS"), QStringLiteral("CH%1_%2").arg(ch).arg(QString::fromLatin1(r.name)), r.size, r.note, true);
    }
    for (int n = 1; n <= 4; ++n)
        for (const char *r : kIna) add(kInaGroup, QStringLiteral("INA%1_%2").arg(n).arg(QString::fromLatin1(r)), 2, nullptr, true);
    for (const char *s : kSupplyStems)
        for (const char *end : {"_MAX", "_MIN"}) add(kSupplyGroup, QString::fromLatin1(s) + end, 3);
    Q_ASSERT(off == kLength - 4 - 16);
    return t;
}

const FieldTable &table()
{
    static const FieldTable t = build();
    return t;
}

QByteArray buildWrite(int addr, const QList<double> &values)
{
    QByteArray f(kLength, 0);
    f.replace(2, 2, writeCmd());
    f[kOffAddr] = char(addr);
    f[kOffMask] = char(kConfigMaskAll >> 8);
    f[kOffMask + 1] = char(kConfigMaskAll & 0xFF);
    table().encode(values, f);
    core::seal(f, kCrcStart);
    return f;
}

QByteArray buildReadRequest(int addr)
{
    QByteArray f(kRequestLength, 0);
    f.replace(2, 2, readCmd());
    f[kOffAddr] = char(addr);
    core::seal(f, kCrcStart);
    return f;
}

QList<double> defaultValues(const QString &jsonPath)
{
    const FieldTable &t = table();
    QList<double> v(t.size(), 0.0);
    for (int i = 0; i < t.size(); ++i) {
        const QString &n = t.fields().at(i).name;
        if (n == QLatin1String("OPERATION")) v[i] = 0x80;
        else if (n == QLatin1String("ENABLE_FAULTS") || n == QLatin1String("MASK_FAULTS")) v[i] = 0x3EFF;
        else if (n == QLatin1String("RETRY")) v[i] = 0x3FBF;
    }
    QFile f(jsonPath);
    if (!jsonPath.isEmpty() && f.open(QIODevice::ReadOnly))
        t.fromJson(QJsonDocument::fromJson(f.readAll()).object(), v);
    return v;
}

const QList<ThresholdMap> &thresholdMap()
{
    static const QList<ThresholdMap> map = [] {
        QList<ThresholdMap> m;
        const FieldTable &mon = psumon::table();
        auto pair = [&](int monitorField, const QString &group, const QString &stem) {
            m.append({monitorField, table().indexOf(group, stem + "_MAX"), table().indexOf(group, stem + "_MIN")});
            Q_ASSERT(monitorField >= 0 && m.last().cfgMax >= 0 && m.last().cfgMin >= 0);
        };
        for (int c = 0; c < kNumCluster; ++c)
            for (const Stem &s : kClusterStems)
                pair(mon.indexOf(QStringLiteral("Cụm %1").arg(c + 1), QStringLiteral("C%1.%2").arg(c + 1).arg(QString::fromLatin1(s.name))),
                     clusterGroup(c, "Ngưỡng"), QString::fromLatin1(s.name));
        for (const char *s : kSupplyStems)
            pair(mon.indexOf(QStringLiteral("Supply"), QStringLiteral("SUPPLY.") + s), kSupplyGroup, QString::fromLatin1(s));
        return m;
    }();
    return map;
}

} // namespace proto::psucfg
