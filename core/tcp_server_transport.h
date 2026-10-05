#pragma once
#include "transport.h"
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

namespace core {

// PC là server, Gateway là client duy nhất. Kết nối mới thay thế kết nối cũ.
class TcpServerTransport : public Transport {
    Q_OBJECT
public:
    TcpServerTransport(const QHostAddress &address, quint16 port, QObject *parent = nullptr);
    void configure(const QHostAddress &address, quint16 port); // gọi khi đang đóng
    void open() override;
    void close() override;
    void write(const QByteArray &data) override;
    QString describe() const override;

private:
    void onNewConnection();
    void dropClient();

    QHostAddress m_address;
    quint16 m_port;
    QTcpServer m_server;
    QTcpSocket *m_client = nullptr;
};

} // namespace core
