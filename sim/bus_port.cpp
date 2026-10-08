#include "bus_port.h"
#include <QTimer>

namespace sim {

BusPort::BusPort(core::Transport *transport, SimWorld *world, bool checkCrc, quint32 seed, QObject *parent)
    : QObject(parent), m_transport(transport), m_world(world), m_parser(&m_registry), m_rng(seed)
{
    registerRequestFrames(m_registry, checkCrc);
    connect(transport, &core::Transport::bytesReceived, this, &BusPort::onBytes);
    connect(transport, &core::Transport::stateChanged, this, [this](core::Transport::State s) {
        emit message(s == core::Transport::State::Connected ? QStringLiteral("%1: đã kết nối").arg(m_transport->describe())
                     : s == core::Transport::State::Waiting ? QStringLiteral("%1: đang chờ kết nối").arg(m_transport->describe())
                                                            : QStringLiteral("%1: đã đóng").arg(m_transport->describe()));
    });
    connect(transport, &core::Transport::errorOccurred, this, [this](const QString &e) { emit message(e); });
    // TRB đang debug tự gửi khung giám sát, không qua bước hỏi.
    connect(world, &SimWorld::unsolicited, this, [this](const QByteArray &f) { send(f, true); });
}

void BusPort::onBytes(const QByteArray &data)
{
    m_parser.feed(data, [this](const core::Frame &frame) {
        ++m_stats.requests;
        emit received(frame.raw);
        const QByteArray reply = m_world->handle(frame);
        if (!reply.isEmpty()) send(reply, true);
    });
}

void BusPort::send(QByteArray reply, bool allowFaults)
{
    int delay = 0;
    if (allowFaults) {
        if (m_faults.dropPercent > 0 && int(m_rng.bounded(100)) < m_faults.dropPercent) { ++m_stats.dropped; return; }
        if (m_faults.corruptPercent > 0 && int(m_rng.bounded(100)) < m_faults.corruptPercent) {
            reply[reply.size() - 4] = char(reply.at(reply.size() - 4) ^ 0xFF);   // sai CRC
            ++m_stats.corrupted;
        }
        if (m_faults.delayMaxMs > 0)
            delay = m_faults.delayMinMs + int(m_rng.bounded(m_faults.delayMaxMs - m_faults.delayMinMs + 1));
    }
    // Không trả lời ngay trong lúc bên kia còn đang ghi: phát qua vòng lặp sự kiện.
    QTimer::singleShot(delay, this, [this, reply] {
        m_transport->write(reply);
        ++m_stats.replies;
        emit sent(reply);
    });
}

} // namespace sim
