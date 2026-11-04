#include "direct_query_proto.h"
#include "../core/frame.h"

namespace proto::directq {

QByteArray trbCmd() { return QByteArray::fromHex("2323"); }
QByteArray psuCmd() { return QByteArray::fromHex("0202"); }

static QByteArray build(const QByteArray &cmd, int length, int addrA, int addrB)
{
    QByteArray f(length, 0);
    f[0] = char(core::kHeader1);
    f[1] = char(core::kHeader2);
    f.replace(2, 2, cmd);
    f[4] = char(addrA);
    if (addrB >= 0) f[5] = char(addrB);
    f[length - 2] = char(0xE1);
    f[length - 1] = char(0xE2);
    core::seal(f, 4);
    return f;
}

QByteArray buildTrb(int mb, int trb) { return build(trbCmd(), kTrbLength, mb, trb); }
QByteArray buildPsu(int addr) { return build(psuCmd(), kPsuLength, addr, -1); }

} // namespace proto::directq
