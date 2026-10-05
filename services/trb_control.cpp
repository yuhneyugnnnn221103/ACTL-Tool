#include "trb_control.h"

namespace services {

using namespace proto::trbctl;

static QString target(int mb, int trb)
{
    return mb == kBroadcast && trb == kBroadcast ? QStringLiteral("tất cả TRB")
                                                 : QStringLiteral("MB%1 / TRB%2").arg(mb).arg(trb);
}

TrbControl::TrbControl(core::Link *link, QObject *parent) : QObject(parent), m_link(link)
{
    connect(link, &core::Link::requestFinished, this, [this](quint64 id, bool ok, const core::Frame &) {
        const auto it = m_pending.constFind(id);
        if (it == m_pending.cend()) return;
        emit commandFinished(it.value(), ok);
        m_pending.erase(it);
    });
}

void TrbControl::sendControl(int mb, int trb, const ControlCmd &c)
{
    QStringList parts{QStringLiteral("PA=%1").arg(c.paMask & 0x0F, 4, 2, QLatin1Char('0')),
                      c.start ? QStringLiteral("Start") : QStringLiteral("Stop"),
                      c.debugMode ? QStringLiteral("Debug") : QStringLiteral("Normal")};
    if (c.clearTrip) parts << QStringLiteral("Clear trip");
    if (c.beamSync) parts << QStringLiteral("Beamsync");
    submit(buildControl(mb, trb, c), QStringLiteral("Điều khiển %1 [%2]").arg(target(mb, trb), parts.join(", ")));
}

void TrbControl::sendBeam(int mb, int trb, const BeamCmd &b)
{
    submit(buildBeam(mb, trb, b),
           QStringLiteral("Beam %1 [PhaseTX=%2, PhaseRX=%3, AmpTX=%4, AmpRX=%5, CH=%6, ADAR=%7]")
               .arg(target(mb, trb)).arg(b.phaseTx).arg(b.phaseRx).arg(b.ampTx).arg(b.ampRx)
               .arg(b.chMask & 0x0F, 4, 2, QLatin1Char('0')).arg(b.adarMask, 8, 2, QLatin1Char('0')));
}

void TrbControl::submit(const QByteArray &frame, const QString &description)
{
    core::Request r;
    r.data = frame;
    r.priority = 10; // lệnh của người vận hành đi trước mọi thứ khác trong hàng đợi
    m_pending.insert(m_link->send(r), description);
}

} // namespace services
