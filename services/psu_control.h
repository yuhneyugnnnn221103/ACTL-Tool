#pragma once
#include "../core/link.h"
#include "../proto/psu_control_proto.h"
#include <QHash>

namespace services {

// Gửi lệnh điều khiển PSU qua link giám sát. Không có ACK: "đã gửi" chỉ có nghĩa là đã ghi ra đường truyền.
class PsuControl : public QObject {
    Q_OBJECT
public:
    explicit PsuControl(core::Link *link, QObject *parent = nullptr);

    void sendControl(int addr, const proto::psuctl::ControlCmd &cmd);

signals:
    void commandFinished(const QString &description, bool sent);

private:
    core::Link *m_link;
    QHash<quint64, QString> m_pending;
};

} // namespace services
