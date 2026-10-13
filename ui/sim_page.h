#pragma once
// Trang Giả lập: đóng vai các TRB ở phía thiết bị của Gateway (qua một cổng COM RS485), xem lưới 20 x 8 TRB
// và ép trạng thái từng TRB. Chạy ngay trong ACTL Tool; mô hình thiết bị dùng chung với actl_sim_cli.
#include "../app/context.h"
#include "../sim/bus_port.h"
#include <QWidget>
#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace ui {

class StatusPill;

class SimPage : public QWidget {
    Q_OBJECT
public:
    explicit SimPage(const AppContext &ctx, QWidget *parent = nullptr);
    ~SimPage() override;

    bool running() const { return m_port != nullptr; }

signals:
    void runningChanged(bool running);

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
    void forSelected(const std::function<void(sim::TrbModel *)> &fn);
    void loadSettings();
    void saveSettings() const;
    void setRunningUi(bool running);

    // kết nối
    QComboBox *m_ports, *m_baud;
    QSpinBox *m_mb, *m_trbPerMb, *m_debugPeriod;
    QPushButton *m_run;
    StatusPill *m_pill;
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

    AppContext m_ctx;
    std::unique_ptr<sim::SimWorld> m_world;
    std::unique_ptr<core::Transport> m_transport;
    std::unique_ptr<sim::BusPort> m_port;
};

} // namespace ui
