#pragma once
#include "../core/link.h"
#include "../proto/fpga_ota_proto.h"
#include <QPair>

namespace services {

// Nạp bitstream cho FPGA trên TRB. Ba bước do người dùng bấm lần lượt:
//   1. checkOnline()   hỏi trạng thái từng TRB
//   2. eraseAndLoad()  xóa flash, chờ, gửi các gói
//   3. bootAndVerify() yêu cầu boot rồi hỏi lại từng TRB
class FpgaOta : public QObject {
    Q_OBJECT
public:
    using Device = QPair<int, int>;

    struct Node {
        Device dev;
        bool online = false, failed = false;
        proto::fpgaota::Status status;
        QString note;
    };
    struct Options {
        bool broadcast = true;        // true: gửi FF/FF tới MỌI TRB; false: gửi riêng cho từng TRB trong danh sách
        bool perPacketCheck = false;  // chỉ có nghĩa khi broadcast; chế độ gửi riêng luôn kiểm tra từng gói
        int eraseWaitSec = 420;
        int packetGapMs = 5;          // nghỉ sau mỗi gói để FPGA kịp ghi flash
        int bootWaitMs = 3000;
    };

    static bool registerFrames(core::FrameRegistry &registry);
    explicit FpgaOta(core::Link *link, QObject *parent = nullptr);

    bool busy() const { return m_phase != Phase::Idle; }
    const QList<Node> &nodes() const { return m_nodes; }
    void setNodes(const QList<Device> &devices);

    void checkOnline();
    void eraseAndLoad(const QByteArray &firmware, const Options &options);
    void skipEraseWait();
    void bootAndVerify(const Options &options);
    void cancel();

signals:
    void nodeChanged(int index);
    void phaseChanged(const QString &text);
    void progress(int done, int total);
    void finished(const QString &summary);

private:
    enum class Phase { Idle, Online, EraseWait, LoadBroadcast, LoadNode, PostCheck, BootWait, Verify };
    static constexpr int kMaxTries = 3;

    bool usable(const Node &n) const { return n.online && !n.failed; }
    bool nextNode(bool onlyUsable);
    void query();
    void send(const QByteArray &frame, int gapMs, bool wait);
    void failNode(const QString &note);
    void loadPacket();
    void loadNextNode();
    void sendPacketTo(const Node &n);
    void finish(const QString &summary);
    void onRequestFinished(quint64 id, bool ok, const core::Frame &reply);
    int countUsable() const;

    core::Link *m_link;
    QList<Node> m_nodes;
    Options m_opt;
    QByteArray m_firmware;
    Phase m_phase = Phase::Idle;
    quint64 m_waitId = 0;
    int m_node = -1, m_packet = 0, m_totalPackets = 0, m_tries = 0, m_countdown = 0;
    bool m_cancel = false;
    QTimer m_timer;
};

} // namespace services
