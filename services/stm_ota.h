#pragma once
#include "../core/link.h"
#include "../proto/stm_ota_proto.h"

namespace services {

// Nạp firmware STM32 cho các PSU, lần lượt từng thiết bị:
// INFO (biết đang chạy slot nào) -> BEGIN -> chờ xóa -> DATA... -> END (CRC32) -> COMMIT.
class StmOta : public QObject {
    Q_OBJECT
public:
    static bool registerFrames(core::FrameRegistry &registry);
    explicit StmOta(core::Link *link, QObject *parent = nullptr);

    bool busy() const { return m_state != State::Idle; }

    void queryInfo(const QList<int> &addrs);
    // imageA/imageB: file .bin link cho slot A/B. Thiết bị đang chạy slot A sẽ nhận imageB và ngược lại.
    void start(const QList<int> &addrs, const QByteArray &imageA, const QByteArray &imageB,
               quint32 version, bool autoCommit);
    void confirmCommit(bool commit); // trả lời cho commitRequested() khi không tự commit
    void cancel();

signals:
    void deviceInfo(int addr, char slot, quint32 version);
    void deviceProgress(int addr, int percent, const QString &text);
    void deviceFinished(int addr, bool ok, const QString &message);
    void commitRequested(int addr);
    void finished(int okCount, int failCount);

private:
    enum class State { Idle, Info, BeginAccept, BeginReady, Data, End, AwaitUser, Commit };
    static constexpr int kMaxRetries = 5;

    void nextDevice();
    void send(const QByteArray &frame, State state, int timeoutMs);
    void sendChunk();
    void onFrame(const core::Frame &frame);
    void onTimeout();
    void done(bool ok, const QString &message);

    core::Link *m_link;
    QList<int> m_queue;
    QByteArray m_imageA, m_imageB, m_image;
    quint32 m_version = 0;
    bool m_autoCommit = false, m_infoOnly = false;
    State m_state = State::Idle;
    int m_addr = 0, m_seq = 0, m_chunks = 0, m_retries = 0, m_ok = 0, m_fail = 0;
    QTimer m_timer;
};

} // namespace services
