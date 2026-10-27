#include "trb_monitor.h"
#include "../proto/trb_monitor_proto.h"

namespace services {

using namespace proto;

bool TrbMonitor::registerFrames(core::FrameRegistry &registry, bool checkCrc, bool acceptLegacyCmd)
{
    bool ok = registry.add(trbmon::spec(checkCrc));
    if (acceptLegacyCmd) ok &= registry.add(trbmon::legacySpec(checkCrc));
    return ok;
}

TrbMonitor::TrbMonitor(core::Link *link, model::DeviceStore *store, AlarmEngine *alarms, QObject *parent)
    : QObject(parent), m_store(store), m_alarms(alarms)
{
    connect(link, &core::Link::frameReceived, this, &TrbMonitor::onFrame);
}

void TrbMonitor::onFrame(const core::Frame &frame)
{
    if (frame.cmd != trbmon::cmd() && frame.cmd != trbmon::legacyCmd()) return;
    const int mb = frame.at(trbmon::kOffMb), trb = frame.at(trbmon::kOffTrb);
    if (!m_store->contains(mb, trb)) { ++m_badAddress; return; }

    int &empty = m_emptyRun[mb << 8 | trb];
    if (frame.empty) {                    // Gateway báo TRB này không phản hồi: đánh dấu X ngay; đủ m_emptyLimit lần liên tiếp thì coi là mất kết nối
        m_store->setNoReply(mb, trb, true);
        if (++empty >= m_emptyLimit) m_store->markSilent(mb, trb);
        return;
    }
    empty = 0;

    const QList<double> v = trbmon::table().decode(frame.raw);
    // Tạm quy ước: có trip code khác 0 hoặc mất PG (Power Good) là Trip; Trip được ưu tiên hơn Quá ngưỡng.
    bool trip = false;
    for (int i = 0; i < trbmon::kNumTrip; ++i)
        trip = trip || v.at(trbmon::kIdxTrip0 + i) != 0;
    const int pgFault = trbmon::pgFaultBits(v.at(trbmon::kIdxPg));
    trip = trip || pgFault != 0;
    int &lastPg = m_pgFault[mb << 8 | trb];
    if (pgFault != lastPg) { lastPg = pgFault; emit powerGoodChanged(mb, trb, pgFault); }

    const QList<int> bad = m_alarms ? m_alarms->check(mb, trb, v) : QList<int>();
    const model::Status before = m_store->trb(mb, trb).status;
    const model::Status now = trip ? model::Status::Trip : bad.isEmpty() ? model::Status::Ok : model::Status::Warning;
    m_store->updateTrb(mb, trb, v, frame.raw, frame.timestampMs, now, bad);
    emit trbUpdated(mb, trb, before != now);
}

} // namespace services
