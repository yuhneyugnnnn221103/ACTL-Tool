#include "serial_transport.h"

namespace core {

SerialTransport::SerialTransport(const QString &portName, qint32 baudRate, QObject *parent)
    : Transport(parent), m_port(this)
{
    m_port.setPortName(portName);
    m_port.setBaudRate(baudRate); // 8N1, không flow control là mặc định của QSerialPort

    connect(&m_port, &QSerialPort::readyRead, this, [this] { emit bytesReceived(m_port.readAll()); });
    connect(&m_port, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError e) {
        if (e == QSerialPort::NoError) return;
        emit errorOccurred(m_port.errorString());
        if (e == QSerialPort::ResourceError) close(); // rút cáp USB
    });
}

void SerialTransport::configure(const QString &portName, qint32 baudRate)
{
    m_port.setPortName(portName);
    m_port.setBaudRate(baudRate);
}

void SerialTransport::open()
{
    if (m_port.isOpen()) return;
    if (m_port.open(QIODevice::ReadWrite))
        setState(State::Connected);
    // mở lỗi: QSerialPort tự phát errorOccurred
}

void SerialTransport::close()
{
    if (m_port.isOpen()) m_port.close();
    setState(State::Closed);
}

void SerialTransport::write(const QByteArray &data) { m_port.write(data); }

QString SerialTransport::describe() const
{
    return QStringLiteral("%1 @ %2").arg(m_port.portName()).arg(m_port.baudRate());
}

} // namespace core
