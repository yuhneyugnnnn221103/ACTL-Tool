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
    emit trbStatusChanged(mb, trb, from, to);
}

} // namespace model
