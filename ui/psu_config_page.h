#pragma once
#include "../app/context.h"
#include <QWidget>

class QComboBox;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QTableWidget;

namespace ui {

// Cấu hình PSU: đọc/ghi một PSU, hoặc áp các trường đã sửa cho nhiều PSU. Hỏi mật khẩu kỹ sư khi ghi xuống thiết bị.
class PsuConfigPage : public QWidget {
    Q_OBJECT
public:
    explicit PsuConfigPage(const AppContext &ctx, QWidget *parent = nullptr);

private:
    QWidget *buildContent();
    void selectDevice(int addr);
    void showDeviceColumn();
    void setNewValues(const QList<double> &values);
    QList<double> newValues() const;
    QList<int> checkedDevices() const;
    QHash<int, double> editedFields() const;
    double parseCell(int row) const;
    QString format(int row, double v) const;
    void refreshDiff();
    void applyGroupFilter();
    void setBusy(bool busy);
    bool ready();

    AppContext m_ctx;
    QListWidget *m_list;
    QTableWidget *m_table;
    QComboBox *m_group;
    QLabel *m_title, *m_progressText;
    QProgressBar *m_progress;
    QList<QPushButton *> m_actions;
    QPushButton *m_cancel;
    int m_addr;
    bool m_hasNew = false;      // cột "Giá trị mới" đã có nội dung thật (đọc từ PSU, mở file hoặc nạp mặc định)
    bool m_fillNewOnRead = false;
    bool m_updating = false;
};

} // namespace ui
