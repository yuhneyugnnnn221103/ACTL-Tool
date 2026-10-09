#include "psu_config_page.h"
#include "auth.h"
#include "theme.h"
#include "../proto/psu_config_proto.h"
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace ui {

using namespace proto;

namespace {
enum Column { ColGroup, ColName, ColDevice, ColNew };
const QColor kDiffColor(0xFAC775);
const QString kDash = QStringLiteral("--");
}

PsuConfigPage::PsuConfigPage(const AppContext &ctx, QWidget *parent)
    : QWidget(parent), m_ctx(ctx), m_addr(ctx.psuStore->firstAddr())
{
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(buildContent());

    auto *cfg = ctx.psuConfig;
    connect(cfg, &services::PsuConfig::configRead, this, [this](int addr) {
        if (addr != m_addr) return;
        showDeviceColumn();
        if (m_fillNewOnRead) setNewValues(*m_ctx.psuConfig->cached(addr));
        m_fillNewOnRead = false;
        refreshDiff();
    });
    connect(cfg, &services::PsuConfig::progress, this, [this](int done, int total) {
        m_progress->setMaximum(total);
        m_progress->setValue(done);
        m_progressText->setText(QStringLiteral("%1 / %2 PSU").arg(done).arg(total));
    });
    connect(cfg, &services::PsuConfig::finished, this, [this](int ok, int fail) {
        setBusy(false);
        m_fillNewOnRead = false;
        m_progressText->setText(QStringLiteral("Xong: %1 thành công, %2 lỗi").arg(ok).arg(fail));
    });
    selectDevice(m_addr);
}

QWidget *PsuConfigPage::buildContent()
{
    // Danh sách PSU: bấm để chọn PSU đang xem; đánh dấu ô để đưa vào thao tác hàng loạt.
    m_list = new QListWidget;
    m_list->setFixedWidth(170);
    for (int i = 0; i < m_ctx.psuStore->count(); ++i) {
        const int addr = m_ctx.psuStore->firstAddr() + i;
        auto *it = new QListWidgetItem(QStringLiteral("PSU %1").arg(addr), m_list);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Unchecked);
        it->setData(Qt::UserRole, addr);
    }
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *it) {
        if (it) selectDevice(it->data(Qt::UserRole).toInt());
    });

    const auto &fields = psucfg::table().fields();
    m_table = new QTableWidget(fields.size(), 4);
    m_table->setAlternatingRowColors(true);
    m_table->setHorizontalHeaderLabels({QStringLiteral("Nhóm"), QStringLiteral("Trường"),
                                        QStringLiteral("Trên PSU"), QStringLiteral("Giá trị mới")});
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_group = new QComboBox;
    m_group->addItem(QStringLiteral("Tất cả nhóm"));
    m_updating = true;
    for (int r = 0; r < fields.size(); ++r) {
        const Field &f = fields.at(r);
        if (m_group->findText(f.group) < 0) m_group->addItem(f.group);
        for (int c = 0; c < 4; ++c) {
            auto *it = new QTableWidgetItem(c == ColGroup ? f.group : c == ColName ? f.name
                                            : c == ColDevice ? kDash : format(r, 0));
            if (c != ColNew) it->setFlags(it->flags() & ~Qt::ItemIsEditable);
            if (!f.note.isEmpty()) it->setToolTip(f.note);
            m_table->setItem(r, c, it);
        }
    }
    m_updating = false;
    connect(m_group, &QComboBox::currentIndexChanged, this, &PsuConfigPage::applyGroupFilter);
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *it) {
        if (m_updating || it->column() != ColNew) return;
        // Nhận số thập phân hoặc 0x..; sai hoặc vượt độ rộng trường thì trả về 0.
        const int size = psucfg::table().fields().at(it->row()).size;
        const qulonglong max = (1ull << (8 * size)) - 1;
        bool ok = false;
        const qulonglong v = it->text().trimmed().toULongLong(&ok, 0);
        m_updating = true;
        it->setText(format(it->row(), ok && v <= max ? double(v) : 0));
        m_updating = false;
        if (!ok || v > max)
            QMessageBox::warning(this, QStringLiteral("Giá trị không hợp lệ"),
                                 QStringLiteral("Trường này nhận giá trị từ 0 đến %1.").arg(max));
        m_hasNew = true;
        m_ctx.auth->touch();
        refreshDiff();
    });

    auto button = [this](const QString &text, auto slot) {
        auto *b = new QPushButton(text);
        connect(b, &QPushButton::clicked, this, slot);
        m_actions << b;
        return b;
    };
    auto *single = new QHBoxLayout;
    m_title = new QLabel;
    single->addWidget(m_title);
    single->addWidget(m_group);
    single->addStretch(1);
    single->addWidget(button(QStringLiteral("Đọc"), [this] {
        if (!ready()) return;
        m_fillNewOnRead = true;
        setBusy(true);
        m_ctx.psuConfig->readDevices({m_addr});
    }));
    single->addWidget(button(QStringLiteral("Ghi và kiểm tra"), [this] {
        if (!ready()) return;
        if (!m_hasNew) {
            QMessageBox::information(this, QStringLiteral("Ghi cấu hình"),
                                     QStringLiteral("Chưa có giá trị để ghi. Hãy Đọc từ PSU, Mở file hoặc Mặc định trước."));
            return;
        }
        if (QMessageBox::question(this, QStringLiteral("Ghi cấu hình"),
                                  QStringLiteral("Ghi toàn bộ cột \"Giá trị mới\" xuống PSU %1?").arg(m_addr))
            != QMessageBox::Yes) return;
        if (!m_ctx.auth->unlock(this)) return; // chỉ hỏi mật khẩu khi thật sự gửi cấu hình xuống thiết bị
        setBusy(true);
        m_ctx.psuConfig->writeFull(m_addr, newValues());
    }));
    single->addWidget(button(QStringLiteral("Mặc định"), [this] {
        // Mặc định của hãng; có file psu_default_config.json cạnh file chạy thì lấy giá trị trong file.
        setNewValues(psucfg::defaultValues(m_ctx.settings.psuDefaultConfigFile));
        refreshDiff();
    }));
    single->addWidget(button(QStringLiteral("Mở file…"), [this] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Mở file cấu hình"), {}, "JSON (*.json)");
        QFile f(path);
        if (path.isEmpty() || !f.open(QIODevice::ReadOnly)) return;
        QList<double> v = newValues();
        psucfg::table().fromJson(QJsonDocument::fromJson(f.readAll()).object(), v);
        setNewValues(v);
        refreshDiff();
    }));
    single->addWidget(button(QStringLiteral("Lưu file…"), [this] {
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Lưu file cấu hình"),
                                                          QStringLiteral("psu_%1.json").arg(m_addr), "JSON (*.json)");
        QFile f(path);
        if (!path.isEmpty() && f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(QJsonDocument(psucfg::table().toJson(newValues())).toJson());
    }));

    auto *batch = new QHBoxLayout;
    m_progress = new QProgressBar;
    m_progress->setMaximum(1);
    m_progress->setValue(0);
    m_progressText = new QLabel;
    m_cancel = new QPushButton(QStringLiteral("Hủy"));
    m_cancel->setEnabled(false);
    connect(m_cancel, &QPushButton::clicked, this, [this] { m_ctx.psuConfig->cancel(); });
    batch->addWidget(new QLabel(QStringLiteral("Hàng loạt:")));
    batch->addWidget(button(QStringLiteral("Đọc các PSU đã đánh dấu"), [this] {
        const QList<int> devs = checkedDevices();
        if (!ready() || devs.isEmpty()) return;
        setBusy(true);
        m_ctx.psuConfig->readDevices(devs);
    }));
    batch->addWidget(button(QStringLiteral("Ghi toàn bộ cho PSU đã đánh dấu"), [this] {
        const QList<int> devs = checkedDevices();
        if (!ready()) return;
        if (!m_hasNew) {
            QMessageBox::information(this, QStringLiteral("Cấu hình hàng loạt"),
                QStringLiteral("Chưa có giá trị để ghi. Hãy Đọc từ thiết bị, Mở file hoặc Mặc định trước."));
            return;
        }
        if (devs.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("Cấu hình hàng loạt"), QStringLiteral("Cần đánh dấu ít nhất một PSU."));
            return;
        }
        if (QMessageBox::question(this, QStringLiteral("Cấu hình hàng loạt"),
                QStringLiteral("Ghi TOÀN BỘ cột \"Giá trị mới\" xuống %1 PSU? Cấu hình hiện có của từng PSU bị ghi đè hoàn toàn.")
                    .arg(devs.size())) != QMessageBox::Yes) return;
        if (!m_ctx.auth->unlock(this)) return;
        setBusy(true);
        m_ctx.psuConfig->writeFullMany(devs, newValues());
    }));
    batch->addStretch(1);
    auto *progressRow = new QHBoxLayout;
    progressRow->addWidget(m_progress, 1);
    progressRow->addWidget(m_progressText);
    progressRow->addWidget(m_cancel);
    auto *lockBtn = new QPushButton(QStringLiteral("Khóa"));
    auto *pwBtn = new QPushButton(QStringLiteral("Đổi mật khẩu"));
    connect(lockBtn, &QPushButton::clicked, this, [this] { m_ctx.auth->lock(); });
    connect(pwBtn, &QPushButton::clicked, this, [this] { if (m_ctx.auth->unlock(this)) m_ctx.auth->changePassword(this); });
    batch->addWidget(pwBtn);
    batch->addWidget(lockBtn);

    // Nút chính: Đọc, Ghi; các nút còn lại là nút phụ.
    for (QPushButton *b : m_actions) {
        const QString t = b->text();
        if (t != QStringLiteral("Đọc") && t != QStringLiteral("Ghi và kiểm tra") && !t.startsWith(QStringLiteral("Ghi toàn bộ")))
            theme::setRole(b, "secondary");
    }
    for (QPushButton *b : {m_cancel, lockBtn, pwBtn}) theme::setRole(b, "secondary");

    auto *right = new QVBoxLayout;
    right->addLayout(single);
    right->addWidget(m_table, 1);
    right->addLayout(batch);
    right->addLayout(progressRow);

    auto *w = new QWidget;
    auto *l = new QHBoxLayout(w);
    l->addWidget(m_list);
    l->addLayout(right, 1);
    return w;
}

