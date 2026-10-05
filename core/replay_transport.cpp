#include "replay_transport.h"
#include <QFile>

namespace core {

ReplayTransport::ReplayTransport(const QString &hexFile, int intervalMs, bool loop, QObject *parent)
    : Transport(parent), m_file(hexFile), m_loop(loop), m_timer(this)
{
    m_timer.setInterval(intervalMs);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (m_next >= m_chunks.size()) {
            if (!m_loop || m_chunks.isEmpty()) { m_timer.stop(); return; }
            m_next = 0;
        }
        emit bytesReceived(m_chunks.at(m_next++));
    });
}

void ReplayTransport::open()
{
    QFile f(m_file);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit errorOccurred(f.errorString());
        return;
    }
    m_chunks.clear();
    while (!f.atEnd()) {
        const QByteArray bytes = QByteArray::fromHex(f.readLine());
        if (!bytes.isEmpty()) m_chunks.append(bytes);
    }
    m_next = 0;
    m_timer.start();
    setState(State::Connected);
}

void ReplayTransport::close()
{
    m_timer.stop();
    setState(State::Closed);
}

} // namespace core
