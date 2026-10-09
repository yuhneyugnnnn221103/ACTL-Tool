#include "device_store.h"

namespace model {

QString statusText(Status s)
{
    switch (s) {
    case Status::NoData:   return QStringLiteral("Chưa có dữ liệu");
    case Status::Ok:       return QStringLiteral("Tốt");
    case Status::Warning:  return QStringLiteral("Quá ngưỡng");
    case Status::Trip:     return QStringLiteral("Trip");
    case Status::Lost:     return QStringLiteral("Mất kết nối");
    case Status::Updating: return QStringLiteral("Đang nạp");
    }
    return {};
}

DeviceStore::DeviceStore(int mbCount, int trbPerMb, QObject *parent)
    : QObject(parent), m_mbCount(mbCount), m_trbPerMb(trbPerMb), m_trbs(mbCount * trbPerMb)
{
    m_counts[int(Status::NoData)] = m_trbs.size();
}

QList<Status> DeviceStore::conditions(int mb, int trb) const
{
    const TrbState &t = m_trbs.at(mb * m_trbPerMb + trb);
    QList<Status> out;
    if (t.status == Status::Trip || (t.status == Status::Lost && t.lastLive == Status::Trip)) out << Status::Trip;
    if (!t.alarms.isEmpty() && (t.status == Status::Warning || t.status == Status::Trip || t.status == Status::Lost)) out << Status::Warning;
    if (t.status == Status::Lost) out << Status::Lost;
    if (out.isEmpty()) out << t.status;      // Ok, NoData, Updating
    return out;
}

int DeviceStore::countCondition(Status s) const
{
    if (s == Status::Ok || s == Status::Lost || s == Status::NoData || s == Status::Updating) return count(s);
    int n = 0;
    for (int i = 0; i < m_trbs.size(); ++i) n += conditions(i / m_trbPerMb, i % m_trbPerMb).contains(s);
    return n;
}

void DeviceStore::updateTrb(int mb, int trb, const QList<double> &values, const QByteArray &raw,
                            qint64 timestampMs, Status status, const QList<int> &alarms)
{
    if (!contains(mb, trb)) return;
    TrbState &s = m_trbs[mb * m_trbPerMb + trb];
    s.values = values;
    s.raw = raw;
    s.alarms = alarms;
    s.lastSeenMs = timestampMs;
    ++s.frames;
    setStatus(mb, trb, status);
}

void DeviceStore::markStale(qint64 nowMs, int staleMs)
{
    for (int i = 0; i < m_trbs.size(); ++i) {
        const TrbState &s = m_trbs.at(i);
        const bool live = s.status == Status::Ok || s.status == Status::Warning || s.status == Status::Trip;
        if (live && nowMs - s.lastSeenMs > staleMs)
            setStatus(i / m_trbPerMb, i % m_trbPerMb, Status::Lost);
    }
}

void DeviceStore::setStatus(int mb, int trb, Status to)
{
    TrbState &s = m_trbs[mb * m_trbPerMb + trb];
    const Status from = s.status;
    if (from == to) return;
    --m_counts[int(from)];
    ++m_counts[int(to)];
    s.status = to;
    if (to != Status::Lost && to != Status::NoData) s.lastLive = to;
    emit trbStatusChanged(mb, trb, from, to);
}

} // namespace model
