#pragma once
// Trạng thái mới nhất của mọi thiết bị. Chỉ truy cập từ thread UI.
#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QObject>

namespace model {

enum class Status { NoData, Ok, Warning, Trip, Lost, Updating };
constexpr int kStatusCount = 6;
QString statusText(Status s);

struct TrbState {
    Status status = Status::NoData;
    Status lastLive = Status::NoData;   // trạng thái gần nhất khác Lost (Ok, Warning, Trip, Updating): biết trước khi mất kết nối là gì
    qint64 lastSeenMs = 0;
    quint64 frames = 0;
    QList<double> values;   // theo thứ tự bảng trường proto::trbmon::table()
    QList<int> alarms;      // chỉ số các trường đang vượt ngưỡng
    QByteArray raw;
};

class DeviceStore : public QObject {
    Q_OBJECT
public:
    DeviceStore(int mbCount, int trbPerMb, QObject *parent = nullptr);

    int mbCount() const { return m_mbCount; }
    int trbPerMb() const { return m_trbPerMb; }
    bool contains(int mb, int trb) const { return mb >= 0 && mb < m_mbCount && trb >= 0 && trb < m_trbPerMb; }
    const TrbState &trb(int mb, int trb) const { return m_trbs.at(mb * m_trbPerMb + trb); }
    int count(Status s) const { return m_counts[int(s)]; }
    // Đếm theo "điều kiện": một TRB có thể thuộc nhiều nhóm cùng lúc (Trip kèm quá ngưỡng; mất kết nối nhưng trước đó đang
    // Trip hoặc quá ngưỡng), nên tổng các nhóm có thể vượt số TRB. Ok, Lost, NoData loại trừ nhau như Status.
    int countCondition(Status s) const;

    void updateTrb(int mb, int trb, const QList<double> &values, const QByteArray &raw,
                   qint64 timestampMs, Status status, const QList<int> &alarms = {});
    void markStale(qint64 nowMs, int staleMs); // quá hạn -> Lost

signals:
    void trbStatusChanged(int mb, int trb, model::Status from, model::Status to);

private:
    void setStatus(int mb, int trb, Status s);

    int m_mbCount, m_trbPerMb;
    QList<TrbState> m_trbs;
    int m_counts[kStatusCount] = {};
};

} // namespace model
Q_DECLARE_METATYPE(model::Status)
