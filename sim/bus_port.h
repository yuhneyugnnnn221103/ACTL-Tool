#pragma once
#include "../proto/gateway_header_proto.h"
// Một cổng nối với Gateway: nhận byte, tách khung, chuyển cho SimWorld rồi gửi trả lời (có thể bơm lỗi).
#include "sim_world.h"
#include "../core/frame_parser.h"
#include "../core/transport.h"
#include <QRandomGenerator>

namespace sim {

struct Faults {
    int dropPercent = 0;       // bỏ không trả lời
    int corruptPercent = 0;    // trả lời nhưng sai CRC
    int delayMinMs = 0, delayMaxMs = 0;
};

class BusPort : public QObject {
    Q_OBJECT
public:
    struct Stats { quint64 requests = 0, replies = 0, dropped = 0, corrupted = 0; };

    // transport thuộc về người gọi (không bị xóa cùng BusPort).
    BusPort(core::Transport *transport, SimWorld *world, bool checkCrc = true, quint32 seed = 1,
            QObject *parent = nullptr);

    // Vai Gateway (khung từ PC qua Ethernet có DEV + LEN sau AB CD): bỏ phần đó trước khi xử lý. Mặc định tắt vì cổng COM nối thẳng
    // (chế độ Debug COM) nhận khung gốc, mà khung gốc như CMD 01 01 hay 03 03 trông giống phần đầu DEV + LEN.
    void setGatewayHeader(bool on) { m_gatewayHeader = on; m_unwrap.reset(); }
    void setFaults(const Faults &f) { m_faults = f; }
    const Faults &faults() const { return m_faults; }
    const Stats &stats() const { return m_stats; }
    const core::FrameParser::Stats &rxStats() const { return m_parser.stats(); }
    core::Transport *transport() const { return m_transport; }

    void open() { m_transport->open(); }
    void close() { m_transport->close(); }

signals:
    void received(const QByteArray &raw);     // khung hợp lệ từ Gateway
    void sent(const QByteArray &raw);         // khung đã gửi cho Gateway
    void message(const QString &text);

private:
    void onBytes(const QByteArray &data);
    void send(QByteArray reply, bool allowFaults);

    core::Transport *m_transport;
    SimWorld *m_world;
    core::FrameRegistry m_registry;
    bool m_gatewayHeader = false;
    proto::gwhdr::Unwrapper m_unwrap;   // vai Gateway: bỏ DEV + LEN của khung PC gửi xuống (khung không có thì cho qua)
    core::FrameParser m_parser;
    Faults m_faults;
    Stats m_stats;
    QRandomGenerator m_rng;
};

} // namespace sim
