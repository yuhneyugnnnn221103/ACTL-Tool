#include "trb_calib.h"

namespace services {

TrbCalib::TrbCalib(core::Link *link, QObject *parent) : QObject(parent), m_link(link)
{
    connect(link, &core::Link::requestFinished, this, [this](quint64 id, bool ok, const core::Frame &) {
        const auto it = m_pending.constFind(id);
        if (it == m_pending.cend()) return;
        emit commandFinished(it.value(), ok);
        m_pending.erase(it);
    });
}

void TrbCalib::send(int mb, int trb, const proto::calib::Data &d)
{
    const bool all = mb == proto::calib::kBroadcast || trb == proto::calib::kBroadcast;
    const QString target = all ? QStringLiteral("tất cả TRB") : QStringLiteral("MB%1 / TRB%2").arg(mb + 1).arg(trb + 1);
    auto count = [](quint32 m) { int n = 0; for (int c = 0; c < 32; ++c) n += m >> c & 1; return n; };
    const QString text = QStringLiteral("Calib %1 [RX bật %2 kênh, TX bật %3 kênh]").arg(target).arg(count(d.rxEn)).arg(count(d.txEn));

    core::Request r;
    r.data = proto::calib::buildFrame(mb, trb, d);
    r.priority = 10;
    m_pending.insert(m_link->send(r), text);
}

} // namespace services
