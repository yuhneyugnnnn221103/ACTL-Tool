#pragma once
#include "../core/link.h"
#include "../proto/trb_calib_proto.h"

namespace services {

// Gửi bản tin Calib (CMD 18 18) cho TRB qua đường Ethernet tới Gateway. Không có ACK:
// "đã gửi" nghĩa là đã ghi ra đường truyền.
class TrbCalib : public QObject {
    Q_OBJECT
public:
    explicit TrbCalib(core::Link *link, QObject *parent = nullptr);

    void send(int mb, int trb, const proto::calib::Data &data);

signals:
    void commandFinished(const QString &description, bool sent);

private:
    core::Link *m_link;
    QHash<quint64, QString> m_pending;
};

} // namespace services
