#pragma once
#include "../core/link.h"
#include "../model/thresholds.h"

namespace services {

// Đọc/ghi cấu hình PSU qua link RS485, lần lượt từng PSU. Ghi không có ACK nên luôn đọc lại để kiểm tra.
// Mỗi lần đọc được cấu hình thì cập nhật ngưỡng cảnh báo (khóa theo địa chỉ PSU).
class PsuConfig : public QObject {
    Q_OBJECT
public:
    static bool registerFrames(core::FrameRegistry &registry, bool checkCrc);

    PsuConfig(core::Link *link, model::Thresholds *thresholds, const QString &thresholdsFile,
              int readTimeoutMs, int writeGapMs, QObject *parent = nullptr);

    bool busy() const { return m_stage != Stage::Idle; }
    const QList<double> *cached(int addr) const; // cấu hình đọc được gần nhất, nullptr nếu chưa có

    void readDevices(const QList<int> &addrs);
    void writeFull(int addr, const QList<double> &values);                          // ghi cả khung rồi kiểm tra
    void writeFullMany(const QList<int> &addrs, const QList<double> &values);       // cùng một khung cho nhiều PSU
    void applyChanges(const QList<int> &addrs, const QHash<int, double> &changes);  // đọc - sửa - ghi - kiểm tra
    void cancel();

signals:
    void configRead(int addr);
    void deviceFinished(int addr, bool ok, const QString &message);
    void progress(int done, int total);
    void finished(int okCount, int failCount);

private:
    enum class Stage { Idle, Read, Write, Verify };
    struct Task { int addr = 0; bool write = false; QList<double> values; }; // values rỗng = đọc rồi áp m_changes

    void start(const QList<Task> &tasks);
    void next();
    void sendRead(Stage stage);
    void sendWrite();
    void onRequestFinished(quint64 id, bool ok, const core::Frame &reply);
    void done(bool ok, const QString &message);

    core::Link *m_link;
    model::Thresholds *m_thresholds;
    QString m_thresholdsFile;
    int m_readTimeoutMs, m_writeGapMs;

    QHash<int, QList<double>> m_cache;
    QList<Task> m_tasks;
    QHash<int, double> m_changes;
    Task m_task;
    Stage m_stage = Stage::Idle;
    quint64 m_waitId = 0;
    int m_total = 0, m_ok = 0, m_fail = 0;
};

} // namespace services
