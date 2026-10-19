#pragma once
#include "../app/context.h"
#include <QWidget>

class QCheckBox;
class QLabel;
class QPushButton;
class QTableWidget;

namespace ui { class BitCells; class StatusPill; }

namespace ui {

// Giám sát và điều khiển PSU: chọn PSU ở hàng nút trên cùng, số liệu thô theo 4 cụm DCM, nguồn phụ, trip code.
class PsuPage : public QWidget {
    Q_OBJECT
public:
    explicit PsuPage(const AppContext &ctx, QWidget *parent = nullptr);

    void setDevice(int addr);
    void refresh();

private:
    QWidget *buildMonitor();
    QWidget *buildControl();
    void sendControl();

    AppContext m_ctx;
    int m_addr;
    QList<QPushButton *> m_selectors;
    StatusPill *m_pill;
    QLabel *m_status;
    QTableWidget *m_cluster, *m_supply;
    QList<QLabel *> m_rtc;
    QList<BitCells *> m_trip;
    QCheckBox *m_power, *m_clearTrip;
    QList<QLabel *> m_fw;   // thông tin firmware PSU (vùng mở rộng của bản tin giám sát)
};

} // namespace ui
