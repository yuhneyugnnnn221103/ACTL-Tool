#pragma once
#include "alarm_engine.h"
#include "../core/link.h"
#include "../model/device_store.h"
#include <QHash>

namespace services {

// Nhận bản tin giám sát TRB từ link và cập nhật DeviceStore. Sống ở thread UI.
class TrbMonitor : public QObject {
    Q_OBJECT
public:
    static bool registerFrames(core::FrameRegistry &registry, bool checkCrc, bool acceptLegacyCmd = true);   // acceptLegacyCmd: nhận cả CMD cũ 11 11

    TrbMonitor(core::Link *link, model::DeviceStore *store, AlarmEngine *alarms = nullptr,
               QObject *parent = nullptr);
    quint64 badAddressFrames() const { return m_badAddress; }
    void setEmptyLimit(int n) { m_emptyLimit = qMax(1, n); }   // số khung rỗng liên tiếp của Gateway để coi TRB mất kết nối

public slots:
    void onFrame(const core::Frame &frame);

signals:
    void trbUpdated(int mb, int trb, bool statusChanged);
    void powerGoodChanged(int mb, int trb, int faultBits);   // bit i = PG của TRM(i+1) đang lỗi; 0 = hết lỗi

private:
    model::DeviceStore *m_store;
    AlarmEngine *m_alarms;
    quint64 m_badAddress = 0;
    int m_emptyLimit = 3;
    QHash<int, int> m_emptyRun;   // khóa mb << 8 | trb: số khung rỗng liên tiếp
    QHash<int, int> m_pgFault;   // khóa mb << 8 | trb: mặt nạ PG lỗi của bản tin trước
};

} // namespace services
