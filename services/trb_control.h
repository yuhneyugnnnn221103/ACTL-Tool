#pragma once
#include "../core/link.h"
#include "../proto/trb_control_proto.h"
#include <QHash>

namespace services {

// Gửi lệnh điều khiển/beam cho TRB qua link giám sát. Hiện chưa chờ ACK:
// "đã gửi" chỉ có nghĩa là đã ghi ra đường truyền.
class TrbControl : public QObject {
    Q_OBJECT
public:
    explicit TrbControl(core::Link *link, QObject *parent = nullptr);

    void sendControl(int mb, int trb, const proto::trbctl::ControlCmd &cmd);
    void sendBeam(int mb, int trb, const proto::trbctl::BeamCmd &cmd);

signals:
    void commandFinished(const QString &description, bool sent);

private:
    void submit(const QByteArray &frame, const QString &description);

    core::Link *m_link;
    QHash<quint64, QString> m_pending;
};

} // namespace services
