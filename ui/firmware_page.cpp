#include "firmware_page.h"
#include "auth.h"
#include "theme.h"
#include <QCheckBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>

namespace ui {

using services::FpgaOta;

namespace {
QTableWidget *makeTable(const QStringList &headers, int rows)
{
    auto *t = new QTableWidget(rows, headers.size());
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->hide();
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionMode(QAbstractItemView::NoSelection);
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < headers.size(); ++c) t->setItem(r, c, new QTableWidgetItem);
    return t;
}
QString hex32(quint32 v) { return QStringLiteral("0x%1").arg(v, 8, 16, QLatin1Char('0')).toUpper().replace("0X", "0x"); }
bool yes(QWidget *parent, const QString &title, const QString &text)
{
    return QMessageBox::question(parent, title, text) == QMessageBox::Yes;
}
}

FirmwarePage::FirmwarePage(const AppContext &ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    auto *tabs = new QTabWidget;
    tabs->addTab(buildFpgaTab(), QStringLiteral("FPGA trên TRB"));
    tabs->addTab(buildStmTab(), QStringLiteral("STM32 trên PSU"));

    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(tabs);
}

bool FirmwarePage::ready()
{
    m_ctx.auth->touch();
    if (m_ctx.serviceBusy()) {
        QMessageBox::information(this, QStringLiteral("Đường RS485 đang bận"),
                                 QStringLiteral("Đang có thao tác cấu hình hoặc nạp code khác chạy trên đường RS485."));
        return false;
    }
    return true;
}

QByteArray FirmwarePage::pickFile(QWidget *parent, QLineEdit *edit, QLabel *info)
{
    const QString path = QFileDialog::getOpenFileName(parent, QStringLiteral("Chọn file firmware"), {}, "Binary (*.bin);;Tất cả (*)");
    QFile f(path);
    if (path.isEmpty() || !f.open(QIODevice::ReadOnly)) return {};
    const QByteArray data = f.readAll();
    edit->setText(path);
    info->setText(QStringLiteral("%1 byte, %2 gói").arg(data.size()).arg((data.size() + 255) / 256));
    return data;
}

// ============================ FPGA ============================

