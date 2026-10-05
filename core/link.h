#pragma once
#include "frame_parser.h"
#include "transport.h"
#include <QList>
#include <QTimer>

namespace core {

struct Request {
    QByteArray data;
    QByteArray replyCmd;    // rỗng = không chờ trả lời (broadcast, hoặc trả lời ở vai mô phỏng)
    int timeoutMs = 200;
    int retries = 2;        // số lần gửi lại sau lần đầu
    int priority = 0;       // lớn hơn gửi trước; cùng mức thì theo thứ tự vào
    int gapMs = 0;          // nghỉ sau khi gửi xong (giãn gói khi broadcast)
};

// Một đường truyền logic: transport + tách khung + hàng đợi gửi (mỗi lúc một lệnh chờ trả lời).
// Sống trên thread riêng: tạo, moveToThread(), rồi chỉ gọi qua slot/send() (an toàn từ thread khác).
class Link : public QObject {
    Q_OBJECT
public:
    // Link sở hữu transport. registry phải được đăng ký xong trước khi start() và sống lâu hơn Link.
    Link(const QString &name, Transport *transport, const FrameRegistry *registry,
         QObject *parent = nullptr);

    QString name() const { return m_name; }
    Transport *transport() const { return m_transport; } // chỉ đụng tới từ thread của Link
    void setIdleResetMs(int ms) { m_idle.setInterval(ms); } // 0 = tắt; nên bật cho cổng COM

    quint64 send(const Request &request); // trả về id, kết quả báo qua requestFinished

public slots:
    void start();
    void stop();

signals:
    void stateChanged(core::Transport::State state);
    void frameReceived(const core::Frame &frame);                 // mọi khung hợp lệ
    void requestFinished(quint64 id, bool ok, const core::Frame &reply);
    void rawReceived(const QByteArray &data);                     // cho log hex
    void rawSent(const QByteArray &data);
    void errorOccurred(const QString &message);
    void rxErrorsChanged(quint64 crcErrors, quint64 droppedBytes);

private:
    struct Pending { quint64 id; Request req; int attempts = 0; };

    void enqueue(quint64 id, const Request &request);
    void pump();
    void transmit();
    void finish(bool ok, const Frame &reply = {});
    void onBytes(const QByteArray &data);
    void onTimeout();

    QString m_name;
    Transport *m_transport;
    FrameParser m_parser;
    QList<Pending> m_queue;
    Pending m_current;
    bool m_busy = false;
    QTimer m_timer; // timeout trả lời hoặc khoảng nghỉ sau khi gửi
    QTimer m_idle;  // đường truyền im lặng -> bỏ khung dở
    QAtomicInteger<quint64> m_nextId{1};
    quint64 m_reportedErrors = 0;
};

} // namespace core
