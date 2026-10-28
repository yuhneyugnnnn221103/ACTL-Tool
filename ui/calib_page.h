#pragma once
#include "../app/context.h"
#include "../proto/trb_calib_proto.h"
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace ui {

// Tab Calib: tích chọn kênh RX/TX, nhập phase/amp TX/RX từng kênh (số nguyên không dấu 0..255), gửi bản tin Calib 18 18 (150 byte)
// qua RS485 (xem proto/trb_calib_proto.h); nhập/xuất file JSON để dùng lại.
class CalibPage : public QWidget {
    Q_OBJECT
public:
    explicit CalibPage(const AppContext &ctx, QWidget *parent = nullptr);

signals:
    void logEvent(const QString &text);

private:
    proto::calib::Data data() const;
    void setData(const proto::calib::Data &d);
    void target(int &mb, int &trb) const;
    void step(int delta);
    void showState(core::Transport::State s);
    void send();
    void importFile();
    void exportFile();

    AppContext m_ctx;
    QComboBox *m_mb, *m_trb;
    QCheckBox *m_rx[proto::calib::kChannels], *m_tx[proto::calib::kChannels];
    QTableWidget *m_table;                       // 32 hàng x 4 cột: Phase TX, Phase RX, Amp TX, Amp RX
    QSpinBox *m_cell[proto::calib::kChannels][4];
    core::Transport::State m_state = core::Transport::State::Closed;
    QPushButton *m_send;
    QLabel *m_result, *m_hint;
};

} // namespace ui
