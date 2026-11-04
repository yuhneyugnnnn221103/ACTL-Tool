#pragma once
#include "serial_transport.h"
#include "tcp_client_transport.h"

namespace core {

// Chuyển đường truyền của Link giữa hai chế độ, mỗi lúc chỉ một chế độ chạy:
//   Ethernet: TCP tới Gateway (chế độ bình thường);
//   Direct:   cổng COM nối thẳng xuống TRB / PSU, không qua Gateway (chế độ Debug COM).
// Chỉ gọi các hàm cấu hình và setMode từ thread của Link (qua QMetaObject::invokeMethod trên Link).
class SwitchTransport : public Transport {
    Q_OBJECT
public:
    enum class Mode { Ethernet, Direct };
    SwitchTransport(TcpClientTransport *tcp, SerialTransport *serial, QObject *parent = nullptr);

    Mode mode() const { return m_mode; }
    TcpClientTransport *tcp() const { return m_tcp; }
    SerialTransport *serial() const { return m_serial; }
    // Đóng đường đang dùng rồi đổi chế độ; không tự mở đường mới (gọi open() sau).
    void setMode(Mode m);

    void open() override { active()->open(); }
    void close() override { active()->close(); }
    void write(const QByteArray &data) override { active()->write(data); }
    QString describe() const override { return active()->describe(); }

signals:
    void modeChanged(core::SwitchTransport::Mode mode);

private:
    Transport *active() const { return m_mode == Mode::Ethernet ? static_cast<Transport *>(m_tcp) : m_serial; }

    TcpClientTransport *m_tcp;
    SerialTransport *m_serial;
    Mode m_mode = Mode::Ethernet;
};

} // namespace core
