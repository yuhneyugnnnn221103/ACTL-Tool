#pragma once
#include "../app/context.h"
#include <QWidget>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace ui {

// Cấu hình TRB: đọc/ghi một thiết bị, hoặc áp các trường đã sửa cho nhiều thiết bị. Hỏi mật khẩu kỹ sư khi ghi xuống thiết bị.
class TrbConfigPage : public QWidget {
    Q_OBJECT
public:
    explicit TrbConfigPage(const AppContext &ctx, QWidget *parent = nullptr);

private:
    using Device = services::TrbConfig::Device;

    QWidget *buildContent();
    void selectDevice(int mb, int trb);
    void showDeviceColumn();
    void setNewValues(const QList<double> &values);
    QList<double> newValues() const;
    QList<Device> checkedDevices() const;
    QHash<int, double> editedFields() const;
    void refreshDiff();
    void applyGroupFilter();
    void setBusy(bool busy);
    bool ready();

    AppContext m_ctx;
    QTreeWidget *m_tree;
    QTableWidget *m_table;
    QComboBox *m_group;
    QLabel *m_title, *m_progressText;
    QProgressBar *m_progress;
    QList<QPushButton *> m_actions;
    QPushButton *m_cancel;
    int m_mb = 0, m_trb = 0;
    bool m_hasNew = false;      // cột "Giá trị mới" đã có nội dung thật (đọc từ thiết bị hoặc mở file)
    bool m_updating = false;
};

} // namespace ui