QWidget *FirmwarePage::buildFpgaTab()
{
    const int perMb = m_ctx.store->trbPerMb(), total = m_ctx.store->mbCount() * perMb;
    m_fpgaTable = makeTable({QStringLiteral("TRB"), QStringLiteral("Kết quả"), QStringLiteral("CODE"),
                             QStringLiteral("STAT"), QStringLiteral("BOOTSTS"), QStringLiteral("WBSTAR")}, total);
    for (int r = 0; r < total; ++r) {
        QTableWidgetItem *it = m_fpgaTable->item(r, 0);
        it->setText(QStringLiteral("MB%1 / TRB%2").arg(r / perMb).arg(r % perMb));
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Checked);
    }

    auto *file = new QLineEdit;
    file->setReadOnly(true);
    auto *fileInfo = new QLabel;
    auto *browse = new QPushButton(QStringLiteral("Chọn file .bin…"));
    theme::setRole(browse, "secondary");
    connect(browse, &QPushButton::clicked, this, [=] {
        const QByteArray d = pickFile(this, file, fileInfo);
        if (!d.isEmpty()) m_fpgaImage = d;
    });

    m_fpgaBroadcast = new QRadioButton(QStringLiteral("Broadcast: nạp cho MỌI TRB trong hệ thống"));
    auto *unicast = new QRadioButton(QStringLiteral("Gửi riêng: chỉ các TRB được đánh dấu (kiểm tra từng gói)"));
    m_fpgaBroadcast->setChecked(true);
    m_fpgaPerPacket = new QCheckBox(QStringLiteral("Khi broadcast, hỏi trạng thái mọi TRB sau từng gói (rất chậm)"));
    connect(m_fpgaBroadcast, &QRadioButton::toggled, m_fpgaPerPacket, &QCheckBox::setEnabled);
    m_fpgaEraseMin = new QSpinBox;
    m_fpgaEraseMin->setRange(0, 30);
    m_fpgaEraseMin->setValue(m_ctx.settings.fpgaEraseWaitSec / 60);
    m_fpgaEraseMin->setSuffix(QStringLiteral(" phút"));
    m_fpgaGap = new QSpinBox;
    m_fpgaGap->setRange(0, 1000);
    m_fpgaGap->setValue(m_ctx.settings.fpgaPacketGapMs);
    m_fpgaGap->setSuffix(QStringLiteral(" ms"));

    auto button = [this](const QString &text, auto slot) {
        auto *b = new QPushButton(text);
        connect(b, &QPushButton::clicked, this, slot);
        m_fpgaActions << b;
        return b;
    };
    auto selected = [this, perMb] {
        QList<FpgaOta::Device> d;
        for (int r = 0; r < m_fpgaTable->rowCount(); ++r)
            if (m_fpgaBroadcast->isChecked() || m_fpgaTable->item(r, 0)->checkState() == Qt::Checked)
                d.append({r / perMb, r % perMb});
        return d;
    };

    auto *check = button(QStringLiteral("1. Kiểm tra kết nối"), [=] {
        if (!ready()) return;
        const auto devs = selected();
        if (devs.isEmpty()) return;
        for (int r = 0; r < m_fpgaTable->rowCount(); ++r)
            for (int c = 1; c < m_fpgaTable->columnCount(); ++c) m_fpgaTable->item(r, c)->setText({});
        m_ctx.fpgaOta->setNodes(devs);
        fpgaSetBusy(true);
        m_ctx.fpgaOta->checkOnline();
    });
    auto who = [this] {
        return m_fpgaBroadcast->isChecked() ? QStringLiteral("MỌI TRB trong hệ thống (broadcast)")
                                            : QStringLiteral("%1 TRB đã kiểm tra ở bước 1").arg(m_ctx.fpgaOta->nodes().size());
    };
    auto *erase = button(QStringLiteral("2. Xóa flash"), [=] {
        if (!ready()) return;
        if (m_ctx.fpgaOta->nodes().isEmpty()) { QMessageBox::information(this, QStringLiteral("Nạp FPGA"), QStringLiteral("Hãy chạy bước 1 trước.")); return; }
        if (!yes(this, QStringLiteral("Xóa flash"), QStringLiteral("Xóa vùng flash cập nhật của %1?").arg(who()))) return;
        if (!m_ctx.auth->unlock(this)) return; // chỉ hỏi mật khẩu khi thật sự xóa flash / nạp / boot / nạp STM32
        fpgaSetBusy(true);
        m_ctx.fpgaOta->erase(fpgaOptions());
    });
    auto *load = button(QStringLiteral("3. Nạp code"), [=] {
        if (!ready()) return;
        if (m_fpgaImage.isEmpty()) { QMessageBox::information(this, QStringLiteral("Nạp FPGA"), QStringLiteral("Chưa chọn file .bin.")); return; }
        if (m_ctx.fpgaOta->nodes().isEmpty()) { QMessageBox::information(this, QStringLiteral("Nạp FPGA"), QStringLiteral("Hãy chạy bước 1 trước.")); return; }
        QString text = QStringLiteral("Nạp %1 byte cho %2?").arg(m_fpgaImage.size()).arg(who());
        if (!m_ctx.fpgaOta->erased()) text += QStringLiteral("\n\nChưa xóa flash ở bước 2 (hoặc đã nạp xong lần trước). Nạp lên vùng chưa xóa có thể ghi sai.");
        if (!yes(this, QStringLiteral("Nạp code"), text)) return;
        if (!m_ctx.auth->unlock(this)) return;
        fpgaSetBusy(true);
        m_ctx.fpgaOta->load(m_fpgaImage, fpgaOptions());
    });
    auto *boot = button(QStringLiteral("4. Boot và xác nhận"), [=] {
        if (!ready() || m_ctx.fpgaOta->nodes().isEmpty()) return;
        int failed = 0;
        for (const FpgaOta::Node &n : m_ctx.fpgaOta->nodes()) failed += n.failed;
        QString text = QStringLiteral("Gửi lệnh boot sang bitstream mới?");
        if (failed) text += QStringLiteral("\n\nCó %1 TRB báo lỗi khi nạp.").arg(failed)
                          + (m_fpgaBroadcast->isChecked() ? QStringLiteral(" Lệnh boot broadcast vẫn tới cả các TRB này.") : QString());
        if (!yes(this, QStringLiteral("Boot"), text)) return;
        if (!m_ctx.auth->unlock(this)) return;
        fpgaSetBusy(true);
        m_ctx.fpgaOta->bootAndVerify(fpgaOptions());
    });
    m_fpgaSkip = new QPushButton(QStringLiteral("Bỏ qua thời gian chờ xóa"));
    m_fpgaCancel = new QPushButton(QStringLiteral("Hủy"));
    connect(m_fpgaSkip, &QPushButton::clicked, this, [this] { m_ctx.fpgaOta->skipEraseWait(); });
    connect(m_fpgaCancel, &QPushButton::clicked, this, [this] { m_ctx.fpgaOta->cancel(); });
    m_fpgaPhase = new QLabel;
    m_fpgaProgress = new QProgressBar;
    m_fpgaProgress->setMaximum(1);
    m_fpgaProgress->setValue(0);

    auto *fileRow = new QHBoxLayout;
    fileRow->addWidget(file, 1);
    fileRow->addWidget(fileInfo);
    fileRow->addWidget(browse);
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Bitstream"), fileRow);
    form->addRow(QStringLiteral("Chế độ"), m_fpgaBroadcast);
    form->addRow(QString(), unicast);
    form->addRow(QString(), m_fpgaPerPacket);
    form->addRow(QStringLiteral("Chờ sau khi xóa flash"), m_fpgaEraseMin);
    form->addRow(QStringLiteral("Nghỉ giữa các gói"), m_fpgaGap);
    auto *buttons = new QHBoxLayout;
    for (QPushButton *b : {check, erase, load, boot, m_fpgaSkip, m_fpgaCancel}) buttons->addWidget(b);
    theme::setRole(erase, "danger");
    theme::setRole(load, "danger");   // xóa flash / nạp / boot: màu riêng để không bấm nhầm
    theme::setRole(boot, "danger");
    theme::setRole(m_fpgaSkip, "secondary");
    theme::setRole(m_fpgaCancel, "secondary");
    buttons->addWidget(m_fpgaProgress, 1);

    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->addLayout(form);
    l->addLayout(buttons);
    l->addWidget(m_fpgaPhase);
    l->addWidget(m_fpgaTable, 1);
    fpgaSetBusy(false);

    FpgaOta *ota = m_ctx.fpgaOta;
    connect(ota, &FpgaOta::nodeChanged, this, &FirmwarePage::fpgaShowNode);
    connect(ota, &FpgaOta::phaseChanged, m_fpgaPhase, &QLabel::setText);
    connect(ota, &FpgaOta::progress, this, [this](int done, int total) {
        m_fpgaProgress->setMaximum(qMax(1, total));
        m_fpgaProgress->setValue(done);
    });
    connect(ota, &FpgaOta::finished, this, [this] {
        fpgaSetBusy(false);
        for (int i = 0; i < m_ctx.fpgaOta->nodes().size(); ++i) fpgaShowNode(i);
    });
    return w;
}

