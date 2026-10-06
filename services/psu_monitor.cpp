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

    const QList<double> v = psumon::table().decode(frame.raw);
    // Như TRB: có trip code khác 0 là Trip, ưu tiên hơn Quá ngưỡng.
    bool trip = false;
    for (int i = 0; i < psumon::kNumTrip; ++i) trip = trip || v.at(psumon::kIdxTrip0 + i) != 0;

    const QList<int> bad = m_alarms ? m_alarms->check(addr, 0, v) : QList<int>();
    const model::Status before = m_store->psu(addr).status;
    const model::Status now = trip ? model::Status::Trip : bad.isEmpty() ? model::Status::Ok : model::Status::Warning;
    m_store->updatePsu(addr, v, frame.raw, frame.timestampMs, now, bad);
    emit psuUpdated(addr, before != now);
}

} // namespace services
