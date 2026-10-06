#pragma once
#include "../app/context.h"
#include "log_filter.h"
#include <QMainWindow>

class QCheckBox;
class QComboBox;
class QLabel;
class QHBoxLayout;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;

namespace ui {

class OverviewGrid;
class PsuGrid;
class StatusPill;
class PsuPage;
class TrbDetailPage;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const AppContext &ctx, QWidget *parent = nullptr);

    // group/mb/trb: để khung log chỉ hiện dòng thuộc trang đang mở (xem log_filter.h); LogSystem hiện ở mọi trang.
    void logEvent(const QString &text, int group = LogSystem, int mb = -1, int trb = -1);

private:
    QWidget *buildTopBar();
    QWidget *buildOverviewPage();
    QWidget *buildLogPanel();
    void watchLink(core::Link *link, const QString &title, StatusPill *pill, QPushButton *button);
    void toggleTcp();
    void toggleSerial();
    void refreshPorts();
    void showDetail(int mb, int trb);
    void showPsu(int addr);
    void refresh();
    void rebuildAbnormalList();
    void logHex(const QString &link, const char *dir, const QByteArray &raw);
    void refilterLogs();
    QString trbTooltip(int mb, int trb) const;
    QString psuTooltip(int addr, int cluster) const;

    AppContext m_ctx;
    model::DeviceStore *m_store;

    QLineEdit *m_tcpAddress;
    QSpinBox *m_tcpPort;
    QComboBox *m_comPort, *m_comBaud;
    QPushButton *m_tcpButton, *m_comButton;
    StatusPill *m_tcpPill, *m_comPill;
    QLabel *m_rxErrLabel;
    StatusPill *m_counterPills[5];
    core::Transport::State m_tcpState = core::Transport::State::Closed, m_comState = core::Transport::State::Closed;

    OverviewGrid *m_grid;
    TrbDetailPage *m_detail;
    PsuPage *m_psu;
    PsuGrid *m_psuGrid;
    QListWidget *m_nav, *m_abnormal;
    QStackedWidget *m_pages;
    struct LogEntry { QString text; int group = LogSystem; int mb = -1, trb = -1; };
    bool logVisible(const LogEntry &e) const;
    void appendLog(QPlainTextEdit *edit, QList<LogEntry> &buffer, const LogEntry &e);
    void rebuildLog(QPlainTextEdit *edit, const QList<LogEntry> &buffer);

    QPlainTextEdit *m_eventLog = nullptr, *m_hexLog = nullptr;
    QList<LogEntry> m_eventBuffer, m_hexBuffer;   // toàn bộ log (tối đa 5000 dòng mỗi loại); khung chỉ hiện phần của trang
    QList<int> m_pageGroups;                      // nhóm log của từng trang, theo chỉ số trang
    QCheckBox *m_hexEnable;
    bool m_listDirty = true;
};

} // namespace ui
