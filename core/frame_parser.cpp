#include "frame_parser.h"
#include <QDateTime>

namespace core {

void FrameParser::feed(const QByteArray &data, const std::function<void(const Frame &)> &onFrame)
{
    m_buf += data;
    const auto *d = reinterpret_cast<const quint8 *>(m_buf.constData());
    const int n = m_buf.size();
    int pos = 0;

    while (n - pos >= 4) {
        if (d[pos] != kHeader1 || d[pos + 1] != kHeader2) { ++pos; ++m_stats.droppedBytes; continue; }

        const FrameSpec *spec = m_registry->match(d + pos + 2);
        if (!spec) { ++pos; ++m_stats.droppedBytes; continue; }
        if (n - pos < spec->length) break; // chờ đủ khung

        const quint8 *f = d + pos;
        const int len = spec->length;
        bool ok = f[len - 2] == kTailer1 && f[len - 1] == kTailer2;
        if (ok && spec->checkCrc) {
            const quint16 recv = quint16(f[len - 4] << 8 | f[len - 3]);
            ok = recv == crc16(f + spec->crcStart, len - 4 - spec->crcStart);
            if (!ok) ++m_stats.crcErrors;
        }
        if (!ok) { ++pos; ++m_stats.droppedBytes; continue; }

        ++m_stats.frames;
        onFrame(Frame{spec->cmd, m_buf.mid(pos, len), QDateTime::currentMSecsSinceEpoch()});
        pos += len;
    }
    m_buf.remove(0, pos);
}

void FrameParser::reset()
{
    m_stats.droppedBytes += m_buf.size();
    m_buf.clear();
}

} // namespace core
