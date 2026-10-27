#include "beam_eth_proto.h"
#include "../core/frame.h"
#include <QtMath>

namespace proto::beameth {

quint8 modeByte(int index)
{
    static const quint8 kBytes[kFrameCount] = {0x01, 0x10, 0x11};
    return index >= 0 && index < kFrameCount ? kBytes[index] : 0;
}

qint16 toTenths(double v) { return qint16(qRound(v * 10.0)); }

QString validate(const Params &p)
{
    auto angleOk = [](double v) { return v >= kAngleMin - 0.05 && v <= kAngleMax + 0.05; };
    for (int k = 0; k < kFrameCount; ++k) {
        const FrameParams &f = p.frame[k];
        const QString mode = QStringLiteral("Mode %1: ").arg(modeByte(k), 2, 16, QLatin1Char('0'));
        if (!angleOk(f.esp)) return mode + QStringLiteral("Esp phải trong khoảng %1 … %2").arg(kAngleMin).arg(kAngleMax);
        if (!angleOk(f.beta)) return mode + QStringLiteral("Beta phải trong khoảng %1 … %2").arg(kAngleMin).arg(kAngleMax);
        if (f.freqMhz < 0 || f.freqMhz > kFreqMax) return mode + QStringLiteral("Freq phải từ 0 đến %1 MHz").arg(kFreqMax);
        if (f.modeValue < 0 || f.modeValue > kModeValueMax) return mode + QStringLiteral("Mode phải từ 0 đến %1").arg(kModeValueMax);
    }
    return {};
}

QByteArray buildFrame(const Params &p)
{
    if (!validate(p).isEmpty()) return {};
    QByteArray all;
    all.reserve(kLength);
    for (int k = 0; k < kFrameCount; ++k) {      // ba khung ứng với ba mode, mỗi khung có giá trị riêng
        const FrameParams &v = p.frame[k];
        QByteArray f(kFrameLength, 0);
        f[0] = char(core::kHeader1);
        f[1] = char(core::kHeader2);
        auto put16 = [&](int off, quint16 v) { f[off] = char(v & 0xFF); f[off + 1] = char(v >> 8); };   // little endian
        put16(2, quint16(toTenths(v.esp)));      // số có dấu: ép về quint16 giữ nguyên bit bù 2
        put16(4, quint16(toTenths(v.beta)));
        put16(6, quint16(v.freqMhz));
        f[8] = char(v.modeValue);
        f[9] = char(modeByte(k));
        put16(10, core::crc16(reinterpret_cast<const quint8 *>(f.constData()), 10));   // CRC phủ cả header (AB CD) tới hết MODE
        f[12] = char(0xEF);
        f[13] = char(0xFF);
        all += f;
    }
    return all;
}

} // namespace proto::beameth
