#pragma once
#include "frame.h"
#include <functional>

namespace core {

// Tách khung từ luồng byte. Sai tailer/CRC hoặc CMD lạ -> trượt 1 byte để đồng bộ lại.
class FrameParser {
public:
    struct Stats { quint64 frames = 0, crcErrors = 0, droppedBytes = 0; };

    explicit FrameParser(const FrameRegistry *registry) : m_registry(registry) {}

    void feed(const QByteArray &data, const std::function<void(const Frame &)> &onFrame);
    void reset();                       // bỏ phần dở dang (gọi khi đường truyền im lặng quá lâu)
    bool hasPending() const { return !m_buf.isEmpty(); }
    const Stats &stats() const { return m_stats; }

private:
    const FrameRegistry *m_registry;
    QByteArray m_buf;
    Stats m_stats;
};

} // namespace core
