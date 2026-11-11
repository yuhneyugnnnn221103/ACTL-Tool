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

public slots:
    void onFrame(const core::Frame &frame);
    // PSU không trả lời lần hỏi (Debug COM hết thời gian chờ): chỉ tính vào thống kê lỗi. KHÔNG đặt X (X chỉ khi nhận khung rỗng);
    // không nhận được khung nào trong staleMs thì thành "?" nhờ PsuStore::markStale.
    void onSilent(int addr);

signals:
    void psuUpdated(int addr, bool statusChanged);

private:
    model::PsuStore *m_store;
    AlarmEngine *m_alarms;
    quint64 m_badAddress = 0;
};

} // namespace services
