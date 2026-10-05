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
    m_trb.prefix = QStringLiteral("trb");
    m_trb.header = cols.join(',');
    QStringList psuCols{"time", "addr", "status"};
    for (const proto::Field &f : psuTable->fields()) psuCols << f.name;
    m_psu.prefix = QStringLiteral("psu");
    m_psu.header = psuCols.join(',');
    m_events.prefix = QStringLiteral("events");
    m_events.header = QStringLiteral("time,event");

    connect(&m_flushTimer, &QTimer::timeout, this, &CsvLogger::flush);
    m_flushTimer.start(2000);
}

void CsvLogger::logTrb(int mb, int trb, const model::TrbState &s, bool force)
{
    qint64 &last = m_lastLogged[mb << 8 | trb];
    if (!force && s.lastSeenMs - last < m_periodMs) return;
    last = s.lastSeenMs;

    QByteArray line = QDateTime::fromMSecsSinceEpoch(s.lastSeenMs).toString(kTimeFormat).toLatin1();
    line += ',' + QByteArray::number(mb) + ',' + QByteArray::number(trb) + ',' + statusText(s.status).toUtf8();
    for (double v : s.values) line += ',' + QByteArray::number(v, 'g', 10);
    write(m_trb, line);
}

void CsvLogger::logPsu(int addr, const model::PsuState &s, bool force)
{
    qint64 &last = m_lastLoggedPsu[addr];
    if (!force && s.lastSeenMs - last < m_periodMs) return;
    last = s.lastSeenMs;

    QByteArray line = QDateTime::fromMSecsSinceEpoch(s.lastSeenMs).toString(kTimeFormat).toLatin1();
    line += ',' + QByteArray::number(addr) + ',' + statusText(s.status).toUtf8();
    for (double v : s.values) line += ',' + QByteArray::number(v, 'g', 10);
    write(m_psu, line);
}

void CsvLogger::logEvent(const QString &text)
{
    QString quoted = text;
    quoted.replace('"', QLatin1String("\"\""));
    write(m_events, QDateTime::currentDateTime().toString(kTimeFormat).toLatin1() + ",\"" + quoted.toUtf8() + '"');
}

void CsvLogger::flush()
{
    if (m_trb.file.isOpen()) m_trb.file.flush();
    if (m_psu.file.isOpen()) m_psu.file.flush();
    if (m_events.file.isOpen()) m_events.file.flush();
}

bool CsvLogger::write(Stream &s, const QByteArray &line)
{
    const QDate today = QDate::currentDate();
    if (!s.file.isOpen() || s.date != today || s.file.size() >= m_maxFileBytes) {
        s.file.close();
        s.date = today;
        const QString day = today.toString(Qt::ISODate);
        const QString folder = m_dir + '/' + day;
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
