#include "gateway_header_proto.h"
#include "../core/frame.h"
#include <atomic>

namespace proto::gwhdr {

static std::atomic<bool> g_direct{false};
void setDirectMode(bool on) { g_direct = on; }
bool directMode() { return g_direct; }

QByteArray insert(Device dev, const QByteArray &frame)
{
    if (frame.size() < kOffDevice || frame.size() + kExtra > 0xFFFF) return {};
    QByteArray f = frame;
    const int total = frame.size() + kExtra;
    QByteArray h;
    h.append(char(dev));
    h.append(char(total >> 8));
    h.append(char(total & 0xFF));
    f.insert(kOffDevice, h);
    return f;
}

QByteArray add(Device dev, const QByteArray &frame)
{
    return directMode() ? frame : insert(dev, frame);
}

QByteArray insertBeam(const QByteArray &block)
{
    const int total = block.size() + kBeamHeader;
    if (block.isEmpty() || total > 0xFFFF) return {};
    QByteArray f;
    f.append(char(core::kHeader1));
    f.append(char(core::kHeader2));
    f.append(char(kTypeBeam));
    f.append(char(total >> 8));
    f.append(char(total & 0xFF));
    return f + block;
}

QByteArray addBeam(const QByteArray &block)
{
    return directMode() ? block : insertBeam(block);
}

QByteArray strip(const QByteArray &frame)
{
    if (frame.size() >= kBeamHeader && quint8(frame.at(2)) == kTypeBeam) {
        if ((quint8(frame.at(3)) << 8 | quint8(frame.at(4))) != frame.size()) return {};
        return frame.mid(kBeamHeader);
    }
    if (frame.size() < kOffDevice + kExtra) return {};
    const quint8 dev = quint8(frame.at(kOffDevice));
    if (dev < Trb || dev > Trsu) return {};
    if ((quint8(frame.at(kOffDevice + 1)) << 8 | quint8(frame.at(kOffDevice + 2))) != frame.size()) return {};
    QByteArray f = frame;
    f.remove(kOffDevice, kExtra);
    return f;
}

QByteArray Unwrapper::feed(const QByteArray &data)
{
    m_buf += data;
    QByteArray out;
    while (!m_buf.isEmpty()) {
        const bool header = quint8(m_buf.at(0)) == core::kHeader1;
        if (header && m_buf.size() < 3) break;                                   // chưa đủ để biết có DEV hay không
        const bool second = m_buf.size() >= 2 && quint8(m_buf.at(1)) == core::kHeader2;
        if (header && second && quint8(m_buf.at(2)) == kTypeBeam) {
            if (m_buf.size() < kBeamHeader) break;
            const int len = quint8(m_buf.at(3)) << 8 | quint8(m_buf.at(4));
            if (len < kBeamHeader + 2 || len > 8192) {
                out.append(m_buf.at(0));
                m_buf.remove(0, 1);
                continue;
            }
            if (m_buf.size() < len) break;
            out += m_buf.mid(kBeamHeader, len - kBeamHeader);   // bỏ 5 byte đầu, còn lại là khối 42 byte gốc
            m_buf.remove(0, len);
        } else if (header && second && quint8(m_buf.at(2)) >= Trb && quint8(m_buf.at(2)) <= Trsu) {
            if (m_buf.size() < kOffDevice + kExtra) break;
            const int len = quint8(m_buf.at(3)) << 8 | quint8(m_buf.at(4));
            if (len < kOffDevice + kExtra + 2 || len > 8192) {                   // độ dài vô lý: không phải khung có phần đầu, bỏ một byte để đồng bộ lại
                out.append(m_buf.at(0));
                m_buf.remove(0, 1);
                continue;
            }
            if (m_buf.size() < len) break;                                       // chờ đủ khung
            out += m_buf.left(kOffDevice) + m_buf.mid(kOffDevice + kExtra, len - kOffDevice - kExtra);
            m_buf.remove(0, len);
        } else {
            out.append(m_buf.at(0));                                             // khung gốc: cho qua, bộ tách khung phía sau tự đồng bộ
            m_buf.remove(0, 1);
        }
    }
    return out;
}

} // namespace proto::gwhdr
