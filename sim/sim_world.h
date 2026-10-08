#pragma once
// Toàn bộ thiết bị giả lập sau Gateway và cách chúng phản ứng với khung nhận được từ Gateway.
#include "trb_model.h"
#include "../core/frame.h"
#include <QObject>
#include <QTimer>
#include <memory>
#include <vector>

namespace sim {

// Registry chứa các khung mà Gateway gửi xuống thiết bị (khác registry của app, vốn chứa khung trả lời).
bool registerRequestFrames(core::FrameRegistry &registry, bool checkCrc);

class SimWorld : public QObject {
    Q_OBJECT
public:
    struct Stats {
        quint64 polls = 0, controls = 0, beams = 0, configReads = 0, configWrites = 0;
        quint64 ignored = 0;        // khung gửi tới địa chỉ không tồn tại
        quint64 debugConflicts = 0; // số lần có từ hai TRB cùng ở chế độ debug
    };

    SimWorld(int mbCount, int trbPerMb, quint32 seed = 1, QObject *parent = nullptr);

    int mbCount() const { return m_mbCount; }
    int trbPerMb() const { return m_trbPerMb; }
    TrbModel *trb(int mb, int trb);
    const Stats &stats() const { return m_stats; }
    int debugCount() const;

    // Chu kỳ TRB debug tự gửi khung giám sát (không cần Gateway hỏi). 0 = tắt.
    void setDebugPeriodMs(int ms);

    // Xử lý một khung từ Gateway; trả về khung trả lời, rỗng nếu không có (hoặc thiết bị mất kết nối).
    QByteArray handle(const core::Frame &frame);

signals:
    void unsolicited(const QByteArray &frame);   // khung TRB debug tự gửi

private:
    QList<TrbModel *> targets(int mb, int trb);  // 0xFF = broadcast
    void noteDebug();

    int m_mbCount, m_trbPerMb;
    std::vector<std::unique_ptr<TrbModel>> m_trbs;
    Stats m_stats;
    QTimer m_debugTimer;
    bool m_conflict = false;
};

} // namespace sim
