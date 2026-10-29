#pragma once
#include "../app/context.h"
#include "../services/log_file.h"
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

namespace ui {

class PlotWidget;

// Tab xem log: mở một hoặc nhiều file .alog (ghép lên cùng trục thời gian), chọn thiết bị và trường để vẽ song song
// (mỗi trường một đồ thị, mỗi thiết bị một đường), xem danh sách sự kiện và thời điểm trip.
class LogViewerPage : public QWidget {
    Q_OBJECT
public:
    explicit LogViewerPage(const AppContext &ctx, QWidget *parent = nullptr);

    // Nạp file (dùng cho nút mở file và test); trả về số file nạp được.
    int openFiles(const QStringList &paths);
    int fileCount() const { return m_files.size(); }
    int panelCount() const { return m_panels.size(); }
    void selectDevice(int key, bool on);
    void selectField(int kind, const QString &name, bool on);

private:
    struct LoadedFile { QString path; logfile::Data data; };
    struct Trip { qint64 t; int key; int status; };

    void rebuildData();
    void rebuildDeviceTree();
    void rebuildPlots();
    void rebuildEvents();
    void schedulePlots();
    void fitAll();
    void setView(qint64 t0, qint64 t1);
    void centerOn(qint64 t);
    void removeSelectedFiles();
    void chooseFiles();
    QList<int> checkedDevices(int kind) const;

    AppContext m_ctx;
    QList<LoadedFile> m_files;
    logfile::Data m_data;                       // mọi file đã ghép
    QList<Trip> m_trips;                        // mọi lần đổi trạng thái sang Trip / Quá ngưỡng / trở lại tốt
    qint64 m_t0 = 0, m_t1 = 1000;               // khoảng nhìn dùng chung
    bool m_plotsPending = false;

    QListWidget *m_fileList;
    QTreeWidget *m_devices;
    QListWidget *m_fields;
    QLineEdit *m_fieldFilter;
    QCheckBox *m_onlySelected;
    QTableWidget *m_events;
    QVBoxLayout *m_plotLayout;
    QWidget *m_plotHolder;
    QLabel *m_info, *m_emptyHint;
    QList<PlotWidget *> m_panels;
};

} // namespace ui
