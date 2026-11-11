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
    static bool registerFrames(core::FrameRegistry &registry, bool checkCrc);

    TrbMonitor(core::Link *link, model::DeviceStore *store, AlarmEngine *alarms = nullptr,
               QObject *parent = nullptr);
    quint64 badAddressFrames() const { return m_badAddress; }

public slots:
    void onFrame(const core::Frame &frame);
    // TRB không trả lời lần hỏi (Debug COM hết thời gian chờ): chỉ tính vào thống kê lỗi. KHÔNG đặt X (X chỉ khi nhận khung rỗng);
    // không nhận được khung nào trong staleMs thì thành "?" nhờ DeviceStore::markStale.
    void onSilent(int mb, int trb);

signals:
    void trbUpdated(int mb, int trb, bool statusChanged);
    void powerGoodChanged(int mb, int trb, int faultBits);   // bit i = PG của TRM(i+1) đang lỗi; 0 = hết lỗi

private:
    model::DeviceStore *m_store;
    AlarmEngine *m_alarms;
    quint64 m_badAddress = 0;
    QHash<int, int> m_pgFault;   // khóa mb << 8 | trb: mặt nạ PG lỗi của bản tin trước
};

} // namespace services
