#pragma once
// Ghi log: mỗi lần bắt đầu ghi (mở app, bấm nút, hẹn giờ) tạo đúng một file nhị phân nén ACTL_<bắt đầu>_<kết thúc>.alog trong thư mục log
// (định dạng ở log_file.h). Ghi sau mỗi bản tin giám sát nhận được; các sự kiện của app ghi chung vào file. File được tạo khi có bản ghi đầu tiên,
// đang ghi thì tên có "dang-ghi" ở phần kết thúc và được đổi tên khi dừng.
#include "log_file.h"
#include <QDateTime>
#include <QTimer>

namespace services {

class LogRecorder : public QObject {
    Q_OBJECT
public:
    // minIntervalMs: 0 = ghi mọi bản tin nhận được; > 0 = tối thiểu chừng đó ms giữa hai mẫu của một thiết bị (đổi trạng thái thì ghi ngay).
    LogRecorder(const QString &dir, int minIntervalMs, QObject *parent = nullptr);
    ~LogRecorder() override;

    QString dir() const { return m_dir; }
    QString currentFile() const { return m_writer.isOpen() ? m_writer.path() : QString(); }

    // Bật/tắt ghi. Khi tắt (thủ công hoặc theo hẹn giờ) file được đóng và đổi tên kèm thời điểm kết thúc; các lời gọi log* bị bỏ qua.
    bool recording() const { return m_recording; }
    void setRecording(bool on);
    // Hẹn giờ: tới startAt thì bật, tới stopAt thì tắt (QDateTime không hợp lệ = không hẹn). Mỗi mốc chỉ dùng một lần.
    void setSchedule(const QDateTime &startAt, const QDateTime &stopAt);
    QDateTime startAt() const { return m_startAt; }
    QDateTime stopAt() const { return m_stopAt; }
    void tick(const QDateTime &now); // kiểm tra hẹn giờ; timer nội bộ gọi mỗi giây

    void logTrb(int mb, int trb, const model::TrbState &state, bool force);
    void logPsu(int addr, const model::PsuState &state, bool force);
    void logEvent(const QString &text);
    void flush();

signals:
    void recordingChanged(bool on);
    void scheduleChanged();
    void errorOccurred(const QString &message);

private:
    bool ensureOpen(qint64 nowMs);
    void closeFile();

    QString m_dir;
    int m_minIntervalMs;
    logfile::Writer m_writer;
    QHash<int, qint64> m_lastLogged;       // khóa deviceKey
    QTimer m_flushTimer, m_scheduleTimer;
    bool m_recording = true;
    QDateTime m_startedAt;                  // thời điểm bắt đầu lần ghi hiện tại (dùng đặt tên file)
    QDateTime m_startAt, m_stopAt;
    bool m_errorReported = false;
};

} // namespace services
