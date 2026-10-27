#pragma once
#include "../app/context.h"
#include "../proto/beam_eth_proto.h"
#include <QWidget>

class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace ui {

// Điều khiển beam, gửi qua đường Ethernet giám sát/điều khiển chung với TRB và PSU: nhập Esp, Beta, Freq, Mode riêng cho từng mode 01, 10, 11 (3 cột) rồi gửi một bản tin 42 byte (3 khung 14 byte) (xem proto/beam_eth_proto.h).
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
    void send();

    AppContext m_ctx;
    core::Transport::State m_state = core::Transport::State::Closed;
    QSpinBox *m_freq[proto::beameth::kFrameCount], *m_modeValue[proto::beameth::kFrameCount];   // mỗi cột một mode (01, 10, 11)
    QDoubleSpinBox *m_esp[proto::beameth::kFrameCount], *m_beta[proto::beameth::kFrameCount];
    QPushButton *m_send;
    QLabel *m_preview, *m_result, *m_hint;
};

} // namespace ui
