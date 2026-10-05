#pragma once
#include "../app/context.h"
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;

namespace ui {

// Nạp code: FPGA trên TRB (broadcast hoặc gửi riêng) và STM32 trên PSU (lần lượt). Cần mở khóa kỹ sư.
class FirmwarePage : public QWidget {
    Q_OBJECT
public:
    explicit FirmwarePage(const AppContext &ctx, QWidget *parent = nullptr);

private:
    QWidget *buildFpgaTab();
    QWidget *buildStmTab();
    bool ready();
    services::FpgaOta::Options fpgaOptions() const;
    void fpgaShowNode(int index);
    void fpgaSetBusy(bool busy);
    static QByteArray pickFile(QWidget *parent, QLineEdit *edit, QLabel *info);

    AppContext m_ctx;

    QTableWidget *m_fpgaTable;
    QRadioButton *m_fpgaBroadcast;
    QCheckBox *m_fpgaPerPacket;
    QSpinBox *m_fpgaEraseMin, *m_fpgaGap;
    QLabel *m_fpgaPhase;
    QProgressBar *m_fpgaProgress;
    QList<QPushButton *> m_fpgaActions;
    QPushButton *m_fpgaSkip, *m_fpgaCancel;
    QByteArray m_fpgaImage;

    QTableWidget *m_stmTable;
    QSpinBox *m_stmVersion;
    QCheckBox *m_stmAutoCommit;
    QList<QPushButton *> m_stmActions;
    QPushButton *m_stmCancel;
    QByteArray m_stmImageA, m_stmImageB;
};

} // namespace ui