FpgaOta::Options FirmwarePage::fpgaOptions() const
{
    FpgaOta::Options o;
    o.broadcast = m_fpgaBroadcast->isChecked();
    o.perPacketCheck = m_fpgaPerPacket->isChecked();
    o.eraseWaitSec = m_fpgaEraseMin->value() * 60;
    o.packetGapMs = m_fpgaGap->value();
    o.bootWaitMs = m_ctx.settings.fpgaBootWaitMs;
    return o;
}

void FirmwarePage::fpgaShowNode(int index)
{
    const FpgaOta::Node &n = m_ctx.fpgaOta->nodes().at(index);
    const int row = n.dev.first * m_ctx.store->trbPerMb() + n.dev.second;
    m_fpgaTable->item(row, 1)->setText(n.note);
    m_fpgaTable->item(row, 1)->setBackground(n.failed || !n.online ? QBrush(QColor(0xF09595)) : QBrush(QColor(0xC0DD97)));
    if (!n.online) return;
    m_fpgaTable->item(row, 2)->setText(QStringLiteral("0x%1").arg(n.status.code, 2, 16, QLatin1Char('0')));
    m_fpgaTable->item(row, 3)->setText(hex32(n.status.stat));
    m_fpgaTable->item(row, 4)->setText(hex32(n.status.bootsts));
    m_fpgaTable->item(row, 5)->setText(hex32(n.status.wbstar));
}

