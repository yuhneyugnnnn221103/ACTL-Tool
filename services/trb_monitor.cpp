#include "trb_monitor.h"
#include "../proto/trb_monitor_proto.h"

namespace services {

using namespace proto;

bool TrbMonitor::registerFrames(core::FrameRegistry &registry, bool checkCrc)
{
    return registry.add(trbmon::spec(checkCrc));
}

TrbMonitor::TrbMonitor(core::Link *link, model::DeviceStore *store, AlarmEngine *alarms, QObject *parent)
    : QObject(parent), m_store(store), m_alarms(alarms)
{
    connect(link, &core::Link::frameReceived, this, &TrbMonitor::onFrame);
}

void TrbMonitor::onFrame(const core::Frame &frame)
{
    if (frame.cmd != trbmon::cmd()) return;
    const int mb = frame.at(trbmon::kOffMb), trb = frame.at(trbmon::kOffTrb);
    if (!m_store->contains(mb, trb)) { ++m_badAddress; return; }

    const QList<double> v = trbmon::table().decode(frame.raw);
    // Tạm quy ước: có trip code khác 0 là Trip; Trip được ưu tiên hơn Quá ngưỡng.
    bool trip = false;
    for (int i = 0; i < trbmon::kNumTrip; ++i)
        trip = trip || v.at(trbmon::kIdxTrip0 + i) != 0;

    const QList<int> bad = m_alarms ? m_alarms->check(mb, trb, v) : QList<int>();
    const model::Status before = m_store->trb(mb, trb).status;
    const model::Status now = trip ? model::Status::Trip : bad.isEmpty() ? model::Status::Ok : model::Status::Warning;
    m_store->updateTrb(mb, trb, v, frame.raw, frame.timestampMs, now, bad);
    emit trbUpdated(mb, trb, before != now);
}

} // namespace services
