#include "psu_control.h"

namespace services {

PsuControl::PsuControl(core::Link *link, QObject *parent) : QObject(parent), m_link(link)
{
    connect(link, &core::Link::requestFinished, this, [this](quint64 id, bool ok, const core::Frame &) {
        const auto it = m_pending.constFind(id);
        if (it == m_pending.cend()) return;
        emit commandFinished(it.value(), ok);
        m_pending.erase(it);
    });
}

void PsuControl::sendControl(int addr, const proto::psuctl::ControlCmd &c)
{
    QStringList on, off;
    for (int i = 0; i < 4; ++i) (c.clusterMask >> i & 1 ? on : off) << QString::number(i + 1);
    QString text = QStringLiteral("Điều khiển PSU %1 [bật cụm: %2; tắt cụm: %3]").arg(addr)
                       .arg(on.isEmpty() ? QStringLiteral("-") : on.join(','), off.isEmpty() ? QStringLiteral("-") : off.join(','));
    if (c.clearTrip) text += QStringLiteral(" [Clear trip]");

    core::Request r;
    r.data = proto::psuctl::buildControl(addr, c);
    r.priority = 10; // lệnh của người vận hành đi trước mọi thứ khác trong hàng đợi
    m_pending.insert(m_link->send(r), text);
}

void PsuControl::sendClearTripThenOn(int addr, int gapMs)
{
    core::Request clear;
    proto::psuctl::ControlCmd c;
    c.clusterMask = proto::psuctl::kMaskOff;
    c.clearTrip = true;
    clear.data = proto::psuctl::buildControl(addr, c);
    clear.priority = 10;
    clear.gapMs = gapMs;   // chờ PSU xử lý xong Clear trip rồi mới tới lệnh bật
    m_pending.insert(m_link->send(clear), QStringLiteral("Điều khiển PSU %1 [Clear trip, tắt]").arg(addr));

    core::Request on;
    c.clusterMask = proto::psuctl::kMaskOn;
    c.clearTrip = false;
    on.data = proto::psuctl::buildControl(addr, c);
    on.priority = 10;
    m_pending.insert(m_link->send(on), QStringLiteral("Điều khiển PSU %1 [bật nguồn, sau Clear trip]").arg(addr));
}

} // namespace services
