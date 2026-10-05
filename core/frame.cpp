#include "frame.h"

namespace core {

quint16 crc16(const quint8 *data, int len)
{
    quint16 crc = 0xFFFF;
    for (int i = 0; i < len; ++i) {
        crc ^= quint16(data[i]) << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000) ? quint16((crc << 1) ^ 0x1021) : quint16(crc << 1);
    }
    return crc;
}

void seal(QByteArray &f, int crcStart)
{
    const int n = f.size();
    f[0] = char(kHeader1);
    f[1] = char(kHeader2);
    const quint16 crc = crc16(reinterpret_cast<const quint8 *>(f.constData()) + crcStart,
                              n - 4 - crcStart);
    f[n - 4] = char(crc >> 8);
    f[n - 3] = char(crc & 0xFF);
    f[n - 2] = char(kTailer1);
    f[n - 1] = char(kTailer2);
}

bool FrameRegistry::add(const FrameSpec &spec)
{
    const int n = spec.cmd.size();
    if ((n != 1 && n != 2) || spec.length < kMinFrameLen || m_specs.contains(spec.cmd))
        return false;
    for (auto it = m_specs.cbegin(); it != m_specs.cend(); ++it)
        if (it.key().size() != n && it.key().at(0) == spec.cmd.at(0))
            return false;
    m_specs.insert(spec.cmd, spec);
    return true;
}

const FrameSpec *FrameRegistry::match(const quint8 *p) const
{
    const char *c = reinterpret_cast<const char *>(p);
    auto it = m_specs.constFind(QByteArray::fromRawData(c, 2));
    if (it == m_specs.cend())
        it = m_specs.constFind(QByteArray::fromRawData(c, 1));
    return it == m_specs.cend() ? nullptr : &it.value();
}

} // namespace core
