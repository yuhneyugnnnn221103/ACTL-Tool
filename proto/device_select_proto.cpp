#include "device_select_proto.h"

namespace proto::devsel {

QByteArray add(Device dev, const QByteArray &frame)
{
    if (frame.size() < kOffDevice) return {};
    QByteArray f = frame;
    f.insert(kOffDevice, char(dev));
    return f;
}

QByteArray strip(const QByteArray &frame)
{
    if (frame.size() <= kOffDevice) return {};
    QByteArray f = frame;
    f.remove(kOffDevice, 1);
    return f;
}

} // namespace proto::devsel