bool PsuConfigPage::ready()
{
    m_ctx.auth->touch();
    return !m_ctx.serviceBusy();
}

void PsuConfigPage::setBusy(bool busy)
{
    for (QPushButton *b : m_actions) b->setEnabled(!busy);
    m_list->setEnabled(!busy);
    m_cancel->setEnabled(busy);
}

QString PsuConfigPage::format(int row, double v) const
{
    const Field &f = psucfg::table().fields().at(row);
    return f.hex ? QStringLiteral("0x%1").arg(qulonglong(v), f.size * 2, 16, QLatin1Char('0')).toUpper().replace("0X", "0x")
                 : QString::number(qulonglong(v));
}

double PsuConfigPage::parseCell(int row) const
{
    return double(m_table->item(row, ColNew)->text().trimmed().toULongLong(nullptr, 0));
}

void PsuConfigPage::selectDevice(int addr)
{
    m_addr = addr;
    m_title->setText(QStringLiteral("<b>PSU %1</b>").arg(addr));
    showDeviceColumn();
    refreshDiff();
}

void PsuConfigPage::showDeviceColumn()
{
    const QList<double> *dev = m_ctx.psuConfig->cached(m_addr);
    m_updating = true;
    for (int r = 0; r < m_table->rowCount(); ++r)
        m_table->item(r, ColDevice)->setText(dev ? format(r, dev->at(r)) : kDash);
    m_updating = false;
}

