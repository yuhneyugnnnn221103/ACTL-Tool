#pragma once
#include "frame.h"
#include <functional>

namespace core {

// Tách khung từ luồng byte. Sai tailer/CRC hoặc CMD lạ -> trượt 1 byte để đồng bộ lại.
class FrameParser {
public:
    struct Stats { quint64 frames = 0, crcErrors = 0, droppedBytes = 0, zeroFill = 0, emptyFrames = 0, tailerErrors = 0; };  // zeroFill: byte 0x00 bị bỏ qua (khung rỗng của Gateway)

    explicit FrameParser(const FrameRegistry *registry) : m_registry(registry) {}

    void feed(const QByteArray &data, const std::function<void(const Frame &)> &onFrame);
    void reset();                       // bỏ phần dở dang (gọi khi đường truyền im lặng quá lâu)
    bool hasPending() const { return !m_buf.isEmpty(); }
    const Stats &stats() const { return m_stats; }

    // Khung gần nhất bị loại vì sai đuôi hoặc CRC, để báo chẩn đoán cho người dùng.
    struct Rejected {
        QByteArray cmd, head, tail;       // head: 12 byte đầu khung; tail: 4 byte cuối (CRC 2 byte + đuôi 2 byte)
        int length = 0, crcStart = 0;
        bool tailerBad = false;
        quint16 recvCrc = 0, calcCrc = 0, calcFromCmd = 0;   // calcFromCmd: CRC nếu tính từ byte CMD đầu tiên
    };
    const Rejected &lastRejected() const { return m_rejected; }
    quint64 rejectedCount() const { return m_stats.crcErrors + m_stats.tailerErrors; }

private:
    const FrameRegistry *m_registry;
    QByteArray m_buf;
    Stats m_stats;
    Rejected m_rejected;
};

} // namespace core
