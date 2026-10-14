#include "beam_eth_proto.h"
#include "../core/frame.h"
#include <QtMath>

namespace proto::beameth {

QString modeName(ModeSel m)
{
    switch (m) {
    case ModeSel::A0: return QStringLiteral("A0");
    case ModeSel::A1: return QStringLiteral("A1");
    case ModeSel::A2: return QStringLiteral("A2");
    }
    return {};
}

quint8 modeByte(ModeSel m)
{
    switch (m) {
    case ModeSel::A0: return 0x01;
    case ModeSel::A1: return 0x10;
    case ModeSel::A2: return 0x11;
    }
    return 0;
}

qint16 toTenths(double v) { return qint16(qRound(v * 10.0)); }

QString validate(const Params &p)
{
    auto angleOk = [](double v) { return v >= kAngleMin - 0.05 && v <= kAngleMax + 0.05; };
    if (!angleOk(p.esp)) return QStringLiteral("Esp phải trong khoảng %1 … %2").arg(kAngleMin).arg(kAngleMax);
    if (!angleOk(p.beta)) return QStringLiteral("Beta phải trong khoảng %1 … %2").arg(kAngleMin).arg(kAngleMax);
    if (p.freqMhz < 0 || p.freqMhz > kFreqMax) return QStringLiteral("Freq phải từ 0 đến %1 MHz").arg(kFreqMax);
    if (p.modeValue < 0 || p.modeValue > kModeValueMax) return QStringLiteral("Mode phải từ 0 đến %1").arg(kModeValueMax);
    return {};
}

QByteArray buildFrame(const Params &p)
{
    if (!validate(p).isEmpty()) return {};
    QByteArray f(kLength, 0);
    f[0] = char(core::kHeader1);
    f[1] = char(core::kHeader2);
    auto put16 = [&](int off, quint16 v) { f[off] = char(v & 0xFF); f[off + 1] = char(v >> 8); };   // little endian: byte thấp trước
    put16(2, quint16(toTenths(p.esp)));          // số có dấu: ép về quint16 giữ nguyên bit bù 2
    put16(4, quint16(toTenths(p.beta)));
    put16(6, quint16(p.freqMhz));
    f[8] = char(p.modeValue);
    f[9] = char(modeByte(p.mode));
    const quint16 crc = core::crc16(reinterpret_cast<const quint8 *>(f.constData()) + 2, 8);
    put16(10, crc);
    f[12] = char(0xEF);
    f[13] = char(0xFF);
    return f;
}

} // namespace proto::beameth
