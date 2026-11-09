#pragma once
#include "../core/link.h"
#include "../proto/stm_ota_proto.h"

namespace services {

// Nạp firmware STM32 cho các PSU, lần lượt từng thiết bị. Firmware đang chạy (app) không nhận firmware, chỉ bootloader recovery ('R') mới nạp:
//   INFO -> (slot 'A') ENTER_REC -> hỏi INFO lặp lại tới khi slot = 'R' -> ERASE (một ACK khi xóa xong) -> BEGIN -> SET_VERSION -> DATA... -> END (CRC32) -> COMMIT
//   -> (chờ ~1 s) INFO tới khi slot = 'A' -> GET_VERSION, so với chuỗi version đã gửi.
// Version là chuỗi ASCII (tên file .bin bỏ đuôi, <= 32 ký tự), không còn số u32. queryInfo() hỏi INFO rồi GET_VERSION.
// Thiết bị đã ở recovery sẵn (board mới, app hỏng) thì bỏ qua ENTER_REC. Một file .bin duy nhất; muốn về bản cũ thì nạp lại file cũ.
// Nếu app treo, cấp nguồn lại PSU: bootloader ở lại recovery khi nhận INFO trong 300 ms đầu, nên INFO đầu tiên được hỏi lặp lại một lúc.
class StmOta : public QObject {
    Q_OBJECT
public:
    static bool registerFrames(core::FrameRegistry &registry);
    explicit StmOta(core::Link *link, QObject *parent = nullptr);

    bool busy() const { return m_state != State::Idle; }

    void queryInfo(const QList<int> &addrs);
    void start(const QList<int> &addrs, const QByteArray &image, const QString &version, bool autoCommit);   // version rỗng = không gửi SET_VERSION
    void confirmCommit(bool commit); // trả lời cho commitRequested() khi không tự commit
    void cancel();

signals:
    void deviceInfo(int addr, char slot, quint8 trial);
    void deviceVersion(int addr, const QString &version);   // trả lời GET_VERSION; rỗng = chưa đặt
    void deviceProgress(int addr, int percent, const QString &text);
    void deviceFinished(int addr, bool ok, const QString &message);
    void commitRequested(int addr);
    void finished(int okCount, int failCount);

private:
    enum class State { Idle, Info, EnterRec, WaitRecovery, Erase, Begin, SetVersion, Data, End, AwaitUser, Commit, PostCommit, PostInfo, GetVersion };
    static constexpr int kMaxRetries = 5;
    static constexpr int kReplyMs = 200;           // chờ ACK của INFO, ENTER_REC, BEGIN, DATA, COMMIT
    static constexpr int kEraseMs = 40000;         // ERASE xóa 5 sector, một ACK khi xong; yêu cầu PC chờ >= 30 s (chưa đo), lấy dư
    static constexpr int kEndMs = 3000;            // END: firmware tính CRC32 cả ảnh
    static constexpr int kInfoPolls = 150;         // INFO đầu tiên khi nạp: hỏi lặp tới ~30 s để kịp cửa sổ 300 ms sau khi cấp nguồn lại
    static constexpr int kRecoveryPolls = 50;      // sau ENTER_REC: hỏi INFO lặp tới khi slot = 'R' (~10 s)
    static constexpr int kRecoveryFirstMs = 300;   // thiết bị reset ~50 ms sau ACK; chờ bootloader lên trước khi hỏi INFO
    static constexpr int kRecoveryRetryMs = 100;
    static constexpr int kVersionTries = 3;        // SET_VERSION / GET_VERSION: chờ ACK 200 ms, thử tối đa 3 lần
    static constexpr int kPostCommitMs = 1000;     // sau COMMIT: chờ bootloader chạy app mới rồi mới hỏi INFO

    void nextDevice();
    void send(const QByteArray &frame, State state, int timeoutMs);
    void pollInfo();
    void sendGetVersion();
    void sendSetVersion();
    void verifyDone(const QString &got);
    void beginErase();
    void sendChunk();
    void onFrame(const core::Frame &frame);
    void onTimeout();
    void done(bool ok, const QString &message);

    core::Link *m_link;
    QList<int> m_queue;
    QByteArray m_image;
    QString m_version, m_got;
    char m_slot = 0;
    bool m_autoCommit = false, m_infoOnly = false;
    State m_state = State::Idle;
    int m_addr = 0, m_seq = 0, m_chunks = 0, m_retries = 0, m_polls = 0, m_ok = 0, m_fail = 0;
    QTimer m_timer;
};

} // namespace services
