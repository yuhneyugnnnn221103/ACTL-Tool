#include "psu_store.h"

namespace model {

PsuStore::PsuStore(int firstAddr, int count, QObject *parent)
    : QObject(parent), m_firstAddr(firstAddr), m_psus(count) {}

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
    emit psuStatusChanged(addr, from, to);
}

} // namespace model
