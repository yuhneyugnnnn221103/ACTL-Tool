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
        const Pending p = it.value();
        m_pending.erase(it);
        if (ok && p.onSent) p.onSent();   // chỉ ghi nhận trạng thái Debug khi lệnh thật sự đã ra đường truyền
        emit commandFinished(p.text, ok);
    });
}

static int key(int mb, int trb) { return mb << 8 | trb; }

TrbControl::Check TrbControl::check(int mb, int trb, const ControlCmd &c) const
{
    if (!c.debugMode) return Check::Ok;
    if (mb == kBroadcast || trb == kBroadcast) return Check::BroadcastDebug;
    if (m_debug && *m_debug != Device(mb, trb)) return Check::OtherInDebug;
    return Check::Ok;
}

TrbControl::Check TrbControl::sendControl(int mb, int trb, const ControlCmd &c)
{
    const Check r = check(mb, trb, c);
    if (r == Check::Ok) sendControlUnchecked(mb, trb, c);
    return r;
}

TrbControl::Check TrbControl::sendControlSwitchDebug(int mb, int trb, const ControlCmd &c)
{
    const Check r = check(mb, trb, c);
    if (r == Check::OtherInDebug && m_debug) {
        // Giữ nguyên PA/Start của TRB đang Debug, chỉ bỏ bit debug, rồi mới sang TRB mới.
        ControlCmd old = m_lastCmd.value(key(m_debug->first, m_debug->second));
        old.debugMode = false;
        old.clearTrip = false;
        old.beamSync = false;
        sendControlUnchecked(m_debug->first, m_debug->second, old);
    } else if (r != Check::Ok) {
        return r;
    }
    sendControlUnchecked(mb, trb, c);
    return Check::Ok;
}

void TrbControl::sendControlUnchecked(int mb, int trb, const ControlCmd &c)
{
    QStringList parts{QStringLiteral("PA=%1").arg(c.paMask & 0x0F, 4, 2, QLatin1Char('0')),
                      c.start ? QStringLiteral("Start") : QStringLiteral("Stop"),
                      c.debugMode ? QStringLiteral("Debug") : QStringLiteral("Normal")};
    if (c.clearTrip) parts << QStringLiteral("Clear trip");
    if (c.beamSync) parts << QStringLiteral("Beamsync");
    submit(buildControl(mb, trb, c), QStringLiteral("Điều khiển %1 [%2]").arg(target(mb, trb), parts.join(", ")),
           [this, mb, trb, c] { applyDebugState(mb, trb, c); });
}

void TrbControl::applyDebugState(int mb, int trb, const ControlCmd &c)
{
    const std::optional<Device> before = m_debug;
    if (mb == kBroadcast || trb == kBroadcast) {
        // Broadcast ghi đè chế độ của mọi TRB; chỉ Normal mới hợp lệ (Debug broadcast bị từ chối trước đó).
        m_lastCmd.clear();
        if (!c.debugMode) m_debug.reset();
    } else {
        m_lastCmd.insert(key(mb, trb), c);
        if (c.debugMode) m_debug = Device(mb, trb);
        else if (m_debug && *m_debug == Device(mb, trb)) m_debug.reset();
    }
    if (before != m_debug) emit debugTrbChanged();
}

void TrbControl::sendBeam(int mb, int trb, const BeamCmd &b)
{
    submit(buildBeam(mb, trb, b),
           QStringLiteral("Beam %1 [PhaseTX=%2, PhaseRX=%3, AmpTX=%4, AmpRX=%5, CH=%6, ADAR=%7]")
               .arg(target(mb, trb)).arg(b.phaseTx).arg(b.phaseRx).arg(b.ampTx).arg(b.ampRx)
               .arg(b.chMask & 0x0F, 4, 2, QLatin1Char('0')).arg(b.adarMask, 8, 2, QLatin1Char('0')));
}

void TrbControl::submit(const QByteArray &frame, const QString &description, std::function<void()> onSent)
{
    core::Request r;
    r.data = frame;
    r.priority = 10; // lệnh của người vận hành đi trước mọi thứ khác trong hàng đợi
    m_pending.insert(m_link->send(r), Pending{description, std::move(onSent)});
}

} // namespace services
