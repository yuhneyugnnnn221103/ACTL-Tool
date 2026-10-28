#include "psu_monitor.h"
#include "../proto/psu_monitor_proto.h"

namespace services {

using namespace proto;

bool PsuMonitor::registerFrames(core::FrameRegistry &registry, bool checkCrc)
{
    return registry.add(psumon::spec(checkCrc));
}

PsuMonitor::PsuMonitor(core::Link *link, model::PsuStore *store, AlarmEngine *alarms, QObject *parent)
    : QObject(parent), m_store(store), m_alarms(alarms)
{
    connect(link, &core::Link::frameReceived, this, &PsuMonitor::onFrame);
}

void PsuMonitor::onFrame(const core::Frame &frame)
{
    if (frame.cmd != psumon::cmd()) return;
    const int addr = frame.at(psumon::kOffAddr);
    if (!m_store->contains(addr)) { ++m_badAddress; return; }

    int &empty = m_emptyRun[addr];
    if (frame.empty) {                    // Gateway báo PSU này không phản hồi: đánh dấu X ngay; đủ m_emptyLimit lần liên tiếp thì coi là mất kết nối
        m_store->setNoReply(addr, true);
        if (++empty >= m_emptyLimit) m_store->markSilent(addr);
        return;
    }
    empty = 0;

    const QList<double> v = psumon::table().decode(frame.raw);
    // Như TRB: có trip code khác 0 là Trip, ưu tiên hơn Quá ngưỡng. Thêm: một cụm DCM ở TRIPPED/LOCKOUT (safe_state của cụm) cũng là Trip.
    // Bảng trip code chưa gán được theo cụm nên trip theo cụm tạm lấy từ safe_state.
    bool trip = false;
    for (int i = 0; i < psumon::kNumTrip; ++i) trip = trip || v.at(psumon::kIdxTrip0 + i) != 0;
    for (int c = 0; c < psumon::kNumCluster; ++c) trip = trip || psumon::clusterTripped(v, c);

    // Cụm đang tắt (chân EN = 0, không trip) không so ngưỡng: số đo của cụm tắt không có ý nghĩa và không được kéo trạng thái PSU lên Quá ngưỡng.
    const QList<int> bad = m_alarms ? m_alarms->check(addr, 0, v, psumon::offClusterFields(v)) : QList<int>();
    const model::Status before = m_store->psu(addr).status;
    const model::Status now = trip ? model::Status::Trip : bad.isEmpty() ? model::Status::Ok : model::Status::Warning;
    m_store->updatePsu(addr, v, frame.raw, frame.timestampMs, now, bad);
    emit psuUpdated(addr, before != now);
}

} // namespace services
