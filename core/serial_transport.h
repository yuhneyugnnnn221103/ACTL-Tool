#pragma once
#include "transport.h"
#include <QSerialPort>

namespace core {

class SerialTransport : public Transport {
    Q_OBJECT
public:
    SerialTransport(const QString &portName, qint32 baudRate, QObject *parent = nullptr);
    void configure(const QString &portName, qint32 baudRate); // gọi khi đang đóng
    void open() override;
    void close() override;
    void write(const QByteArray &data) override;
    QString describe() const override;

private:
    QSerialPort m_port;
};

} // namespace core
