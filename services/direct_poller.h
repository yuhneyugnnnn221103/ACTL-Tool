#pragma once
#include "../core/link.h"
#include "psu_monitor.h"
#include "trb_monitor.h"
#include <QTimer>
#include <functional>

namespace services {

// Chế độ Debug COM: không có Gateway tự hỏi thiết bị, nên PC chủ động gửi lệnh hỏi giám sát cho MỘT thiết bị (TRB hoặc PSU) và chờ trả lời.
// Trả lời có dữ liệu đi qua đường bình thường (Link -> Monitor -> Store). Hết thời gian chờ thì báo cho monitor như một khung rỗng,
// nên cơ chế X / ? / mất kết nối giống hệt khi có Gateway. Sống ở thread UI.
class DirectPoller : public QObject {
    Q_OBJECT
public:
    enum class Kind { Trb, Psu };

    DirectPoller(core::Link *link, TrbMonitor *trb, PsuMonitor *psu, QObject *parent = nullptr);

    void setTarget(Kind kind, int a, int b = 0);   // TRB: a = MB, b = TRB; PSU: a = địa chỉ
    void setPeriodMs(int ms);                      // chu kỳ hỏi (tối thiểu 10 ms); đổi khi đang chạy có hiệu lực ngay
    void setReplyTimeoutMs(int ms) { m_timeoutMs = ms; }
    // Trả về true thì tạm nghỉ hỏi (đang cấu hình / nạp code dùng chung đường truyền: không chen lệnh hỏi vào giữa).
    void setPauseCheck(std::function<bool()> f) { m_pause = std::move(f); }

    bool running() const { return m_timer.isActive(); }
    QByteArray query() const;                      // lệnh hỏi của đích hiện tại
    quint64 sent() const { return m_sent; }
    quint64 replied() const { return m_replied; }
    quint64 timeouts() const { return m_timeouts; }
    void resetStats();

public slots:
    void start();
    void stop();
    void pollOnce();                               // hỏi ngay một lần (bỏ qua nếu lệnh trước chưa xong, hoặc chưa kết nối)

signals:
    void statsChanged();
    void runningChanged(bool running);

private:
    void onFinished(quint64 id, bool ok);

    core::Link *m_link;
    TrbMonitor *m_trb;
    PsuMonitor *m_psu;
    QTimer m_timer;
    std::function<bool()> m_pause;
    Kind m_kind = Kind::Trb;
    int m_a = 0, m_b = 0, m_timeoutMs = 200;
    quint64 m_pending = 0;
    bool m_connected = false;
    quint64 m_sent = 0, m_replied = 0, m_timeouts = 0;
};

} // namespace services
