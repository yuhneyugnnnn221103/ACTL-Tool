#include "link.h"

namespace core {

Link::Link(const QString &name, Transport *transport, const FrameRegistry *registry, QObject *parent)
    : QObject(parent), m_name(name), m_transport(transport), m_parser(registry), m_timer(this), m_idle(this)
{
    qRegisterMetaType<core::Frame>();
    qRegisterMetaType<core::Transport::State>();
    m_transport->setParent(this); // đi cùng Link khi moveToThread
    m_timer.setSingleShot(true);
    m_idle.setSingleShot(true);
    m_idle.setInterval(0);

    connect(m_transport, &Transport::bytesReceived, this, &Link::onBytes);
    connect(m_transport, &Transport::errorOccurred, this, &Link::errorOccurred);
    connect(m_transport, &Transport::stateChanged, this, [this](Transport::State s) {
        emit stateChanged(s);
        if (s == Transport::State::Connected) {
            pump();
        } else {
            m_parser.reset();
            if (m_busy) { m_timer.stop(); finish(false); }
        }
    });
    connect(&m_timer, &QTimer::timeout, this, &Link::onTimeout);
    connect(&m_idle, &QTimer::timeout, this, [this] {
        m_parser.reset();
        m_reportedErrors = m_parser.stats().crcErrors + m_parser.stats().droppedBytes;
        emit rxErrorsChanged(m_parser.stats().crcErrors, m_parser.stats().droppedBytes);
    });
}

void Link::start() { m_transport->open(); }
void Link::stop() { m_transport->close(); }

quint64 Link::send(const Request &request)
{
    const quint64 id = m_nextId.fetchAndAddRelaxed(1);
    QMetaObject::invokeMethod(this, [this, id, request] { enqueue(id, request); }, Qt::QueuedConnection);
    return id;
}

void Link::enqueue(quint64 id, const Request &request)
{
    int i = m_queue.size();
    while (i > 0 && m_queue.at(i - 1).req.priority < request.priority) --i;
    m_queue.insert(i, Pending{id, request});
    pump();
}

void Link::pump()
{
    if (m_busy || m_queue.isEmpty()) return;
    m_current = m_queue.takeFirst();
    m_busy = true;
    if (m_transport->state() != Transport::State::Connected) { finish(false); return; }
    transmit();
}

void Link::transmit()
{
    ++m_current.attempts;
    m_transport->write(m_current.req.data);
    emit rawSent(m_current.req.data);

    const Request &r = m_current.req;
    if (!r.replyCmd.isEmpty())
        m_timer.start(r.timeoutMs);
    else if (r.gapMs > 0)
        m_timer.start(r.gapMs);
    else
        finish(true);
}

void Link::onTimeout()
{
    if (!m_busy) return;
    if (m_current.req.replyCmd.isEmpty()) { finish(true); return; } // hết khoảng nghỉ
    if (m_current.attempts <= m_current.req.retries) transmit();
    else finish(false);
}

void Link::finish(bool ok, const Frame &reply)
{
    m_busy = false;
    emit requestFinished(m_current.id, ok, reply);
    // Gọi lại qua event loop để một loạt lệnh lỗi liên tiếp không đệ quy sâu.
    QMetaObject::invokeMethod(this, &Link::pump, Qt::QueuedConnection);
}

void Link::onBytes(const QByteArray &data)
{
    emit rawReceived(data);
    m_parser.feed(data, [this](const Frame &f) {
        if (m_busy && !m_current.req.replyCmd.isEmpty() && f.cmd == m_current.req.replyCmd) {
            m_timer.stop();
            finish(true, f);
        }
        emit frameReceived(f);
    });
    const auto &st = m_parser.stats();
    if (st.crcErrors + st.droppedBytes != m_reportedErrors) {
        m_reportedErrors = st.crcErrors + st.droppedBytes;
        emit rxErrorsChanged(st.crcErrors, st.droppedBytes);
    }
    if (m_idle.interval() > 0) {
        if (m_parser.hasPending()) m_idle.start();
        else m_idle.stop();
    }
}

} // namespace core
