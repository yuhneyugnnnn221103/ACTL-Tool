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

} // namespace services
