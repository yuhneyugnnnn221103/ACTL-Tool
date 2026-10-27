#pragma once
#include "../model/thresholds.h"
#include <QObject>
#include <QSet>

namespace services {

// So sánh giá trị giám sát với ngưỡng của chính thiết bị đó. Chỉ phát sự kiện khi một trường
// bắt đầu vượt ngưỡng hoặc trở lại bình thường, không lặp lại mỗi bản tin.
class AlarmEngine : public QObject {
    Q_OBJECT
public:
    AlarmEngine(const model::Thresholds *thresholds, const proto::FieldTable *table, QObject *parent = nullptr);

    // Trả về các trường đang vượt ngưỡng. Trường trong skip không so ngưỡng (ví dụ cụm DCM đang tắt) và bỏ cảnh báo đang có mà không phát sự kiện.
    QList<int> check(int mb, int trb, const QList<double> &values, const QSet<int> &skip = {});

signals:
    void alarmEvent(int mb, int trb, const QString &text, bool raised);

private:
    const model::Thresholds *m_thresholds;
    const proto::FieldTable *m_table;
    QHash<int, QSet<int>> m_active;
};

} // namespace services
