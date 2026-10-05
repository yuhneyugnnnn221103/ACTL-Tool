#include "tcp_server_transport.h"

namespace core {

TcpServerTransport::TcpServerTransport(const QHostAddress &address, quint16 port, QObject *parent)
    : Transport(parent), m_address(address), m_port(port), m_server(this)
{
    connect(&m_server, &QTcpServer::newConnection, this, &TcpServerTransport::onNewConnection);
}

void TcpServerTransport::configure(const QHostAddress &address, quint16 port)
{
    m_address = address;
    m_port = port;
}

void TcpServerTransport::open()
{
    if (m_server.isListening()) return;
    if (m_server.listen(m_address, m_port))
        setState(State::Waiting);
    else
        emit errorOccurred(m_server.errorString());
}

void TcpServerTransport::close()
{
    m_server.close();
    dropClient();
    setState(State::Closed);
}

void TcpServerTransport::write(const QByteArray &data)
{
    if (m_client) m_client->write(data);
}

QString TcpServerTransport::describe() const
{
    return QStringLiteral("TCP %1:%2").arg(m_address.toString()).arg(m_port);
}

void TcpServerTransport::onNewConnection()
{
    while (QTcpSocket *s = m_server.nextPendingConnection()) {
        dropClient();
        m_client = s;
        s->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        s->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
        connect(s, &QTcpSocket::readyRead, this, [this, s] { emit bytesReceived(s->readAll()); });
        connect(s, &QTcpSocket::disconnected, this, [this, s] {
            if (s != m_client) return;
            m_client = nullptr;
            s->deleteLater();
            if (m_server.isListening()) setState(State::Waiting);
        });
        setState(State::Connected);
    }
}

void TcpServerTransport::dropClient()
{
    if (!m_client) return;
    QTcpSocket *s = m_client;
    m_client = nullptr;
    s->disconnect(this);
    s->abort();
    s->deleteLater();
}

} // namespace core
