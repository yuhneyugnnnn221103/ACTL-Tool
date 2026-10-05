#pragma once
#include "../app/context.h"
#include <QMainWindow>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;

namespace ui {

class OverviewGrid;
class PsuGrid;
class PsuPage;
class TrbDetailPage;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const AppContext &ctx, QWidget *parent = nullptr);

    void logEvent(const QString &text);

private:
    QWidget *buildTopBar();
    QWidget *buildOverviewPage();
    QWidget *buildLogPanel();
    void watchLink(core::Link *link, const QString &title, QLabel *pill, QPushButton *button);
    void toggleTcp();
    void toggleSerial();
    void refreshPorts();
    void showDetail(int mb, int trb);
    void showPsu(int addr);
    void refresh();
    void rebuildAbnormalList();
    QString trbTooltip(int mb, int trb) const;
    QString psuTooltip(int addr, int cluster) const;

    AppContext m_ctx;
    model::DeviceStore *m_store;

    QLineEdit *m_tcpAddress;
    QSpinBox *m_tcpPort;
    QComboBox *m_comPort, *m_comBaud;
    QPushButton *m_tcpButton, *m_comButton;
    QLabel *m_tcpPill, *m_comPill, *m_rxErrLabel, *m_counters;
    core::Transport::State m_tcpState = core::Transport::State::Closed, m_comState = core::Transport::State::Closed;

    OverviewGrid *m_grid;
    TrbDetailPage *m_detail;
    PsuPage *m_psu;
    PsuGrid *m_psuGrid;
    QListWidget *m_nav, *m_abnormal;
    QStackedWidget *m_pages;
    QPlainTextEdit *m_eventLog, *m_hexLog;
    QCheckBox *m_hexEnable;
    bool m_listDirty = true;
};

} // namespace ui
