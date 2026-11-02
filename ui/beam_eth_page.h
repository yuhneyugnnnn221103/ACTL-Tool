#pragma once
#include "../app/context.h"
#include "../proto/beam_eth_proto.h"
#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QTimer;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace ui {

// Giả lập beam RDPU (điều khiển beam), gửi qua đường Ethernet giám sát/điều khiển chung với TRB và PSU: nhập Esp, Beta, Freq, Mode riêng cho từng mode 01, 10, 11 (3 cột) rồi gửi một bản tin 42 byte (3 khung 14 byte) (xem proto/beam_eth_proto.h).
class BeamEthPage : public QWidget {
    Q_OBJECT
public:
    explicit BeamEthPage(const AppContext &ctx, QWidget *parent = nullptr);

signals:
    void logEvent(const QString &text);   // dòng sự kiện đã gắn mã [CTRL] / [NET]

private:
    proto::beameth::Params params() const;
    void showState(core::Transport::State s);
    void updatePreview();
    bool send(bool quiet = false);   // quiet: lần gửi theo chu kỳ, không ghi từng lần vào nhật ký
    void onSendClicked();
    void applyPending();
    void updatePending();   // so giá trị đang nhập với giá trị đang gửi: bật nút Áp dụng khi khác nhau
    void stopPeriodic(const QString &why);

    AppContext m_ctx;
    core::Transport::State m_state = core::Transport::State::Closed;
    QSpinBox *m_freq[proto::beameth::kFrameCount], *m_modeValue[proto::beameth::kFrameCount];   // mỗi cột một mode (01, 10, 11)
    QDoubleSpinBox *m_esp[proto::beameth::kFrameCount], *m_beta[proto::beameth::kFrameCount];
    QPushButton *m_send;
    QCheckBox *m_periodic;
    QPushButton *m_apply = nullptr;
    proto::beameth::Params m_active;   // giá trị đang được gửi theo chu kỳ; chỉ đổi khi bấm Áp dụng
    QSpinBox *m_periodMs;
    QTimer *m_timer = nullptr;
    quint64 m_sentCount = 0;
    QLabel *m_preview, *m_result, *m_hint;
};

} // namespace ui
