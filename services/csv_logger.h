#pragma once
// Ghi log CSV (UTF-8 có BOM để Excel đọc đúng tiếng Việt):
//   <dir>/yyyy-MM-dd/trb/MB03_TRB05_<phiên>[_NN].csv   mỗi TRB một file, một dòng = một bản tin giám sát (MB, TRB đếm từ 1)
//   <dir>/yyyy-MM-dd/psu/PSU2_<phiên>[_NN].csv         mỗi PSU một file
//   <dir>/yyyy-MM-dd/events_<phiên>[_NN].csv           một dòng = một sự kiện (chung)
// <phiên> = giờ bắt đầu lần ghi (yyyyMMdd-HHmmss): mỗi lần bắt đầu ghi (mở app, bấm nút, hẹn giờ) là một bộ file mới,
// nên các lần ghi không nối vào nhau. File của thiết bị chỉ được tạo khi thiết bị có dữ liệu.
// Sang ngày mới hoặc file vượt maxFileBytes thì mở file tiếp theo.
#include "../model/device_store.h"
#include "../model/psu_store.h"
#include "../proto/field_table.h"
#include <QDate>
#include <QDateTime>
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

    // Bật/tắt ghi. Khi tắt (thủ công hoặc theo hẹn giờ) mọi file được đóng; các lời gọi log* bị bỏ qua.
    bool recording() const { return m_recording; }
    void setRecording(bool on);
    // Hẹn giờ: tới startAt thì bật, tới stopAt thì tắt (QDateTime không hợp lệ = không hẹn). Mỗi mốc chỉ dùng một lần.
    void setSchedule(const QDateTime &startAt, const QDateTime &stopAt);
    QDateTime startAt() const { return m_startAt; }
    QDateTime stopAt() const { return m_stopAt; }
    void tick(const QDateTime &now); // kiểm tra hẹn giờ; timer nội bộ gọi mỗi giây
    // Ghi nếu đã quá periodMs kể từ lần ghi trước của thiết bị này, hoặc force (đổi trạng thái).
    void logTrb(int mb, int trb, const model::TrbState &state, bool force);
    void logPsu(int addr, const model::PsuState &state, bool force);
    void logEvent(const QString &text);
    void flush();

signals:
    void recordingChanged(bool on);
    void scheduleChanged();
    void errorOccurred(const QString &message);

private:
    struct Stream { QString folder, prefix, header; QFile file; QDate date; };
    bool write(Stream &s, const QByteArray &line);
    static void closeAll(std::map<int, Stream> &streams);
    static void flushAll(std::map<int, Stream> &streams);

    QString m_dir;
    int m_periodMs;
    qint64 m_maxFileBytes;
    QString m_trbHeader, m_psuHeader;
    std::map<int, Stream> m_trb, m_psu;   // khóa: mb << 8 | trb, hoặc địa chỉ PSU
    Stream m_events;
    QHash<int, qint64> m_lastLogged, m_lastLoggedPsu;
    QTimer m_flushTimer, m_scheduleTimer;
    bool m_recording = true;
    QString m_session, m_lastStamp;
    int m_sessionDup = 0;
    void newSession();
    QDateTime m_startAt, m_stopAt;
    bool m_errorReported = false;
};

} // namespace services
