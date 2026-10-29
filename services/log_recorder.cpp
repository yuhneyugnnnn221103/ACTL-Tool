#include "log_recorder.h"
#include <QDir>

namespace services {

LogRecorder::LogRecorder(const QString &dir, int minIntervalMs, QObject *parent)
    : QObject(parent), m_dir(dir), m_minIntervalMs(minIntervalMs)
{
    m_startedAt = QDateTime::currentDateTime();
    connect(&m_flushTimer, &QTimer::timeout, this, &LogRecorder::flush);
    m_flushTimer.start(2000);      // khối nào đã gom được thì ghi xuống đĩa mỗi 2 s: tắt đột ngột chỉ mất vài giây cuối
    connect(&m_scheduleTimer, &QTimer::timeout, this, [this] { tick(QDateTime::currentDateTime()); });
    m_scheduleTimer.start(1000);
}

LogRecorder::~LogRecorder() { closeFile(); }

void LogRecorder::setRecording(bool on)
{
    if (on == m_recording) return;
    m_recording = on;
    if (!on) {
        closeFile();
    } else {
        m_startedAt = QDateTime::currentDateTime();
        m_lastLogged.clear();     // mở lại thì ghi ngay bản tin đầu tiên của mỗi thiết bị
        m_errorReported = false;
    }
    emit recordingChanged(on);
}

void LogRecorder::setSchedule(const QDateTime &startAt, const QDateTime &stopAt)
{
    m_startAt = startAt;
    m_stopAt = stopAt;
    emit scheduleChanged();
    tick(QDateTime::currentDateTime());
}

void LogRecorder::tick(const QDateTime &now)
{
    bool changed = false;
    if (m_startAt.isValid() && now >= m_startAt) {
        m_startAt = QDateTime();
        changed = true;
        if (!(m_stopAt.isValid() && now >= m_stopAt)) setRecording(true);   // đã quá cả mốc dừng thì không bật
    }
    if (m_stopAt.isValid() && now >= m_stopAt) {
        m_stopAt = QDateTime();
        changed = true;
        setRecording(false);
    }
    if (changed) emit scheduleChanged();
}

// Tạo file khi có bản ghi đầu tiên của lần ghi; tên chứa thời điểm bắt đầu ghi (không phải thời điểm bản ghi đầu).
bool LogRecorder::ensureOpen(qint64 nowMs)
{
    Q_UNUSED(nowMs)
    if (m_writer.isOpen()) return true;
    QDir().mkpath(m_dir);
    QString error;
    const QString path = m_dir + '/' + logfile::fileName(m_startedAt, QDateTime());
    if (!m_writer.open(path, m_startedAt.toMSecsSinceEpoch(), &error)) {
        if (!m_errorReported) emit errorOccurred(QStringLiteral("Không ghi được log: %1 (%2)").arg(path, error));
        m_errorReported = true;
        return false;
    }
    return true;
}

// Đóng file và đổi tên "dang-ghi" thành thời điểm kết thúc. File không có bản ghi nào thì xóa.
void LogRecorder::closeFile()
{
    if (!m_writer.isOpen()) return;
    const QString path = m_writer.path();
    const bool empty = m_writer.records() == 0;
    m_writer.close();
    if (empty) { QFile::remove(path); return; }
    const QDateTime end = QDateTime::currentDateTime();
    QString target = m_dir + '/' + logfile::fileName(m_startedAt, end);
    for (int n = 2; QFile::exists(target); ++n)             // hai lần ghi cùng giây bắt đầu và kết thúc
        target = m_dir + '/' + logfile::fileName(m_startedAt, end).replace(QStringLiteral(".alog"), QStringLiteral("-%1.alog").arg(n));
    if (!QFile::rename(path, target)) emit errorOccurred(QStringLiteral("Không đổi được tên file log: %1").arg(path));
}

void LogRecorder::logTrb(int mb, int trb, const model::TrbState &s, bool force)
{
    if (!m_recording || s.raw.isEmpty()) return;
    qint64 &last = m_lastLogged[logfile::deviceKey(logfile::KindTrb, mb, trb)];
    if (!force && m_minIntervalMs > 0 && s.lastSeenMs - last < m_minIntervalMs) return;
    if (!ensureOpen(s.lastSeenMs)) return;
    last = s.lastSeenMs;
    m_writer.addTrb(s.lastSeenMs, mb, trb, int(s.status), s.raw);
}

void LogRecorder::logPsu(int addr, const model::PsuState &s, bool force)
{
    if (!m_recording || s.raw.isEmpty()) return;
    qint64 &last = m_lastLogged[logfile::deviceKey(logfile::KindPsu, addr)];
    if (!force && m_minIntervalMs > 0 && s.lastSeenMs - last < m_minIntervalMs) return;
    if (!ensureOpen(s.lastSeenMs)) return;
    last = s.lastSeenMs;
    m_writer.addPsu(s.lastSeenMs, addr, int(s.status), s.raw);
}

void LogRecorder::logEvent(const QString &text)
{
    if (!m_recording) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!ensureOpen(now)) return;
    m_writer.addEvent(now, text);
}

void LogRecorder::flush() { if (m_writer.isOpen()) m_writer.flush(); }

} // namespace services
