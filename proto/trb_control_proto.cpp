#include "trb_control_proto.h"
#include "../core/frame.h"

namespace proto::trbctl {

static QByteArray base(int len, quint8 cmd, int mb, int trb)
{
    QByteArray f(len, 0);
    f[2] = f[3] = char(cmd);
    f[4] = char(mb);
    f[5] = char(trb);
    return f;
}

QByteArray buildControl(int mb, int trb, const ControlCmd &c)
{
    QByteArray f = base(14, 0xA2, mb, trb);
    f[6] = char(c.paMask & 0x0F);
    f[7] = char((c.clearTrip ? 0x01 : 0) | (c.start ? 0x02 : 0) | (c.beamSync ? 0x04 : 0)
                | (c.debugMode ? 0x10 : 0));
    core::seal(f, 4);
    return f;
}

QByteArray buildBeam(int mb, int trb, const BeamCmd &b)
{
    QByteArray f = base(17, 0x14, mb, trb);
    f[6] = char(b.phaseTx);
    f[7] = char(b.phaseRx);
    f[8] = char(b.ampTx);
    f[9] = char(b.ampRx);
    f[10] = char(b.chMask & 0x0F);
    f[11] = char(b.adarMask);
    core::seal(f, 4);
    return f;
}

} // namespace proto::trbctl