void PsuConfigPage::setNewValues(const QList<double> &values)
{
    m_updating = true;
    for (int r = 0; r < m_table->rowCount(); ++r) m_table->item(r, ColNew)->setText(format(r, values.at(r)));
    m_updating = false;
    m_hasNew = true;
}

QList<double> PsuConfigPage::newValues() const
{
    QList<double> v;
    for (int r = 0; r < m_table->rowCount(); ++r) v << parseCell(r);
    return v;
}

QList<int> PsuConfigPage::checkedDevices() const
{
    QList<int> out;
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->checkState() == Qt::Checked) out.append(m_list->item(i)->data(Qt::UserRole).toInt());
    return out;
}

QHash<int, double> PsuConfigPage::editedFields() const
{
    QHash<int, double> out;
    const QList<double> *dev = m_ctx.psuConfig->cached(m_addr);
    if (!dev || !m_hasNew) return out;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        const double v = parseCell(r);
        if (v != dev->at(r)) out.insert(r, v);
    }
    return out;
}

void PsuConfigPage::refreshDiff()
{
    const QHash<int, double> edited = editedFields();
    m_updating = true;
    for (int r = 0; r < m_table->rowCount(); ++r)
        m_table->item(r, ColNew)->setBackground(edited.contains(r) ? QBrush(kDiffColor) : QBrush());
    m_updating = false;
}

void PsuConfigPage::applyGroupFilter()
{
    const QString g = m_group->currentIndex() == 0 ? QString() : m_group->currentText();
    for (int r = 0; r < m_table->rowCount(); ++r)
        m_table->setRowHidden(r, !g.isEmpty() && m_table->item(r, ColGroup)->text() != g);
}

} // namespace ui
