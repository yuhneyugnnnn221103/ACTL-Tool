#pragma once
#include "../app/context.h"
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace ui {

// Chế độ Debug COM: bỏ Ethernet và Gateway, nối cổng COM (RS422 qua bộ chuyển đổi) thẳng xuống MỘT thiết bị TRB hoặc PSU để thử riêng
// giao tiếp phần mềm - phần cứng khi Gateway chưa sẵn sàng. Khung giống khung hiện tại; mọi bản tin gửi khung gốc (không có DEV + LEN như khi đi qua Gateway).
// Vì không có Gateway tự hỏi thiết bị, trang này chủ động gửi lệnh hỏi giám sát (xem services/direct_poller.h).
class DirectPage : public QWidget {
    Q_OBJECT
public:
    explicit DirectPage(const AppContext &ctx, QWidget *parent = nullptr);

signals:
    void directModeChanged(bool direct);
    void showTrb(int mb, int trb);
    void showPsu(int addr);
    void logEvent(const QString &text);

private:
    enum Kind { KindTrb = 0, KindPsu = 1, KindTrsu = 2 };

    void setDirect(bool on);
    void showState(core::Transport::State s);
    void refreshPorts();
    void toggleOpen();
    void applyTarget();
    void showStats();
    void sendHex();
    void viewDevice();

    AppContext m_ctx;
    core::Transport::State m_state = core::Transport::State::Closed;
    QCheckBox *m_direct;
    QComboBox *m_port, *m_baud, *m_kind;
    QPushButton *m_open, *m_refresh, *m_once, *m_loop, *m_sendHex, *m_view;
    QSpinBox *m_a, *m_b, *m_period;
    QLabel *m_aLabel, *m_bLabel, *m_status, *m_query, *m_stats, *m_hexResult;
    QLineEdit *m_hex;
};

} // namespace ui
