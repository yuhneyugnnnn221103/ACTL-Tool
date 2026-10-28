#include "trb_calib_proto.h"
#include "../core/frame.h"
#include <QJsonArray>

namespace proto::calib {

QByteArray cmd() { return QByteArray::fromHex("1818"); }

static void putMask(QByteArray &f, int off, quint32 mask)
{
    // byte đầu = kênh 24-31, byte cuối = kênh 0-7
    for (int i = 0; i < 4; ++i) f[off + i] = char((mask >> (8 * (3 - i))) & 0xFF);
}

static quint32 getMask(const QByteArray &f, int off)
{
    quint32 m = 0;
    for (int i = 0; i < 4; ++i) m = m << 8 | quint8(f.at(off + i));
    return m;
}

QByteArray buildFrame(int mb, int trb, const Data &d)
{
    QByteArray f(kLength, 0);
    f.replace(2, 2, cmd());
    f[kOffMb] = char(mb);
    f[kOffTrb] = char(trb);
    putMask(f, kOffRxEn, d.rxEn);
    putMask(f, kOffTxEn, d.txEn);
    for (int c = 0; c < kChannels; ++c) {
        const int o = kOffChannel0 + c * kChannelBytes;
        f[o] = char(d.phaseTx[c]);
        f[o + 1] = char(d.phaseRx[c]);
        f[o + 2] = char(d.ampTx[c]);
        f[o + 3] = char(d.ampRx[c]);
    }
    core::seal(f, kCrcStart);
    return f;
}

bool parseFrame(const QByteArray &f, Data &d)
{
    if (f.size() != kLength || f.mid(2, 2) != cmd()) return false;
    d.rxEn = getMask(f, kOffRxEn);
    d.txEn = getMask(f, kOffTxEn);
    for (int c = 0; c < kChannels; ++c) {
        const int o = kOffChannel0 + c * kChannelBytes;
        d.phaseTx[c] = quint8(f.at(o));
        d.phaseRx[c] = quint8(f.at(o + 1));
        d.ampTx[c] = quint8(f.at(o + 2));
        d.ampRx[c] = quint8(f.at(o + 3));
    }
    return true;
}

QJsonObject toJson(const Data &d)
{
    auto channels = [](quint32 m) {
        QJsonArray a;
        for (int c = 0; c < kChannels; ++c) if (m >> c & 1) a.append(c);
        return a;
    };
    QJsonObject o;
    o[QStringLiteral("format")] = QStringLiteral("actl-calib-1");
    o[QStringLiteral("rx_enabled")] = channels(d.rxEn);
    o[QStringLiteral("tx_enabled")] = channels(d.txEn);
    QJsonArray arr;
    for (int c = 0; c < kChannels; ++c)
        arr.append(QJsonObject{{QStringLiteral("ch"), c}, {QStringLiteral("phase_tx"), d.phaseTx[c]}, {QStringLiteral("phase_rx"), d.phaseRx[c]},
                               {QStringLiteral("amp_tx"), d.ampTx[c]}, {QStringLiteral("amp_rx"), d.ampRx[c]}});
    o[QStringLiteral("channels")] = arr;
    return o;
}

QString fromJson(const QJsonObject &o, Data &out)
{
    Data d;
    auto readMask = [&](const QString &key, quint32 &mask) -> QString {
        if (!o.contains(key) || !o.value(key).isArray()) return QStringLiteral("thiếu danh sách \"%1\"").arg(key);
        for (const QJsonValue &v : o.value(key).toArray()) {
            const int c = v.toInt(-1);
            if (!v.isDouble() || c < 0 || c >= kChannels) return QStringLiteral("\"%1\" có kênh không hợp lệ (cần 0..31)").arg(key);
            mask |= quint32(1) << c;
        }
        return {};
    };
    if (QString e = readMask(QStringLiteral("rx_enabled"), d.rxEn); !e.isEmpty()) return e;
    if (QString e = readMask(QStringLiteral("tx_enabled"), d.txEn); !e.isEmpty()) return e;
    if (!o.value(QStringLiteral("channels")).isArray()) return QStringLiteral("thiếu danh sách \"channels\"");
    int seen = 0;
    for (const QJsonValue &v : o.value(QStringLiteral("channels")).toArray()) {
        const QJsonObject ch = v.toObject();
        const int c = ch.value(QStringLiteral("ch")).toInt(-1);
        if (c < 0 || c >= kChannels) return QStringLiteral("kênh \"ch\" phải từ 0 đến 31");
        auto value = [&](const char *key, quint8 &dst) -> QString {
            const QJsonValue j = ch.value(QLatin1String(key));
            const double x = j.toDouble(-1);
            if (!j.isDouble() || x < 0 || x > 255 || x != int(x)) return QStringLiteral("kênh %1: \"%2\" phải là số nguyên 0..255").arg(c).arg(QLatin1String(key));
            dst = quint8(x);
            return {};
        };
        for (const QString &e : {value("phase_tx", d.phaseTx[c]), value("phase_rx", d.phaseRx[c]), value("amp_tx", d.ampTx[c]), value("amp_rx", d.ampRx[c])})
            if (!e.isEmpty()) return e;
        ++seen;
    }
    if (seen == 0) return QStringLiteral("không có kênh nào trong \"channels\"");
    out = d;
    return {};
}

} // namespace proto::calib
