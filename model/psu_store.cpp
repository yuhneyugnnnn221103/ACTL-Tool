#include "psu_store.h"
#include "../proto/psu_monitor_proto.h"

namespace model {

PsuStore::PsuStore(int firstAddr, int count, QObject *parent)
    : QObject(parent), m_firstAddr(firstAddr), m_psus(count) {}

QList<int> PsuStore::clusterAlarms(int addr, int cluster) const
{
    QList<int> out;
    const int first = proto::psumon::clusterField(cluster, proto::psumon::ClusterField(0));
    for (int f : psu(addr).alarms)
        if (f >= first && f < first + proto::psumon::kClusterFields) out.append(f);
    return out;
}

Status PsuStore::clusterStatus(int addr, int cluster) const
{
    const Status s = psu(addr).status;
    if (s == Status::NoData || s == Status::Lost || s == Status::Updating) return s;
    return clusterAlarms(addr, cluster).isEmpty() ? Status::Ok : Status::Warning;
}

Status PsuStore::clusterDisplayStatus(int addr, int cluster) const
{
    const Status s = psu(addr).status;
    if (s == Status::NoData || s == Status::Updating) return s;
    return clusterAlarms(addr, cluster).isEmpty() ? Status::Ok : Status::Warning;
}

Status PsuStore::displayStatus(int addr) const
{
    const PsuState &p = psu(addr);
    if (p.status != Status::Lost) return p.status;
    if (p.lastLive == Status::Trip) return Status::Trip;
    if (!p.alarms.isEmpty()) return Status::Warning;
    return p.lastLive == Status::Updating ? Status::Updating : Status::Ok;
}

void PsuStore::updatePsu(int addr, const QList<double> &values, const QByteArray &raw, qint64 timestampMs,
                         Status status, const QList<int> &alarms)
{
    if (!contains(addr)) return;
    PsuState &s = m_psus[addr - m_firstAddr];
    s.values = values;
    s.raw = raw;
    s.alarms = alarms;
    s.lastSeenMs = timestampMs;
    ++s.frames;
    setStatus(addr, status);
}

void PsuStore::markStale(qint64 nowMs, int staleMs)
{
    for (int i = 0; i < m_psus.size(); ++i) {
        const PsuState &s = m_psus.at(i);
        const bool live = s.status == Status::Ok || s.status == Status::Warning || s.status == Status::Trip;
        if (live && nowMs - s.lastSeenMs > staleMs) setStatus(m_firstAddr + i, Status::Lost);
    }
}

void PsuStore::setStatus(int addr, Status to)
{
    PsuState &s = m_psus[addr - m_firstAddr];
    const Status from = s.status;
    if (from == to) return;
    s.status = to;
    if (to != Status::Lost && to != Status::NoData) s.lastLive = to;
    emit psuStatusChanged(addr, from, to);
}

} // namespace model
