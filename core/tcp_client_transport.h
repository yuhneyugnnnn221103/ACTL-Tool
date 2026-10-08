#pragma once
#include "transport.h"
#include <QTcpSocket>
#include <QTimer>

namespace core {

// Chủ động nối tới một máy chủ TCP và tự nối lại khi mất kết nối (dùng cho chương trình giả lập nối tới Gateway).
class TcpClientTransport : public Transport {
    Q_OBJECT
public:
    TcpClientTransport(const QString &host, quint16 port, QObject *parent = nullptr);
    void open() override;
    void close() override;
    void write(const QByteArray &data) override;
    QString describe() const override;

private:
    void connectNow();

    QString m_host;
    quint16 m_port;
    QTcpSocket m_socket;
    QTimer m_retry;
    bool m_wanted = false;
};

} // namespace core