void FirmwarePage::fpgaSetBusy(bool busy)
{
    for (QPushButton *b : m_fpgaActions) b->setEnabled(!busy);
    m_fpgaSkip->setEnabled(busy);
    m_fpgaCancel->setEnabled(busy);
}

// ============================ STM32 ============================

QWidget *FirmwarePage::buildStmTab()
{
    const int count = m_ctx.settings.psuCount, first = m_ctx.settings.psuFirstAddr;
    m_stmTable = makeTable({QStringLiteral("PSU"), QStringLiteral("Slot đang chạy"), QStringLiteral("Version"),
                            QStringLiteral("Tiến độ"), QStringLiteral("Kết quả")}, count);
    for (int r = 0; r < count; ++r) {
        QTableWidgetItem *it = m_stmTable->item(r, 0);
        it->setText(QStringLiteral("PSU địa chỉ %1").arg(first + r));
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Checked);
    }
    auto row = [=](int addr) { return addr - first; };
    auto selected = [=] {
        QList<int> a;
        for (int r = 0; r < count; ++r)
            if (m_stmTable->item(r, 0)->checkState() == Qt::Checked) a << first + r;
        return a;
    };

    auto *form = new QFormLayout;
    for (QByteArray *image : {&m_stmImageA, &m_stmImageB}) {
        auto *edit = new QLineEdit;
        edit->setReadOnly(true);
        auto *info = new QLabel;
        auto *browse = new QPushButton(QStringLiteral("Chọn file .bin…"));
        theme::setRole(browse, "secondary");
        connect(browse, &QPushButton::clicked, this, [=] {
            const QByteArray d = pickFile(this, edit, info);
            if (!d.isEmpty()) *image = d;
        });
        auto *h = new QHBoxLayout;
        h->addWidget(edit, 1);
        h->addWidget(info);
        h->addWidget(browse);
        form->addRow(image == &m_stmImageA ? QStringLiteral("Ảnh link cho slot A") : QStringLiteral("Ảnh link cho slot B"), h);
    }
    m_stmVersion = new QSpinBox;
    m_stmVersion->setRange(1, 2147483647);
    m_stmVersion->setValue(int(QDateTime::currentSecsSinceEpoch()));
    m_stmVersion->setToolTip(QStringLiteral("Bootloader chọn slot có version lớn hơn khi cả hai slot đều hợp lệ. Mặc định là thời điểm hiện tại."));
    form->addRow(QStringLiteral("Version"), m_stmVersion);
    m_stmAutoCommit = new QCheckBox(QStringLiteral("Tự kích hoạt (COMMIT, thiết bị reset) ngay khi CRC32 đúng"));
    form->addRow(QString(), m_stmAutoCommit);

    auto button = [this](const QString &text, auto slot) {
        auto *b = new QPushButton(text);
        connect(b, &QPushButton::clicked, this, slot);
        m_stmActions << b;
        return b;
    };
    auto setBusy = [this](bool busy) {
        for (QPushButton *b : m_stmActions) b->setEnabled(!busy);
        m_stmCancel->setEnabled(busy);
    };
    m_stmCancel = new QPushButton(QStringLiteral("Hủy"));
    connect(m_stmCancel, &QPushButton::clicked, this, [this] { m_ctx.stmOta->cancel(); });
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(button(QStringLiteral("Đọc slot và version"), [=] {
        if (!ready() || selected().isEmpty()) return;
        setBusy(true);
        m_ctx.stmOta->queryInfo(selected());
    }));
    buttons->addWidget(button(QStringLiteral("Nạp các PSU đã đánh dấu"), [=] {
        if (!ready() || selected().isEmpty()) return;
        if (m_stmImageA.isEmpty() && m_stmImageB.isEmpty()) { QMessageBox::information(this, QStringLiteral("Nạp STM32"), QStringLiteral("Chưa chọn file .bin.")); return; }
        if (!yes(this, QStringLiteral("Nạp STM32"),
                 QStringLiteral("Nạp firmware version %1 cho %2 PSU, lần lượt từng thiết bị?\n\nThiết bị phải đang ở SAFE_OFF mới chấp nhận.")
                     .arg(m_stmVersion->value()).arg(selected().size()))) return;
        if (!m_ctx.auth->unlock(this)) return;
        for (int r = 0; r < count; ++r) { m_stmTable->item(r, 3)->setText({}); m_stmTable->item(r, 4)->setText({}); m_stmTable->item(r, 4)->setBackground({}); }
        setBusy(true);
        m_ctx.stmOta->start(selected(), m_stmImageA, m_stmImageB, quint32(m_stmVersion->value()), m_stmAutoCommit->isChecked());
    }));
    buttons->addWidget(m_stmCancel);
    for (QPushButton *b : m_stmActions) theme::setRole(b, b->text().startsWith(QStringLiteral("Nạp")) ? "danger" : "secondary");
    theme::setRole(m_stmCancel, "secondary");
    buttons->addStretch(1);

    services::StmOta *ota = m_ctx.stmOta;
    connect(ota, &services::StmOta::deviceInfo, this, [=](int addr, char slot, quint32 version) {
        m_stmTable->item(row(addr), 1)->setText(QString(QLatin1Char(slot)));
        m_stmTable->item(row(addr), 2)->setText(QString::number(version));
    });
    connect(ota, &services::StmOta::deviceProgress, this, [=](int addr, int percent, const QString &text) {
        m_stmTable->item(row(addr), 3)->setText(QStringLiteral("%1% · %2").arg(percent).arg(text));
    });
    connect(ota, &services::StmOta::deviceFinished, this, [=](int addr, bool ok, const QString &m) {
        m_stmTable->item(row(addr), 4)->setText(m);
        m_stmTable->item(row(addr), 4)->setBackground(QBrush(ok ? QColor(0xC0DD97) : QColor(0xF09595)));
    });
    connect(ota, &services::StmOta::commitRequested, this, [=](int addr) {
        m_ctx.stmOta->confirmCommit(yes(this, QStringLiteral("Kích hoạt firmware"),
            QStringLiteral("PSU địa chỉ %1 đã nhận đủ ảnh và CRC32 đúng.\n\nKích hoạt ngay? Thiết bị sẽ reset.").arg(addr)));
    });
    connect(ota, &services::StmOta::finished, this, [=] { setBusy(false); });

    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->addLayout(form);
    l->addLayout(buttons);
    l->addWidget(m_stmTable, 1);
    setBusy(false);
    return w;
}

} // namespace ui
