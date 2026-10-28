#include "sim_world.h"
#include "../proto/control_frame_proto.h"
#include "../proto/trb_calib_proto.h"
#include "../proto/trb_config_proto.h"
#include "../proto/trb_control_proto.h"
#include "../proto/trb_monitor_proto.h"

namespace sim {

using namespace proto;

bool registerRequestFrames(core::FrameRegistry &registry, bool checkCrc)
{
    bool ok = registry.add(trbmon::pollSpec(checkCrc));
    ok &= registry.add({QByteArray::fromHex("A2A2"), 14, 4, checkCrc, QStringLiteral("TRB điều khiển")});
    ok &= registry.add({QByteArray::fromHex("1414"), 17, 4, checkCrc, QStringLiteral("TRB beam")});
    // Khung cấu hình có thêm byte thiết bị (0x01 = TRB) sau AB CD: khóa tra cứu là {DEV, byte CMD đầu}, CRC vẫn tính từ byte địa chỉ (lùi 1 vì có DEV).
    ok &= registry.add({QByteArray::fromHex("01A1"), trbcfg::kLength + 1, 5, checkCrc, QStringLiteral("TRB ghi cấu hình")});
    ok &= registry.add({QByteArray::fromHex("01A3"), 10 + 1, 5, checkCrc, QStringLiteral("TRB hỏi cấu hình")});
    ok &= registry.add({QByteArray::fromHex("0118"), proto::calib::kLength + 1, 5, checkCrc, QStringLiteral("TRB Calib")});   // 01 = byte thiết bị TRB, 18 = CMD
    core::FrameSpec control{QByteArray(1, char(ctlframe::kCmd)), ctlframe::kLength, 0, false, QStringLiteral("Điều khiển tổng")};
    control.tailer = false;                          // khung điều khiển tổng không có CRC tổng, đuôi 3 byte E1 E2 E3 nên không dùng kiểm tra đuôi 2 byte của parser
    ok &= registry.add(control);
    return ok;
}

SimWorld::SimWorld(int mbCount, int trbPerMb, quint32 seed, QObject *parent)
    : QObject(parent), m_mbCount(mbCount), m_trbPerMb(trbPerMb)
{
    for (int mb = 0; mb < mbCount; ++mb)
        for (int t = 0; t < trbPerMb; ++t) m_trbs.push_back(std::make_unique<TrbModel>(mb, t, seed));
    connect(&m_debugTimer, &QTimer::timeout, this, [this] {
        for (auto &m : m_trbs)
            if (m->debug() && m->online()) emit unsolicited(m->monitorFrame());
    });
}

TrbModel *SimWorld::trb(int mb, int t)
{
    if (mb < 0 || mb >= m_mbCount || t < 0 || t >= m_trbPerMb) return nullptr;
    return m_trbs[size_t(mb * m_trbPerMb + t)].get();
}

int SimWorld::debugCount() const
{
    int n = 0;
    for (const auto &m : m_trbs) n += m->debug();
    return n;
}

void SimWorld::setDebugPeriodMs(int ms)
{
    if (ms > 0) m_debugTimer.start(ms); else m_debugTimer.stop();
}

QList<TrbModel *> SimWorld::targets(int mb, int t)
{
    QList<TrbModel *> out;
    for (auto &m : m_trbs)
        if ((mb == 0xFF || m->mb() == mb) && (t == 0xFF || m->trb() == t)) out.append(m.get());
    return out;
}

void SimWorld::noteDebug()
{
    const bool conflict = debugCount() > 1;
    if (conflict && !m_conflict) ++m_stats.debugConflicts;
    m_conflict = conflict;
}

QByteArray SimWorld::handle(const core::Frame &frame)
{
    if (quint8(frame.at(2)) == ctlframe::kCmd) {      // khung điều khiển tổng: xử lý từng khung con có bit mask bật (beam điều khiển và PSU chưa mô phỏng)
        QByteArray reply;
        for (ctlframe::Section sec : {ctlframe::SecTrbControl, ctlframe::SecTrbBeam}) {
            const QByteArray inner = ctlframe::section(frame.raw, sec);
            if (inner.isEmpty()) continue;
            reply += handle(core::Frame{inner.mid(2, 2), inner, frame.timestampMs});
        }
        return reply;
    }
    const int mb = frame.at(trbmon::kOffMb), t = frame.at(trbmon::kOffTrb);
    const quint8 cmd = frame.at(2);
    const QList<TrbModel *> list = targets(mb, t);
    if (list.isEmpty()) { ++m_stats.ignored; return {}; }

    switch (cmd) {
    case 0x23:                                       // hỏi giám sát: chỉ một thiết bị trả lời
        ++m_stats.polls;
        if (list.size() != 1 || !list.first()->online()) return {};
        return list.first()->monitorFrame();
    case 0xA2: {                                     // điều khiển: không có ACK
        ++m_stats.controls;
        trbctl::ControlCmd c;
        c.paMask = frame.at(6) & 0x0F;
        c.clearTrip = frame.at(7) & 0x01;
        c.start = frame.at(7) & 0x02;
        c.beamSync = frame.at(7) & 0x04;
        c.debugMode = frame.at(7) & 0x10;
        for (TrbModel *m : list) if (m->online()) m->applyControl(c);
        noteDebug();
        return {};
    }
    case 0x14: {                                     // beam: không có ACK
        ++m_stats.beams;
        trbctl::BeamCmd b;
        b.phaseTx = frame.at(6); b.phaseRx = frame.at(7); b.ampTx = frame.at(8); b.ampRx = frame.at(9);
        b.chMask = frame.at(10) & 0x0F; b.adarMask = frame.at(11);
        for (TrbModel *m : list) if (m->online()) m->applyBeam(b);
        return {};
    }
    case 0x18:                                       // Calib: không có ACK (chỉ đếm, mô hình chưa lưu giá trị)
        ++m_stats.calibs;
        return {};
    case 0xA1:                                       // ghi cấu hình: không có ACK
        ++m_stats.configWrites;
        for (TrbModel *m : list) if (m->online()) m->writeConfig(frame);
        return {};
    case 0xA3:                                       // hỏi cấu hình
        ++m_stats.configReads;
        if (list.size() != 1 || !list.first()->online()) return {};
        return list.first()->configReply();
    default:
        ++m_stats.ignored;
        return {};
    }
}

} // namespace sim
