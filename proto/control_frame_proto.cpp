#include "control_frame_proto.h"
#include "../core/frame.h"

namespace proto::ctlframe {

int sectionOffset(Section s)
{
    int off = kOffSection0;
    for (int i = 0; i < s; ++i) off += kSectionLength[i];
    return off;
}

QByteArray wrap(Section s, const QByteArray &inner)
{
    if (s < 0 || s >= kSections || inner.size() != kSectionLength[s]) return {};
    QByteArray f(kLength, char(kPad));
    f[0] = char(core::kHeader1);
    f[1] = char(core::kHeader2);
    f[2] = char(kCmd);
    f[kOffMask] = char(1 << s);
    f.replace(sectionOffset(s), inner.size(), inner);
    return f;
}

QByteArray section(const QByteArray &frame, Section s)
{
    if (frame.size() != kLength || s < 0 || s >= kSections) return {};
    if (!(quint8(frame.at(kOffMask)) >> s & 1)) return {};
    return frame.mid(sectionOffset(s), kSectionLength[s]);
}

} // namespace proto::ctlframe
