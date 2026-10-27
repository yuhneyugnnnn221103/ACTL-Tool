#pragma once
#include "../app/context.h"
#include "log_filter.h"
#include <QMainWindow>

class QCheckBox;
class QFrame;
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
    void showConnBox();
    void watchLink(core::Link *link, const QString &title, StatusPill *pill, QPushButton *button);
    void toggleTcp();
    void toggleSerial();
    void refreshPorts();
    void showDetail(int mb, int trb);
    void showPsu(int addr);
    void refresh();
    void rebuildAbnormalList();
    void logHex(const QString &link, const char *dir, const QByteArray &raw);
    void logHexError(const QString &link, const QString &label, const QByteArray &raw);   // byte bị loại: hiện ở mọi trang
    void refilterLogs();
    QString trbTooltip(int mb, int trb) const;
    QString psuTooltip(int addr, int cluster) const;

    AppContext m_ctx;
    model::DeviceStore *m_store;

    QLineEdit *m_tcpAddress;
    QSpinBox *m_tcpPort;
    QComboBox *m_comPort, *m_comBaud;
    QPushButton *m_tcpButton, *m_comButton;
    StatusPill *m_tcpPill, *m_comPill, *m_sparePill;   // thẻ trạng thái các đường trong popup
    StatusPill *m_connPill;              // một thẻ cho đường của trang đang mở; bấm để xem đủ 4 đường
    QFrame *m_connPopup;
    QLabel *m_rxErrLabel = nullptr;      // thống kê lỗi nhận, ở cuối cửa sổ thả
    void syncConnPill();
    void showConnPopup();
    QList<int> m_pageConn;               // ô kết nối của từng trang, -1 = không có
    qint64 m_lastRejectLogMs = 0;
    QString m_rxErrText = QStringLiteral("Lỗi CRC: 0 · Sai đuôi: 0 · Byte bỏ qua: 0");   // hiện trong cửa sổ thả của thẻ kết nối
    StatusPill *m_counterPills[5];
    core::Transport::State m_tcpState = core::Transport::State::Closed, m_comState = core::Transport::State::Closed;

    OverviewGrid *m_grid;
    TrbDetailPage *m_detail;
    PsuPage *m_psu;
    QWidget *m_detailHolder = nullptr, *m_psuHolder = nullptr;   // vùng cuộn bọc quanh trang chi tiết / PSU
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
    void editLogSchedule();
    bool m_listDirty = true;
};

} // namespace ui
