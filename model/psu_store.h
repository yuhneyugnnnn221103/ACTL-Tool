#pragma once
// Trạng thái mới nhất của các PSU, địa chỉ liên tiếp từ firstAddr. Chỉ truy cập từ thread UI.
#include "device_store.h"

namespace model {

struct PsuState {
    Status status = Status::NoData;
    Status lastLive = Status::NoData;   // trạng thái gần nhất khác Lost / NoData
    qint64 lastSeenMs = 0;
    quint64 frames = 0;
    QList<double> values;   // theo thứ tự bảng trường proto::psumon::table()
    QList<int> alarms;      // chỉ số các trường đang vượt ngưỡng
    QByteArray raw;
};

class PsuStore : public QObject {
    Q_OBJECT
public:
    PsuStore(int firstAddr, int count, QObject *parent = nullptr);

    int firstAddr() const { return m_firstAddr; }
    int count() const { return m_psus.size(); }
    bool contains(int addr) const { return addr >= m_firstAddr && addr < m_firstAddr + m_psus.size(); }
    const PsuState &psu(int addr) const { return m_psus.at(addr - m_firstAddr); }

    // Trạng thái một cụm DCM (0..3): mất kết nối/chưa có dữ liệu theo PSU, Quá ngưỡng nếu có giá trị của cụm
    // vượt ngưỡng, còn lại Tốt. Trip chưa gán được cho cụm vì chưa có bảng trip code, nên không hiện ở đây.
    Status clusterStatus(int addr, int cluster) const;
    QList<int> clusterAlarms(int addr, int cluster) const;
    // Trạng thái để tô màu ô: mất kết nối thì lấy theo số liệu cuối (cụm: Quá ngưỡng hoặc Tốt; PSU: Trip > Quá ngưỡng > Tốt).
    Status clusterDisplayStatus(int addr, int cluster) const;
    Status displayStatus(int addr) const;
    bool isLost(int addr) const { return psu(addr).status == Status::Lost; } // chỉ số trường (trong bảng giám sát) đang vượt ngưỡng

    void updatePsu(int addr, const QList<double> &values, const QByteArray &raw, qint64 timestampMs,
                   Status status, const QList<int> &alarms = {});
    void markStale(qint64 nowMs, int staleMs); // quá hạn -> Lost

signals:
    void psuStatusChanged(int addr, model::Status from, model::Status to);

private:
    void setStatus(int addr, Status s);

    int m_firstAddr;
    QList<PsuState> m_psus;
};

} // namespace model
