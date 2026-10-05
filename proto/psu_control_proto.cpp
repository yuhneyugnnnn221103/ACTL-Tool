#include "psu_control_proto.h"
#include "../core/frame.h"

namespace proto::psuctl {

QByteArray buildControl(int addr, const ControlCmd &c)
{
    QByteArray f(kLength, 0);
    f[2] = char(0x01);
    f[3] = char(addr);
    f[4] = char(c.clusterMask & 0x0F);
    f[5] = char(c.clearTrip ? 0x01 : 0x00);
    core::seal(f, 2);
    return f;
}

} // namespace proto::psuctl
