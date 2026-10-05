#pragma once
// Ghi log CSV (UTF-8 có BOM để Excel đọc đúng tiếng Việt):
//   <dir>/yyyy-MM-dd/trb_yyyy-MM-dd[_NN].csv     một dòng = một bản tin giám sát TRB
//   <dir>/yyyy-MM-dd/events_yyyy-MM-dd[_NN].csv  một dòng = một sự kiện
// Sang ngày mới hoặc file vượt maxFileBytes thì mở file mới.
#include "../model/device_store.h"
#include "../proto/field_table.h"
#include <QDate>
#include <QFile>
#include <QHash>
#include <QTimer>

namespace services {

class CsvLogger : public QObject {
    Q_OBJECT
public:
    CsvLogger(const QString &dir, int periodMs, qint64 maxFileBytes, const proto::FieldTable *trbTable,
              QObject *parent = nullptr);

    QString dir() const { return m_dir; }
    // Ghi nếu đã quá periodMs kể từ lần ghi trước của thiết bị này, hoặc force (đổi trạng thái).
    void logTrb(int mb, int trb, const model::TrbState &state, bool force);
    void logEvent(const QString &text);
    void flush();

signals:
    void errorOccurred(const QString &message);

private:
    struct Stream { QString prefix, header; QFile file; QDate date; };
    bool write(Stream &s, const QByteArray &line);

    QString m_dir;
    int m_periodMs;
    qint64 m_maxFileBytes;
    const proto::FieldTable *m_trbTable;
    Stream m_trb, m_events;
    QHash<int, qint64> m_lastLogged;
    QTimer m_flushTimer;
    bool m_errorReported = false;
};

} // namespace services
