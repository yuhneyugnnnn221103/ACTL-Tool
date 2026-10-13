#include "csv_logger.h"
#include <QDateTime>
#include <QDir>

namespace services {

static const char *kTimeFormat = "yyyy-MM-dd HH:mm:ss.zzz";

CsvLogger::CsvLogger(const QString &dir, int periodMs, qint64 maxFileBytes, const proto::FieldTable *trbTable,
                     const proto::FieldTable *psuTable, QObject *parent)
    : QObject(parent), m_dir(dir), m_periodMs(periodMs), m_maxFileBytes(maxFileBytes)
{
    QStringList cols{"time", "mb", "trb", "status"};
    for (const proto::Field &f : trbTable->fields()) cols << f.name;
    m_trbHeader = cols.join(',');
    QStringList psuCols{"time", "addr", "status"};
    for (const proto::Field &f : psuTable->fields()) psuCols << f.name;
    m_psuHeader = psuCols.join(',');
    m_events.prefix = QStringLiteral("events");
    m_events.header = QStringLiteral("time,event");

    connect(&m_flushTimer, &QTimer::timeout, this, &CsvLogger::flush);
    m_flushTimer.start(2000);
    connect(&m_scheduleTimer, &QTimer::timeout, this, [this] { tick(QDateTime::currentDateTime()); });
    m_scheduleTimer.start(1000);
}

void CsvLogger::setRecording(bool on)
{
    if (on == m_recording) return;
    m_recording = on;
    if (!on) {
        closeAll(m_trb);
        closeAll(m_psu);
        m_events.file.close();
    } else {
        m_lastLogged.clear();     // mở lại thì ghi ngay bản tin đầu tiên của mỗi thiết bị
        m_lastLoggedPsu.clear();
        m_errorReported = false;
    }
    emit recordingChanged(on);
}

void CsvLogger::setSchedule(const QDateTime &startAt, const QDateTime &stopAt)
{
    m_startAt = startAt;
    m_stopAt = stopAt;
    emit scheduleChanged();
    tick(QDateTime::currentDateTime());
}

void CsvLogger::tick(const QDateTime &now)
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

void CsvLogger::closeAll(std::map<int, Stream> &streams)
{
    for (auto &kv : streams) kv.second.file.close();
}

void CsvLogger::logTrb(int mb, int trb, const model::TrbState &s, bool force)
{
    if (!m_recording) return;
    qint64 &last = m_lastLogged[mb << 8 | trb];
    if (!force && s.lastSeenMs - last < m_periodMs) return;
    last = s.lastSeenMs;

    QByteArray line = QDateTime::fromMSecsSinceEpoch(s.lastSeenMs).toString(kTimeFormat).toLatin1();
    line += ',' + QByteArray::number(mb + 1) + ',' + QByteArray::number(trb + 1) + ',' + statusText(s.status).toUtf8();
    for (double v : s.values) line += ',' + QByteArray::number(v, 'g', 10);
    Stream &st = m_trb[mb << 8 | trb];
    if (st.prefix.isEmpty()) {
        st.folder = QStringLiteral("trb");
        st.prefix = QStringLiteral("MB%1_TRB%2").arg(mb + 1, 2, 10, QLatin1Char('0')).arg(trb + 1, 2, 10, QLatin1Char('0'));
        st.header = m_trbHeader;
    }
    write(st, line);
}

void CsvLogger::logPsu(int addr, const model::PsuState &s, bool force)
{
    if (!m_recording) return;
    qint64 &last = m_lastLoggedPsu[addr];
    if (!force && s.lastSeenMs - last < m_periodMs) return;
    last = s.lastSeenMs;

    QByteArray line = QDateTime::fromMSecsSinceEpoch(s.lastSeenMs).toString(kTimeFormat).toLatin1();
    line += ',' + QByteArray::number(addr) + ',' + statusText(s.status).toUtf8();
    for (double v : s.values) line += ',' + QByteArray::number(v, 'g', 10);
    Stream &st = m_psu[addr];
    if (st.prefix.isEmpty()) {
        st.folder = QStringLiteral("psu");
        st.prefix = QStringLiteral("PSU%1").arg(addr);
        st.header = m_psuHeader;
    }
    write(st, line);
}

void CsvLogger::logEvent(const QString &text)
{
    if (!m_recording) return;
    QString quoted = text;
    quoted.replace('"', QLatin1String("\"\""));
    write(m_events, QDateTime::currentDateTime().toString(kTimeFormat).toLatin1() + ",\"" + quoted.toUtf8() + '"');
}

void CsvLogger::flushAll(std::map<int, Stream> &streams)
{
    for (auto &kv : streams)
        if (kv.second.file.isOpen()) kv.second.file.flush();
}

void CsvLogger::flush()
{
    flushAll(m_trb);
    flushAll(m_psu);
    if (m_events.file.isOpen()) m_events.file.flush();
}

bool CsvLogger::write(Stream &s, const QByteArray &line)
{
    const QDate today = QDate::currentDate();
    if (!s.file.isOpen() || s.date != today || s.file.size() >= m_maxFileBytes) {
        s.file.close();
        s.date = today;
        const QString day = today.toString(Qt::ISODate);
        const QString folder = s.folder.isEmpty() ? m_dir + '/' + day : m_dir + '/' + day + '/' + s.folder;
        QDir().mkpath(folder);
        // Phần đầu tiên chưa đầy sẽ được ghi tiếp (kể cả sau khi khởi động lại app).
        for (int part = 1;; ++part) {
            s.file.setFileName(QStringLiteral("%1/%2_%3%4.csv").arg(folder, s.prefix, day,
                               part == 1 ? QString() : QStringLiteral("_%1").arg(part, 2, 10, QLatin1Char('0'))));
            if (s.file.size() < m_maxFileBytes) break;
        }
        if (!s.file.open(QIODevice::WriteOnly | QIODevice::Append)) {
            if (!m_errorReported) emit errorOccurred(QStringLiteral("Không ghi được log: %1 (%2)")
                                                         .arg(s.file.fileName(), s.file.errorString()));
            m_errorReported = true;
            return false;
        }
        if (s.file.size() == 0) s.file.write("\xEF\xBB\xBF" + s.header.toUtf8() + "\r\n");
    }
    return s.file.write(line + "\r\n") > 0;
}

} // namespace services
