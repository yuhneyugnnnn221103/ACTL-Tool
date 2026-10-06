#pragma once
#include "../core/link.h"
#include "../proto/trb_control_proto.h"
#include <QHash>
#include <QPair>
#include <functional>
#include <optional>

namespace services {

// Gửi lệnh điều khiển/beam cho TRB qua link giám sát. Hiện chưa chờ ACK:
// "đã gửi" chỉ có nghĩa là đã ghi ra đường truyền.
//
// Chế độ Debug: TRB ở Debug tự phát bản tin giám sát không cần Gateway hỏi, nên hai TRB cùng Debug sẽ
// tranh bus. Vì vậy tối đa một TRB ở Debug: lệnh Debug cho TRB thứ hai bị từ chối (hoặc chuyển Debug
// sang TRB mới bằng sendControlSwitchDebug), lệnh Debug gửi broadcast luôn bị từ chối. App chỉ biết
// các lệnh nó đã gửi trong phiên này; TRB đã ở Debug từ trước (hoặc do tool khác đặt) thì không biết được.
class TrbControl : public QObject {
    Q_OBJECT
public:
    explicit TrbControl(core::Link *link, QObject *parent = nullptr);

    enum class Check { Ok, BroadcastDebug, OtherInDebug };
    using Device = QPair<int, int>; // (mb, trb)

    // TRB đang ở Debug theo các lệnh đã gửi; nullopt = không có.
    std::optional<Device> debugTrb() const { return m_debug; }
    // Lệnh có vi phạm quy tắc "một TRB Debug" không. Không gửi gì.
    Check check(int mb, int trb, const proto::trbctl::ControlCmd &cmd) const;

    // Từ chối (trả về lý do, không gửi) nếu check() != Ok.
    Check sendControl(int mb, int trb, const proto::trbctl::ControlCmd &cmd);
    // Như sendControl nhưng nếu TRB khác đang Debug thì trước tiên đưa TRB đó về Normal (gửi lại lệnh cuối
    // của nó với debug = 0, giữ nguyên PA và Start) rồi mới gửi lệnh Debug. BroadcastDebug vẫn bị từ chối.
    Check sendControlSwitchDebug(int mb, int trb, const proto::trbctl::ControlCmd &cmd);
    void sendBeam(int mb, int trb, const proto::trbctl::BeamCmd &cmd);

signals:
    void commandFinished(const QString &description, bool sent);
    void debugTrbChanged();

private:
    struct Pending { QString text; std::function<void()> onSent; };
    void submit(const QByteArray &frame, const QString &description, std::function<void()> onSent = {});
    void sendControlUnchecked(int mb, int trb, const proto::trbctl::ControlCmd &cmd);
    void applyDebugState(int mb, int trb, const proto::trbctl::ControlCmd &cmd);

    core::Link *m_link;
    QHash<quint64, Pending> m_pending;
    QHash<int, proto::trbctl::ControlCmd> m_lastCmd;   // lệnh điều khiển cuối cùng gửi cho từng TRB (mb << 8 | trb)
    std::optional<Device> m_debug;
};

} // namespace services
