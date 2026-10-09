#pragma once
#include "../model/device_store.h"
#include "../model/thresholds.h"
#include "../services/trb_control.h"
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QSpinBox;
class QTableWidget;

namespace ui {

class BitCells;
class LedIndicator;
class StatusPill;

// Giám sát chi tiết và điều khiển một TRB.
class TrbDetailPage : public QWidget {
    Q_OBJECT
public:
    TrbDetailPage(const model::DeviceStore *store, services::TrbControl *control,
                  const model::Thresholds *thresholds, QWidget *parent = nullptr);

    void setDevice(int mb, int trb);
    int mb() const { return m_mb; }
    int trb() const { return m_trb; }
    void refresh();

signals:
    void deviceChanged(int mb, int trb);

private:
    QWidget *buildMonitor();
    QWidget *buildControl();
    QTableWidget *makeTable(const QStringList &rows, int firstField);
    void step(int delta);
    void sendControl();
    void sendBeam();
    bool confirmTarget(int &mb, int &trb);

    const model::DeviceStore *m_store;
    services::TrbControl *m_control;
    const model::Thresholds *m_thresholds;
    int m_mb = 0, m_trb = 0;

    QComboBox *m_mbBox, *m_trbBox, *m_target, *m_mode;
    QList<StatusPill *> m_pills;
    StatusPill *m_debugPill;   // m_debugPill: TRB đang ở chế độ Debug (nếu có)
    QLabel *m_status;
    struct Table { QTableWidget *w; int firstField; };
    QList<Table> m_tables;
    QList<QLabel *> m_metrics;
    QList<QLabel *> m_stateLabels;   // 4 nhãn trạng thái TRM1..4
    QList<BitCells *> m_trip;
    QList<LedIndicator *> m_ledAdar, m_ledPg, m_ledPa;
    QCheckBox *m_pa[4], *m_start, *m_clearTrip, *m_beamSync, *m_adar[8], *m_ch[4];
    QSpinBox *m_phaseTx, *m_phaseRx, *m_ampTx, *m_ampRx;
};

} // namespace ui
