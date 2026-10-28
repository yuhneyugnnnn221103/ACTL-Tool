#pragma once
#include "alarm_engine.h"
#include "../core/link.h"
#include "../model/psu_store.h"

namespace services {

// Nhận bản tin giám sát PSU từ link và cập nhật PsuStore. Ngưỡng và cảnh báo khóa theo (địa chỉ PSU, 0).
class PsuMonitor : public QObject {
    Q_OBJECT
public:
    static bool registerFrames(core::FrameRegistry &registry, bool checkCrc);

    PsuMonitor(core::Link *link, model::PsuStore *store, AlarmEngine *alarms = nullptr, QObject *parent = nullptr);
    quint64 badAddressFrames() const { return m_badAddress; }
    void setEmptyLimit(int n) { m_emptyLimit = qMax(1, n); }   // số khung rỗng liên tiếp của Gateway để coi PSU mất kết nối (X hiện ngay từ khung đầu)

public slots:
    void onFrame(const core::Frame &frame);

signals:
    void psuUpdated(int addr, bool statusChanged);

private:
    model::PsuStore *m_store;
    AlarmEngine *m_alarms;
    quint64 m_badAddress = 0;
    int m_emptyLimit = 3;
    QHash<int, int> m_emptyRun;
};

} // namespace services
