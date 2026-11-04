#include "switch_transport.h"

namespace core {

SwitchTransport::SwitchTransport(TcpClientTransport *tcp, SerialTransport *serial, QObject *parent)
    : Transport(parent), m_tcp(tcp), m_serial(serial)
{
    m_tcp->setParent(this);
    m_serial->setParent(this);
    for (Transport *t : {static_cast<Transport *>(m_tcp), static_cast<Transport *>(m_serial)}) {
        // Chỉ chuyển tiếp từ đường đang dùng: đường kia đã đóng nên không còn gì để nghe.
        connect(t, &Transport::stateChanged, this, [this, t](Transport::State s) { if (t == active()) setState(s); });
        connect(t, &Transport::bytesReceived, this, [this, t](const QByteArray &d) { if (t == active()) emit bytesReceived(d); });
        connect(t, &Transport::errorOccurred, this, [this, t](const QString &m) { if (t == active()) emit errorOccurred(m); });
    }
}

void SwitchTransport::setMode(Mode m)
{
    if (m == m_mode) return;
    active()->close();
    m_mode = m;
    setState(State::Closed);
    emit modeChanged(m);
}

} // namespace core
