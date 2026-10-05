#include "trb_config_page.h"
#include "auth.h"
#include "../proto/trb_config_proto.h"
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace ui {

using namespace proto;

namespace {
enum Column { ColGroup, ColName, ColDevice, ColNew };
const QColor kDiffColor(0xFAC775);

QString fmt(double v) { return QString::number(qulonglong(v)); }
}

TrbConfigPage::TrbConfigPage(const AppContext &ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(buildContent());

    auto *cfg = ctx.trbConfig;
    connect(cfg, &services::TrbConfig::configRead, this, [this](int mb, int trb) {
        if (mb != m_mb || trb != m_trb) return;
        showDeviceColumn();
        if (m_fillNewOnRead) setNewValues(*m_ctx.trbConfig->cached(mb, trb));
        m_fillNewOnRead = false;
        refreshDiff();
    });
    connect(cfg, &services::TrbConfig::progress, this, [this](int done, int total) {
        m_progress->setMaximum(total);
        m_progress->setValue(done);
        m_progressText->setText(QStringLiteral("%1 / %2 thiết bị").arg(done).arg(total));
    });
    connect(cfg, &services::TrbConfig::finished, this, [this](int ok, int fail) {
        setBusy(false);
        m_fillNewOnRead = false;
        m_progressText->setText(QStringLiteral("Xong: %1 thành công, %2 lỗi").arg(ok).arg(fail));
    });
    selectDevice(0, 0);
}

QWidget *TrbConfigPage::buildContent()
{
    // Cây thiết bị: bấm vào để chọn thiết bị đang xem; đánh dấu ô để đưa vào thao tác hàng loạt.
    m_tree = new QTreeWidget;
    m_tree->setHeaderLabel(QStringLiteral("Thiết bị (đánh dấu = hàng loạt)"));
    m_tree->setFixedWidth(230);
    for (int mb = 0; mb < m_ctx.store->mbCount(); ++mb) {
        auto *parent = new QTreeWidgetItem(m_tree, {QStringLiteral("MB%1").arg(mb)});
        parent->setFlags(parent->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
        parent->setCheckState(0, Qt::Unchecked);
        for (int trb = 0; trb < m_ctx.store->trbPerMb(); ++trb) {
            auto *it = new QTreeWidgetItem(parent, {QStringLiteral("TRB%1").arg(trb)});
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(0, Qt::Unchecked);
            it->setData(0, Qt::UserRole, mb);
            it->setData(0, Qt::UserRole + 1, trb);
        }
    }
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it) {
        if (it && it->parent()) selectDevice(it->data(0, Qt::UserRole).toInt(), it->data(0, Qt::UserRole + 1).toInt());
    });

    const auto &fields = trbcfg::table().fields();
    m_table = new QTableWidget(fields.size(), 4);
    m_table->setHorizontalHeaderLabels({QStringLiteral("Nhóm"), QStringLiteral("Trường"),
                                        QStringLiteral("Trên thiết bị"), QStringLiteral("Giá trị mới")});
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_group = new QComboBox;
    m_group->addItem(QStringLiteral("Tất cả nhóm"));
    m_updating = true;
    for (int r = 0; r < fields.size(); ++r) {
        if (m_group->findText(fields.at(r).group) < 0) m_group->addItem(fields.at(r).group);
        for (int c = 0; c < 4; ++c) {
            auto *it = new QTableWidgetItem(c == ColGroup ? fields.at(r).group : c == ColName ? fields.at(r).name
                                            : c == ColDevice ? QStringLiteral("--") : QStringLiteral("0"));
            if (c != ColNew) it->setFlags(it->flags() & ~Qt::ItemIsEditable);
            m_table->setItem(r, c, it);
        }
    }
    m_updating = false;
    connect(m_group, &QComboBox::currentIndexChanged, this, &TrbConfigPage::applyGroupFilter);
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *it) {
        if (m_updating || it->column() != ColNew) return;
        // Nhận số thập phân hoặc 0x..; sai hoặc vượt độ rộng trường thì trả về 0.
        const int size = trbcfg::table().fields().at(it->row()).size;
        const qulonglong max = size >= 4 ? 0xFFFFFFFFull : (1ull << (8 * size)) - 1;
        bool ok = false;
        const qulonglong v = it->text().trimmed().toULongLong(&ok, 0);
        m_updating = true;
        it->setText(ok && v <= max ? QString::number(v) : QStringLiteral("0"));
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
        m_ctx.trbConfig->readDevices({{m_mb, m_trb}});
    }));
    single->addWidget(button(QStringLiteral("Ghi và kiểm tra"), [this] {
        if (!ready()) return;
        if (!m_hasNew) {
            QMessageBox::information(this, QStringLiteral("Ghi cấu hình"),
                                     QStringLiteral("Chưa có giá trị để ghi. Hãy Đọc từ thiết bị hoặc Mở file trước."));
            return;
        }
        if (QMessageBox::question(this, QStringLiteral("Ghi cấu hình"),
                                  QStringLiteral("Ghi toàn bộ cột \"Giá trị mới\" xuống MB%1 / TRB%2?").arg(m_mb).arg(m_trb))
            != QMessageBox::Yes) return;
        if (!m_ctx.auth->unlock(this)) return; // chỉ hỏi mật khẩu khi thật sự gửi cấu hình xuống thiết bị
        setBusy(true);
        m_ctx.trbConfig->writeFull({m_mb, m_trb}, newValues());
    }));
    single->addWidget(button(QStringLiteral("Mở file…"), [this] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Mở file cấu hình"), {}, "JSON (*.json)");
        QFile f(path);
        if (path.isEmpty() || !f.open(QIODevice::ReadOnly)) return;
        QList<double> v = newValues();
        trbcfg::fromJson(QJsonDocument::fromJson(f.readAll()).object(), v);
        setNewValues(v);
        refreshDiff();
    }));
    single->addWidget(button(QStringLiteral("Lưu file…"), [this] {
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Lưu file cấu hình"),
                                                          QStringLiteral("trb_mb%1_trb%2.json").arg(m_mb).arg(m_trb), "JSON (*.json)");
        QFile f(path);
        if (!path.isEmpty() && f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(QJsonDocument(trbcfg::toJson(newValues())).toJson());
    }));

    auto *batch = new QHBoxLayout;
    m_progress = new QProgressBar;
    m_progress->setMaximum(1);
    m_progress->setValue(0);
    m_progressText = new QLabel;
    m_cancel = new QPushButton(QStringLiteral("Hủy"));
    m_cancel->setEnabled(false);
    connect(m_cancel, &QPushButton::clicked, this, [this] { m_ctx.trbConfig->cancel(); });
    batch->addWidget(new QLabel(QStringLiteral("Hàng loạt:")));
    batch->addWidget(button(QStringLiteral("Đọc các TRB đã đánh dấu"), [this] {
        const QList<Device> devs = checkedDevices();
        if (!ready() || devs.isEmpty()) return;
        setBusy(true);
        m_ctx.trbConfig->readDevices(devs);
    }));
    batch->addWidget(button(QStringLiteral("Ghi toàn bộ cho TRB đã đánh dấu"), [this] {
        const QList<Device> devs = checkedDevices();
        if (!ready()) return;
        if (!m_hasNew) {
            QMessageBox::information(this, QStringLiteral("Cấu hình hàng loạt"),
                QStringLiteral("Chưa có giá trị để ghi. Hãy Đọc từ thiết bị, Mở file trước."));
            return;
        }
        if (devs.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("Cấu hình hàng loạt"), QStringLiteral("Cần đánh dấu ít nhất một TRB."));
            return;
        }
        if (QMessageBox::question(this, QStringLiteral("Cấu hình hàng loạt"),
                QStringLiteral("Ghi TOÀN BỘ cột \"Giá trị mới\" xuống %1 TRB? Cấu hình hiện có của từng TRB bị ghi đè hoàn toàn.")
                    .arg(devs.size())) != QMessageBox::Yes) return;
        if (!m_ctx.auth->unlock(this)) return;
        setBusy(true);
        m_ctx.trbConfig->writeFullMany(devs, newValues());
    }));
    batch->addWidget(button(QStringLiteral("Áp ô đã sửa cho TRB đã đánh dấu"), [this] {
        const QList<Device> devs = checkedDevices();
        const QHash<int, double> changes = editedFields();
        if (!ready()) return;
        if (devs.isEmpty() || changes.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("Cấu hình hàng loạt"),
                QStringLiteral("Cần đánh dấu ít nhất một TRB, và có ít nhất một ô \"Giá trị mới\" khác cột \"Trên thiết bị\" "
                               "(đọc thiết bị đang xem trước, rồi sửa các ô cần đổi)."));
            return;
        }
        if (QMessageBox::question(this, QStringLiteral("Cấu hình hàng loạt"),
                QStringLiteral("Ghi %1 trường đã sửa xuống %2 TRB? Các trường khác của từng TRB được giữ nguyên.")
                    .arg(changes.size()).arg(devs.size())) != QMessageBox::Yes) return;
        if (!m_ctx.auth->unlock(this)) return;
        setBusy(true);
        m_ctx.trbConfig->applyChanges(devs, changes);
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

    auto *right = new QVBoxLayout;
    right->addLayout(single);
    right->addWidget(m_table, 1);
    right->addLayout(batch);
    right->addLayout(progressRow);

    auto *w = new QWidget;
    auto *l = new QHBoxLayout(w);
    l->addWidget(m_tree);
    l->addLayout(right, 1);
    return w;
}

bool TrbConfigPage::ready()
{
    m_ctx.auth->touch();
    return !m_ctx.serviceBusy();
}

void TrbConfigPage::setBusy(bool busy)
{
    for (QPushButton *b : m_actions) b->setEnabled(!busy);
    m_tree->setEnabled(!busy);
    m_cancel->setEnabled(busy);
}

void TrbConfigPage::selectDevice(int mb, int trb)
{
    m_mb = mb;
    m_trb = trb;
    m_title->setText(QStringLiteral("<b>MB%1 / TRB%2</b>").arg(mb).arg(trb));
    showDeviceColumn();
    refreshDiff();
}

void TrbConfigPage::showDeviceColumn()
{
    const QList<double> *dev = m_ctx.trbConfig->cached(m_mb, m_trb);
    m_updating = true;
    for (int r = 0; r < m_table->rowCount(); ++r)
        m_table->item(r, ColDevice)->setText(dev ? fmt(dev->at(r)) : QStringLiteral("--"));
    m_updating = false;
}

void TrbConfigPage::setNewValues(const QList<double> &values)
{
    m_updating = true;
    for (int r = 0; r < m_table->rowCount(); ++r) m_table->item(r, ColNew)->setText(fmt(values.at(r)));
    m_updating = false;
    m_hasNew = true;
}

QList<double> TrbConfigPage::newValues() const
{
    QList<double> v;
    for (int r = 0; r < m_table->rowCount(); ++r) v << m_table->item(r, ColNew)->text().toDouble();
    return v;
}

QList<TrbConfigPage::Device> TrbConfigPage::checkedDevices() const
{
    QList<Device> out;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        const QTreeWidgetItem *mb = m_tree->topLevelItem(i);
        for (int j = 0; j < mb->childCount(); ++j)
            if (mb->child(j)->checkState(0) == Qt::Checked) out.append({i, j});
    }
    return out;
}

QHash<int, double> TrbConfigPage::editedFields() const
{
    QHash<int, double> out;
    const QList<double> *dev = m_ctx.trbConfig->cached(m_mb, m_trb);
    if (!dev || !m_hasNew) return out;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        const double v = m_table->item(r, ColNew)->text().toDouble();
        if (v != dev->at(r)) out.insert(r, v);
    }
    return out;
}

void TrbConfigPage::refreshDiff()
{
    const QHash<int, double> edited = editedFields();
    m_updating = true;
    for (int r = 0; r < m_table->rowCount(); ++r)
        m_table->item(r, ColNew)->setBackground(edited.contains(r) ? QBrush(kDiffColor) : QBrush());
    m_updating = false;
}

void TrbConfigPage::applyGroupFilter()
{
    const QString g = m_group->currentIndex() == 0 ? QString() : m_group->currentText();
    for (int r = 0; r < m_table->rowCount(); ++r)
        m_table->setRowHidden(r, !g.isEmpty() && m_table->item(r, ColGroup)->text() != g);
}

} // namespace ui
