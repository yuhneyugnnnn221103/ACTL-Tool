#include "frame_parser.h"
#include <QDateTime>

namespace core {

void FrameParser::feed(const QByteArray &data, const std::function<void(const Frame &)> &onFrame, const DiscardFn &onDiscard)
{
    m_buf += data;
    const auto *d = reinterpret_cast<const quint8 *>(m_buf.constData());
    const int n = m_buf.size();
    int pos = 0;
    int covered = m_covered;
    QByteArray junk;
    auto flushJunk = [&] {
        if (!junk.isEmpty() && onDiscard) onDiscard(junk, Junk);
        junk.clear();
    };

    while (n - pos >= 4) {
        if (d[pos] != kHeader1 || d[pos + 1] != kHeader2) {
            // Byte 0x00 là phần Gateway chèn thay cho khung của thiết bị không trả lời / lỗi: bỏ qua, không tính là byte lỗi.
            if (d[pos] == 0) ++m_stats.zeroFill;
            else { noteDropped(d[pos]); if (pos >= covered) junk.append(char(d[pos])); }
            ++pos;
            continue;
        }

        const FrameSpec *spec = m_registry->match(d + pos + 2);
        if (!spec) { if (pos >= covered) junk.append(char(d[pos])); ++pos; ++m_stats.droppedBytes; continue; }
        if (n - pos < spec->length) break; // chờ đủ khung

        const quint8 *f = d + pos;
        const int len = spec->length;
        const bool tailerOk = !spec->tailer || (f[len - 2] == kTailer1 && f[len - 1] == kTailer2);
        bool ok = tailerOk;
        if (ok && spec->emptyFrom > 0) {
            bool zeros = true;
            for (int i = spec->emptyFrom; i < len - 2 && zeros; ++i) zeros = f[i] == 0;
            if (zeros) {
                ++m_stats.emptyFrames;
                flushJunk();
                Frame fr{spec->cmd, m_buf.mid(pos, len), QDateTime::currentMSecsSinceEpoch()};
                fr.empty = true;
                onFrame(fr);
                pos += len;
                continue;
            }
        }
        auto noteRejected = [&](bool tailerBad) {
            m_rejected.cmd = spec->cmd;
            m_rejected.length = len;
            m_rejected.crcStart = spec->crcStart;
            m_rejected.tailerBad = tailerBad;
            m_rejected.head = QByteArray(reinterpret_cast<const char *>(f), qMin(len, 12));
            m_rejected.tail = QByteArray(reinterpret_cast<const char *>(f + len - 4), 4);
            m_rejected.recvCrc = quint16(f[len - 4] << 8 | f[len - 3]);
            m_rejected.calcCrc = crc16(f + spec->crcStart, len - 4 - spec->crcStart);
            m_rejected.calcFromCmd = crc16(f + 2, len - 4 - 2);
        };
        if (!tailerOk) { ++m_stats.tailerErrors; noteRejected(true); }
        if (ok && spec->checkCrc) {
            const quint16 recv = quint16(f[len - 4] << 8 | f[len - 3]);
            ok = recv == crc16(f + spec->crcStart, len - 4 - spec->crcStart);
            if (!ok) { ++m_stats.crcErrors; noteRejected(false); }
        }
        if (!ok) {
            if (pos >= covered) {                                   // báo nguyên khung lỗi một lần; các byte của nó không báo lại như rác
                flushJunk();
                if (onDiscard) onDiscard(m_buf.mid(pos, len), tailerOk ? BadCrc : BadTail);
            }
            covered = qMax(covered, pos + len);
            ++pos;
            ++m_stats.droppedBytes;
            continue;
        }
        flushJunk();

        ++m_stats.frames;
        onFrame(Frame{spec->cmd, m_buf.mid(pos, len), QDateTime::currentMSecsSinceEpoch()});
        pos += len;
    }
    flushJunk();
    m_covered = qMax(0, covered - pos);
    m_buf.remove(0, pos);
}

void FrameParser::reset()
{
    m_covered = 0;
    m_stats.droppedBytes += m_buf.size();
    m_buf.clear();
}

} // namespace core
