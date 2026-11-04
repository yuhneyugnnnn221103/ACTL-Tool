#include "direct_poller.h"
#include "../proto/direct_query_proto.h"
#include "../proto/psu_monitor_proto.h"

namespace services {

DirectPoller::DirectPoller(core::Link *link, TrbMonitor *trb, PsuMonitor *psu, QObject *parent)
    : QObject(parent), m_link(link), m_trb(trb), m_psu(psu)
{
    m_timer.setInterval(100);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &DirectPoller::pollOnce);
    connect(link, &core::Link::stateChanged, this, [this](core::Transport::State s) {
        m_connected = s == core::Transport::State::Connected;
        if (!m_connected) m_pending = 0;   // đường đóng: lệnh đang chờ đã bị Link hủy
    });
    connect(link, &core::Link::requestFinished, this, [this](quint64 id, bool ok, const core::Frame &) { onFinished(id, ok); });
}

void DirectPoller::setTarget(Kind kind, int a, int b) { m_kind = kind; m_a = a; m_b = b; }

void DirectPoller::setPeriodMs(int ms) { m_timer.setInterval(qMax(10, ms)); }

QByteArray DirectPoller::query() const
{
    return m_kind == Kind::Trb ? proto::directq::buildTrb(m_a, m_b) : proto::directq::buildPsu(m_a);
}

void DirectPoller::resetStats()
{
    m_sent = m_replied = m_timeouts = 0;
    emit statsChanged();
}

void DirectPoller::start()
{
    if (m_timer.isActive()) return;
    m_timer.start();
    emit runningChanged(true);
    pollOnce();
}

void DirectPoller::stop()
{
    if (!m_timer.isActive()) return;
    m_timer.stop();
    emit runningChanged(false);
}

void DirectPoller::pollOnce()
{
    if (m_pending || !m_connected || (m_pause && m_pause())) return;
    core::Request r;
    r.data = query();
    r.replyCmd = m_kind == Kind::Trb ? proto::directq::trbCmd() : proto::psumon::cmd();
    r.timeoutMs = m_timeoutMs;
    r.retries = 0;       // mỗi lần hỏi là một mẫu: không trả lời thì tính một lần im lặng, không gửi lại
    r.priority = -10;    // nhường mọi lệnh khác (điều khiển, cấu hình)
    m_pending = m_link->send(r);
    ++m_sent;
    emit statsChanged();
}

void DirectPoller::onFinished(quint64 id, bool ok)
{
    if (!id || id != m_pending) return;
    m_pending = 0;
    if (ok) {
        ++m_replied;
    } else {
        ++m_timeouts;
        if (m_kind == Kind::Trb) m_trb->onSilent(m_a, m_b); else m_psu->onSilent(m_a);
    }
    emit statsChanged();
}

} // namespace services
