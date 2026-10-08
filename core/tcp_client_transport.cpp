#include "tcp_client_transport.h"

namespace core {

TcpClientTransport::TcpClientTransport(const QString &host, quint16 port, QObject *parent)
    : Transport(parent), m_host(host), m_port(port), m_socket(this), m_retry(this)
{
    m_retry.setInterval(2000);
    connect(&m_retry, &QTimer::timeout, this, &TcpClientTransport::connectNow);
    connect(&m_socket, &QTcpSocket::connected, this, [this] {
        m_socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);
        m_retry.stop();
        setState(State::Connected);
    });
    connect(&m_socket, &QTcpSocket::readyRead, this, [this] { emit bytesReceived(m_socket.readAll()); });
    connect(&m_socket, &QTcpSocket::disconnected, this, [this] {
        if (!m_wanted) return;
        setState(State::Waiting);
        m_retry.start();
    });
    connect(&m_socket, &QTcpSocket::errorOccurred, this, [this] {
        if (m_wanted) emit errorOccurred(m_socket.errorString());
    });
}

void TcpClientTransport::open()
{
    m_wanted = true;
    setState(State::Waiting);
    connectNow();
    m_retry.start();
}

void TcpClientTransport::close()
{
    m_wanted = false;
    m_retry.stop();
    m_socket.abort();
    setState(State::Closed);
}

void TcpClientTransport::write(const QByteArray &data)
{
    if (m_socket.state() == QAbstractSocket::ConnectedState) m_socket.write(data);
}

QString TcpClientTransport::describe() const
{
    return QStringLiteral("TCP client %1:%2").arg(m_host).arg(m_port);
}

void TcpClientTransport::connectNow()
{
    if (m_socket.state() == QAbstractSocket::UnconnectedState) m_socket.connectToHost(m_host, m_port);
}

} // namespace core
