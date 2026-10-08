#pragma once
// Giao diện đơn giản cho actl_sim: chọn cách nối tới Gateway, xem lưới 20 x 8 TRB và ép trạng thái từng TRB.
#include "bus_port.h"
#include <QMainWindow>
#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QLineEdit;

namespace ui { class StatusPill; }

namespace sim {

class SimWindow : public QMainWindow {
    Q_OBJECT
public:
    SimWindow();
    ~SimWindow() override;

private:
    QWidget *buildConnection();
    QWidget *buildGrid();
    QWidget *buildActions();
    QWidget *buildFaults();
    void rebuildWorld();
    void toggleRun();
    void start();
    void stop();
    void refresh();                 // màu lưới + thống kê
    void applyFaults();
    void log(const QString &text);
    void forSelected(const std::function<void(TrbModel *)> &fn);
    void loadSettings();
    void saveSettings() const;
    void setRunningUi(bool running);

    // kết nối
    QRadioButton *m_serial, *m_connect, *m_listen;
    QComboBox *m_ports, *m_baud;
    QLineEdit *m_host;
    QSpinBox *m_connectPort, *m_listenPort, *m_mb, *m_trbPerMb, *m_debugPeriod;
    QPushButton *m_run;
    ui::StatusPill *m_pill;
    // lưới và thao tác
    QTableWidget *m_grid;
    QComboBox *m_field, *m_tripIndex;
    QDoubleSpinBox *m_value;
    QList<QCheckBox *> m_tripBits;
    QLabel *m_selection, *m_stats;
    // lỗi bơm
    QSpinBox *m_drop, *m_corrupt, *m_delayMin, *m_delayMax;
    QPlainTextEdit *m_log;
    QCheckBox *m_hex;

    std::unique_ptr<SimWorld> m_world;
    std::unique_ptr<core::Transport> m_transport;
    std::unique_ptr<BusPort> m_port;
};

} // namespace sim
