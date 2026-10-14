#pragma once
#include "../app/context.h"
#include "../proto/beam_eth_proto.h"
#include <QWidget>

class QButtonGroup;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace ui {

class StatusPill;

// Điều khiển beam qua đường Ethernet riêng: nhập Esp, Beta, Freq, Mode rồi gửi một bản tin 14 byte (xem proto/beam_eth_proto.h).
class BeamEthPage : public QWidget {
    Q_OBJECT
public:
    explicit BeamEthPage(const AppContext &ctx, QWidget *parent = nullptr);

signals:
    void logEvent(const QString &text);   // dòng sự kiện đã gắn mã [CTRL] / [NET]

private:
    proto::beameth::Params params() const;
    void toggleConnection();
    void showState(core::Transport::State s);
    void updatePreview();
    void send();

    AppContext m_ctx;
    core::Transport::State m_state = core::Transport::State::Closed;
    StatusPill *m_pill;
    QLineEdit *m_address;
    QSpinBox *m_port, *m_freq, *m_modeValue;
    QPushButton *m_connect, *m_send;
    QDoubleSpinBox *m_esp, *m_beta;
    QButtonGroup *m_mode;
    QLabel *m_preview, *m_result;
};

} // namespace ui
