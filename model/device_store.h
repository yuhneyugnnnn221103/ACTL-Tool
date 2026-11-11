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

// Thống kê trả lời giám sát của một thiết bị: bản tin có dữ liệu so với khung rỗng (toàn 00, Gateway báo thiết bị không trả lời).
// Tỉ lệ lỗi tính theo cửa sổ trượt kWindow lần trả lời gần nhất; tổng cộng dồn giữ riêng để xem lâu dài.
struct ReplyStats {
    static constexpr int kWindow = 100;
    quint64 okTotal = 0, emptyTotal = 0;
    int window = 0, windowEmpty = 0;    // số lần trả lời đang nằm trong cửa sổ, và trong đó số khung rỗng
    void add(bool empty);
    int count() const { return window; }
    double rate() const { return window ? double(windowEmpty) / window : 0.0; }
    void reset() { *this = ReplyStats(); }
private:
    quint8 m_ring[kWindow] = {};
    int m_pos = 0;
};

struct TrbState {
    ReplyStats reply;
    Status status = Status::NoData;
    Status lastLive = Status::NoData;   // trạng thái gần nhất khác Lost (Ok, Warning, Trip, Updating): biết trước khi mất kết nối là gì
    qint64 lastSeenMs = 0;
    quint64 frames = 0;
    bool noReply = false;   // X: vừa nhận khung RỖNG (toàn 00) của Gateway cho TRB này và chưa có bản tin thật nào sau đó; tự hết khi quá hạn không nhận thêm khung nào
    qint64 lastRxMs = 0;    // lần nhận khung rỗng gần nhất (khung thật thì lastSeenMs): còn nhận được khung thì chưa phải "?"
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
    // Các điều kiện của một TRB (Trip, Warning, Lost, hoặc Ok / NoData / Updating nếu không có gì bất thường).
    QList<Status> conditions(int mb, int trb) const;
    // Trạng thái dùng để tô màu ô: mất kết nối thì lấy theo số liệu cuối (Trip > Quá ngưỡng > Tốt), không có màu riêng cho "mất".
    Status displayStatus(int mb, int trb) const;

    void updateTrb(int mb, int trb, const QList<double> &values, const QByteArray &raw,
                   qint64 timestampMs, Status status, const QList<int> &alarms = {});
    void markStale(qint64 nowMs, int staleMs); // quá hạn -> Lost
    void recordReply(int mb, int trb, bool empty);
    void resetReplyStats();
    void setNoReply(int mb, int trb, bool on); // đặt/xóa cờ noReply (xem TrbState)
    void markEmpty(int mb, int trb, qint64 timestampMs);   // nhận khung rỗng: đặt X và ghi nhận còn nhận được khung (không chuyển Lost)

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
