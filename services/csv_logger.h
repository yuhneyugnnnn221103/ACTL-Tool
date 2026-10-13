#pragma once
// Ghi log CSV (UTF-8 có BOM để Excel đọc đúng tiếng Việt):
//   <dir>/yyyy-MM-dd/trb/MB03_TRB05_yyyy-MM-dd[_NN].csv   mỗi TRB một file, một dòng = một bản tin giám sát (MB, TRB đếm từ 1)
//   <dir>/yyyy-MM-dd/psu/PSU2_yyyy-MM-dd[_NN].csv         mỗi PSU một file
//   <dir>/yyyy-MM-dd/events_yyyy-MM-dd[_NN].csv           một dòng = một sự kiện (chung)
// File của thiết bị chỉ được tạo khi thiết bị có dữ liệu. Sang ngày mới hoặc file vượt maxFileBytes thì mở file mới.
#include "../model/device_store.h"
#include "../model/psu_store.h"
#include "../proto/field_table.h"
#include <QDate>
#include <QFile>
#include <QHash>
#include <QTimer>
#include <map>

namespace services {

class CsvLogger : public QObject {
    Q_OBJECT
public:
    CsvLogger(const QString &dir, int periodMs, qint64 maxFileBytes, const proto::FieldTable *trbTable,
              const proto::FieldTable *psuTable, QObject *parent = nullptr);

    QString dir() const { return m_dir; }
    // Ghi nếu đã quá periodMs kể từ lần ghi trước của thiết bị này, hoặc force (đổi trạng thái).
    void logTrb(int mb, int trb, const model::TrbState &state, bool force);
    void logPsu(int addr, const model::PsuState &state, bool force);
    void logEvent(const QString &text);
    void flush();

signals:
    void errorOccurred(const QString &message);

private:
    struct Stream { QString folder, prefix, header; QFile file; QDate date; };
    bool write(Stream &s, const QByteArray &line);
    static void flushAll(std::map<int, Stream> &streams);

    QString m_dir;
    int m_periodMs;
    qint64 m_maxFileBytes;
    QString m_trbHeader, m_psuHeader;
    std::map<int, Stream> m_trb, m_psu;   // khóa: mb << 8 | trb, hoặc địa chỉ PSU
    Stream m_events;
    QHash<int, qint64> m_lastLogged, m_lastLoggedPsu;
    QTimer m_flushTimer;
    bool m_errorReported = false;
};

} // namespace services
