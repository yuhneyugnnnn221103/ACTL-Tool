#include "psu_control_proto.h"
#include "../core/frame.h"

namespace proto::psuctl {

QByteArray cmd() { return QByteArray::fromHex("0101"); }

QByteArray buildControl(int addr, const ControlCmd &c)
{
    QByteArray f(kLength, 0);
    f.replace(2, 2, cmd());
    f[kOffAddr] = char(addr);
    f[kOffMask] = char(c.clusterMask & 0x0F);
    f[kOffClearTrip] = char(c.clearTrip ? 0x01 : 0x00);
    core::seal(f, 2);
    return f;
}

} // namespace proto::psuctl
