#include "control_frame_proto.h"
#include "../core/frame.h"
#include <atomic>

namespace proto::ctlframe {

static std::atomic<bool> g_standalone{false};
void setStandalone(bool on) { g_standalone = on; }
bool standalone() { return g_standalone; }


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
    for (int i = 0; i < kTailLength; ++i) f[kLength - kTailLength + i] = char(kTail[i]);
    f.replace(sectionOffset(s), inner.size(), inner);
    return f;
}

QByteArray pack(Section s, const QByteArray &inner)
{
    if (!g_standalone) return wrap(s, inner);
    return s >= 0 && s < kSections && inner.size() == kSectionLength[s] ? inner : QByteArray();
}

QByteArray section(const QByteArray &frame, Section s)
{
    if (frame.size() != kLength || s < 0 || s >= kSections) return {};
    if (!(quint8(frame.at(kOffMask)) >> s & 1)) return {};
    return frame.mid(sectionOffset(s), kSectionLength[s]);
}

} // namespace proto::ctlframe
