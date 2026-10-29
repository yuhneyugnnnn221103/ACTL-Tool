#include "log_viewer_page.h"
#include "plot_widget.h"
#include "theme.h"
#include "../proto/psu_monitor_proto.h"
#include "../proto/trb_monitor_proto.h"
#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace ui {

using model::Status;

namespace {
constexpr int kMaxPanels = 12, kMaxLinesPerPanel = 16;
const QColor kPalette[] = {QColor(0x18, 0x5F, 0xA5), QColor(0xD8, 0x5A, 0x30), QColor(0x3B, 0x6D, 0x11), QColor(0x7F, 0x77, 0xDD), QColor(0xBA, 0x75, 0x17),
                           QColor(0x0F, 0x6E, 0x56), QColor(0xD4, 0x53, 0x7E), QColor(0x5F, 0x5E, 0x5A), QColor(0xA3, 0x2D, 0x2D), QColor(0x2B, 0x9F, 0xC4)};

enum Role { KeyRole = Qt::UserRole + 1, KindRole, NameRole };

QString shortStamp(qint64 t) { return QDateTime::fromMSecsSinceEpoch(t).toString(QStringLiteral("dd/MM HH:mm:ss")); }
QString stamp(qint64 t) { return QDateTime::fromMSecsSinceEpoch(t).toString(QStringLiteral("dd/MM/yyyy HH:mm:ss.zzz")); }
const proto::FieldTable &tableOf(int kind) { return kind == logfile::KindTrb ? proto::trbmon::table() : proto::psumon::table(); }
}

LogViewerPage::LogViewerPage(const AppContext &ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    // --- Cột trái: file, thiết bị, trường.
    auto *open = new QPushButton(QStringLiteral("Mở file log…"));
    auto *remove = new QPushButton(QStringLiteral("Bỏ file"));
    auto *clear = new QPushButton(QStringLiteral("Bỏ tất cả"));
    theme::setRole(remove, "secondary");
    theme::setRole(clear, "secondary");
    m_fileList = new QListWidget;
    m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileList->setMaximumHeight(120);
    m_fileList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_fileList->setWordWrap(true);
    m_fileList->setToolTip(QStringLiteral("Có thể mở nhiều file: các file được ghép lên cùng một trục thời gian"));
    auto *fileButtons = new QHBoxLayout;
    fileButtons->addWidget(open);
    fileButtons->addWidget(remove);
    fileButtons->addWidget(clear);

    m_devices = new QTreeWidget;
    m_devices->setHeaderHidden(true);
    m_devices->setToolTip(QStringLiteral("Tích thiết bị cần vẽ (mỗi thiết bị một đường trong mỗi đồ thị)"));

    m_fieldFilter = new QLineEdit;
    m_fieldFilter->setPlaceholderText(QStringLiteral("Lọc trường (ví dụ TRM1, I SEN, Supply)…"));
    m_fieldFilter->setClearButtonEnabled(true);
    m_fields = new QListWidget;
    m_fields->setToolTip(QStringLiteral("Tích trường cần vẽ: mỗi trường một đồ thị, các thiết bị đã chọn vẽ song song"));

    auto *left = new QWidget;
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->addLayout(fileButtons);
    ll->addWidget(m_fileList);
    ll->addWidget(new QLabel(QStringLiteral("Thiết bị")));
    ll->addWidget(m_devices, 3);
    ll->addWidget(new QLabel(QStringLiteral("Trường")));
    ll->addWidget(m_fieldFilter);
    ll->addWidget(m_fields, 4);
    left->setMinimumWidth(260);

    // --- Cột phải: đồ thị (cuộn) + bảng sự kiện.
    m_plotHolder = new QWidget;
    m_plotLayout = new QVBoxLayout(m_plotHolder);
    m_plotLayout->setContentsMargins(0, 0, 0, 0);
    m_plotLayout->setSpacing(6);
    m_emptyHint = new QLabel(QStringLiteral("Mở một hoặc nhiều file log (.alog), rồi tích thiết bị và trường để vẽ đồ thị.\n"
                                            "Lăn chuột trên đồ thị để phóng to/thu nhỏ trục thời gian, kéo để dịch, nhấp đúp để xem toàn bộ."));
    m_emptyHint->setAlignment(Qt::AlignCenter);
    theme::setRole(m_emptyHint, "muted");
    m_plotLayout->addWidget(m_emptyHint);
    m_plotLayout->addStretch(1);
    auto *plotScroll = new QScrollArea;
    plotScroll->setWidgetResizable(true);
    plotScroll->setFrameShape(QFrame::NoFrame);
    plotScroll->setWidget(m_plotHolder);

    m_info = new QLabel;
    theme::setRole(m_info, "muted");
    m_onlySelected = new QCheckBox(QStringLiteral("Chỉ thiết bị đã chọn"));
    auto *eventHead = new QHBoxLayout;
    eventHead->addWidget(new QLabel(QStringLiteral("Sự kiện và thời điểm trip (bấm một dòng để tới thời điểm đó)")));
    eventHead->addStretch(1);
    eventHead->addWidget(m_onlySelected);
    m_events = new QTableWidget(0, 4);
    m_events->setHorizontalHeaderLabels({QStringLiteral("Thời điểm"), QStringLiteral("Thiết bị"), QStringLiteral("Loại"), QStringLiteral("Chi tiết")});
    m_events->horizontalHeader()->setStretchLastSection(true);
    m_events->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_events->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_events->setSelectionMode(QAbstractItemView::SingleSelection);
    m_events->verticalHeader()->setVisible(false);
    auto *bottom = new QWidget;
    auto *bl = new QVBoxLayout(bottom);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->addLayout(eventHead);
    bl->addWidget(m_events, 1);

    auto *rightSplit = new QSplitter(Qt::Vertical);
    rightSplit->addWidget(plotScroll);
    rightSplit->addWidget(bottom);
    rightSplit->setStretchFactor(0, 3);
    rightSplit->setStretchFactor(1, 1);
    rightSplit->setSizes({560, 200});
    auto *right = new QWidget;
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->addWidget(m_info);
    rl->addWidget(rightSplit, 1);

    auto *split = new QSplitter;
    split->addWidget(left);
    split->addWidget(right);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({300, 1000});
    auto *l = new QVBoxLayout(this);
    l->addWidget(split, 1);

    // --- Kết nối.
    connect(open, &QPushButton::clicked, this, &LogViewerPage::chooseFiles);
    connect(remove, &QPushButton::clicked, this, &LogViewerPage::removeSelectedFiles);
    connect(clear, &QPushButton::clicked, this, [this] { m_files.clear(); rebuildData(); });
    connect(m_devices, &QTreeWidget::itemChanged, this, [this] { schedulePlots(); rebuildEvents(); });
    connect(m_fields, &QListWidget::itemChanged, this, [this] { schedulePlots(); });
    connect(m_fieldFilter, &QLineEdit::textChanged, this, [this](const QString &f) {
        for (int i = 0; i < m_fields->count(); ++i) m_fields->item(i)->setHidden(!f.isEmpty() && !m_fields->item(i)->text().contains(f, Qt::CaseInsensitive));
    });
    connect(m_onlySelected, &QCheckBox::toggled, this, [this] { rebuildEvents(); });
    connect(m_events, &QTableWidget::cellClicked, this, [this](int row, int) {
        const QTableWidgetItem *it = m_events->item(row, 0);
        if (it) centerOn(it->data(Qt::UserRole).toLongLong());
    });

    // Danh sách trường: TRB rồi PSU, giữ thứ tự trong bảng trường.
    for (int kind : {logfile::KindTrb, logfile::KindPsu}) {
        for (const proto::Field &f : tableOf(kind).fields()) {
            auto *it = new QListWidgetItem(QStringLiteral("%1 · %2").arg(kind == logfile::KindTrb ? QStringLiteral("TRB") : QStringLiteral("PSU"), f.name));
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(Qt::Unchecked);
            it->setData(KindRole, kind);
            it->setData(NameRole, f.name);
            m_fields->addItem(it);
        }
    }
    rebuildData();
}

void LogViewerPage::chooseFiles()
{
    const QString dir = m_ctx.logger ? m_ctx.logger->dir() : m_ctx.settings.logDir;
    const QStringList paths = QFileDialog::getOpenFileNames(this, QStringLiteral("Mở file log"), dir, QStringLiteral("Log ACTL (*.alog);;Tất cả (*)"));
    if (!paths.isEmpty()) openFiles(paths);
}

int LogViewerPage::openFiles(const QStringList &paths)
{
    int ok = 0;
    QStringList errors;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (const QString &path : paths) {
        bool dup = false;
        for (const LoadedFile &f : m_files) dup = dup || QFileInfo(f.path) == QFileInfo(path);
        if (dup) continue;
        LoadedFile f;
        f.path = path;
        QString error;
        if (logfile::load(path, f.data, &error)) { m_files.append(f); ++ok; }
        else errors << QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), error);
    }
    QApplication::restoreOverrideCursor();
    if (ok) rebuildData();
    if (!errors.isEmpty()) QMessageBox::warning(this, QStringLiteral("Mở file log"), errors.join('\n'));
    return ok;
}

void LogViewerPage::removeSelectedFiles()
{
    QList<int> rows;
    for (QListWidgetItem *it : m_fileList->selectedItems()) rows << m_fileList->row(it);
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int r : rows) m_files.removeAt(r);
    if (!rows.isEmpty()) rebuildData();
}

// Ghép mọi file đã mở, dựng lại cây thiết bị, danh sách trip và các đồ thị.
void LogViewerPage::rebuildData()
{
    m_data = logfile::Data();
    for (const LoadedFile &f : m_files) logfile::merge(m_data, f.data);

    m_fileList->clear();
    for (const LoadedFile &f : m_files) {
        int samples = 0;
        for (const logfile::Series &s : f.data.series) samples += s.count();
        auto *it = new QListWidgetItem(QStringLiteral("%1\n%2 → %3 · %4 mẫu").arg(QFileInfo(f.path).fileName(), shortStamp(f.data.startMs), shortStamp(f.data.endMs)).arg(samples));
        it->setToolTip(QStringLiteral("%1\n%2 → %3").arg(f.path, stamp(f.data.startMs), stamp(f.data.endMs)));
        if (!f.data.warnings.isEmpty()) it->setToolTip(it->toolTip() + QStringLiteral("\n") + f.data.warnings.join('\n'));
        m_fileList->addItem(it);
    }

    // Mọi lần đổi trạng thái của từng thiết bị.
    m_trips.clear();
    for (auto it = m_data.series.cbegin(); it != m_data.series.cend(); ++it) {
        const logfile::Series &s = it.value();
        int last = -1;
        for (int i = 0; i < s.count(); ++i) {
            const int st = quint8(s.status.at(i));
            if (st != last) { if (last >= 0 || st == int(Status::Trip) || st == int(Status::Warning)) m_trips.append({s.t.at(i), it.key(), st}); last = st; }
        }
    }
    std::stable_sort(m_trips.begin(), m_trips.end(), [](const Trip &a, const Trip &b) { return a.t < b.t; });

    rebuildDeviceTree();
    fitAll();
    rebuildEvents();
    rebuildPlots();

    qint64 samples = 0;
    for (const logfile::Series &s : m_data.series) samples += s.count();
    QStringList warnings = m_data.warnings;
    m_info->setText(m_files.isEmpty() ? QStringLiteral("Chưa mở file log")
                                      : QStringLiteral("%1 file · %2 → %3 · %4 thiết bị · %5 mẫu%6").arg(m_files.size()).arg(stamp(m_data.startMs), stamp(m_data.endMs))
                                            .arg(m_data.series.size()).arg(samples)
                                            .arg(warnings.isEmpty() ? QString() : QStringLiteral(" · CẢNH BÁO: ") + warnings.join(QStringLiteral("; "))));
}

void LogViewerPage::rebuildDeviceTree()
{
    QSet<int> checked;                                   // giữ lựa chọn cũ khi thêm/bớt file
    for (int top = 0; top < m_devices->topLevelItemCount(); ++top) {
        QTreeWidgetItem *t = m_devices->topLevelItem(top);
        for (int a = 0; a < t->childCount(); ++a) {
            QTreeWidgetItem *c = t->child(a);
            if (c->data(0, KeyRole).isValid()) { if (c->checkState(0) == Qt::Checked) checked.insert(c->data(0, KeyRole).toInt()); continue; }
            for (int b = 0; b < c->childCount(); ++b)
                if (c->child(b)->checkState(0) == Qt::Checked) checked.insert(c->child(b)->data(0, KeyRole).toInt());
        }
    }
    QSignalBlocker block(m_devices);
    m_devices->clear();
    QTreeWidgetItem *trbRoot = nullptr, *psuRoot = nullptr;
    QMap<int, QTreeWidgetItem *> mbItems;
    for (auto it = m_data.series.cbegin(); it != m_data.series.cend(); ++it) {
        const int key = it.key();
        auto *leaf = new QTreeWidgetItem;
        leaf->setData(0, KeyRole, key);
        leaf->setFlags(leaf->flags() | Qt::ItemIsUserCheckable);
        leaf->setCheckState(0, checked.contains(key) ? Qt::Checked : Qt::Unchecked);
        leaf->setText(0, QStringLiteral("%1 (%2 mẫu)").arg(logfile::deviceName(key)).arg(it.value().count()));
        if (logfile::keyKind(key) == logfile::KindTrb) {
            if (!trbRoot) trbRoot = new QTreeWidgetItem(m_devices, QStringList{QStringLiteral("TRB")});
            QTreeWidgetItem *&mb = mbItems[logfile::keyA(key)];
            if (!mb) mb = new QTreeWidgetItem(trbRoot, QStringList{QStringLiteral("MB%1").arg(logfile::keyA(key) + 1)});
            mb->addChild(leaf);
        } else {
            if (!psuRoot) psuRoot = new QTreeWidgetItem(m_devices, QStringList{QStringLiteral("PSU")});
            psuRoot->addChild(leaf);
        }
    }
    for (QTreeWidgetItem *root : {trbRoot, psuRoot}) if (root) root->setExpanded(true);
    for (QTreeWidgetItem *mb : mbItems) mb->setExpanded(mbItems.size() <= 2);
}

QList<int> LogViewerPage::checkedDevices(int kind) const
{
    QList<int> out;
    std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *it) {
        if (it->data(0, KeyRole).isValid()) {
            const int key = it->data(0, KeyRole).toInt();
            if (logfile::keyKind(key) == kind && it->checkState(0) == Qt::Checked) out << key;
        }
        for (int i = 0; i < it->childCount(); ++i) walk(it->child(i));
    };
    for (int i = 0; i < m_devices->topLevelItemCount(); ++i) walk(m_devices->topLevelItem(i));
    return out;
}

void LogViewerPage::selectDevice(int key, bool on)
{
    std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *it) {
        if (it->data(0, KeyRole).isValid() && it->data(0, KeyRole).toInt() == key) it->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
        for (int i = 0; i < it->childCount(); ++i) walk(it->child(i));
    };
    for (int i = 0; i < m_devices->topLevelItemCount(); ++i) walk(m_devices->topLevelItem(i));
}

void LogViewerPage::selectField(int kind, const QString &name, bool on)
{
    for (int i = 0; i < m_fields->count(); ++i) {
        QListWidgetItem *it = m_fields->item(i);
        if (it->data(KindRole).toInt() == kind && it->data(NameRole).toString() == name) it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    }
}

void LogViewerPage::schedulePlots()
{
    if (m_plotsPending) return;
    m_plotsPending = true;
    QTimer::singleShot(0, this, [this] { m_plotsPending = false; rebuildPlots(); });
}

void LogViewerPage::fitAll()
{
    if (m_data.series.isEmpty() && m_data.events.isEmpty()) { m_t0 = 0; m_t1 = 1000; return; }
    const qint64 span = qMax<qint64>(1000, m_data.endMs - m_data.startMs);
    m_t0 = m_data.startMs - span / 50;
    m_t1 = m_data.endMs + span / 50;
}

void LogViewerPage::setView(qint64 t0, qint64 t1)
{
    m_t0 = t0;
    m_t1 = t1;
    for (PlotWidget *p : m_panels) p->setRange(t0, t1);
}

void LogViewerPage::centerOn(qint64 t)
{
    const qint64 span = m_t1 - m_t0;
    setView(t - span / 2, t + span / 2);
}

// Mỗi trường được tích là một đồ thị; mỗi thiết bị đã tích của cùng loại là một đường trong đó.
void LogViewerPage::rebuildPlots()
{
    for (PlotWidget *p : m_panels) { m_plotLayout->removeWidget(p); p->deleteLater(); }
    m_panels.clear();

    int skipped = 0;
    for (int i = 0; i < m_fields->count(); ++i) {
        QListWidgetItem *item = m_fields->item(i);
        if (item->checkState() != Qt::Checked) continue;
        const int kind = item->data(KindRole).toInt();
        const QString name = item->data(NameRole).toString();
        const proto::FieldTable &table = tableOf(kind);
        int index = -1;
        for (int k = 0; k < table.size(); ++k) if (table.fields().at(k).name == name) { index = k; break; }
        const QList<int> devices = checkedDevices(kind);
        if (index < 0 || devices.isEmpty()) continue;
        if (m_panels.size() >= kMaxPanels) { ++skipped; continue; }

        QVector<PlotLine> lines;
        QVector<PlotMarker> markers;
        int n = 0;
        for (int key : devices) {
            if (n >= kMaxLinesPerPanel) break;
            const logfile::Series &s = m_data.series.value(key);
            PlotLine line;
            line.name = logfile::deviceName(key);
            line.color = kPalette[n % (sizeof(kPalette) / sizeof(kPalette[0]))];
            line.t = s.t;
            line.v.reserve(s.count());
            for (int j = 0; j < s.count(); ++j) line.v.append(logfile::fieldValue(s, j, table.fields().at(index)));
            lines.append(line);
            for (const Trip &tr : m_trips)                   // vạch đỏ tại mỗi thời điểm trip của thiết bị đang vẽ
                if (tr.key == key && tr.status == int(Status::Trip)) markers.append({tr.t, QColor(0xE2, 0x4B, 0x4A), QStringLiteral("Trip %1").arg(logfile::deviceName(key))});
            ++n;
        }
        auto *plot = new PlotWidget;
        const proto::Field &f = table.fields().at(index);
        plot->setTitle(QStringLiteral("%1 · %2%3").arg(kind == logfile::KindTrb ? QStringLiteral("TRB") : QStringLiteral("PSU"), name, f.unit.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(f.unit)));
        plot->setLines(lines);
        plot->setMarkers(markers);
        plot->setRange(m_t0, m_t1);
        connect(plot, &PlotWidget::rangeChanged, this, [this](qint64 a, qint64 b) { setView(a, b); });
        connect(plot, &PlotWidget::resetRequested, this, [this] { fitAll(); setView(m_t0, m_t1); });
        connect(plot, &PlotWidget::cursorChanged, this, [this](qint64 t) { for (PlotWidget *p : m_panels) p->setCursorTime(t); });
        m_plotLayout->insertWidget(m_plotLayout->count() - 1, plot);
        m_panels.append(plot);
    }
    m_emptyHint->setVisible(m_panels.isEmpty());
    if (skipped) m_info->setText(m_info->text() + QStringLiteral(" · chỉ vẽ %1 đồ thị đầu, bỏ %2 trường").arg(kMaxPanels).arg(skipped));
}

void LogViewerPage::rebuildEvents()
{
    QSet<int> selected;
    for (int kind : {logfile::KindTrb, logfile::KindPsu})
        for (int k : checkedDevices(kind)) selected.insert(k);
    struct Row { qint64 t; QString device, kind, text; QColor color; };
    QList<Row> rows;
    for (const Trip &tr : m_trips) {
        if (m_onlySelected->isChecked() && !selected.contains(tr.key)) continue;
        QString kind;
        QColor color;
        if (tr.status == int(Status::Trip)) { kind = QStringLiteral("TRIP"); color = QColor(0xE2, 0x4B, 0x4A); }
        else if (tr.status == int(Status::Warning)) { kind = QStringLiteral("Quá ngưỡng"); color = QColor(0xBA, 0x75, 0x17); }
        else if (tr.status == int(Status::Ok)) { kind = QStringLiteral("Tốt"); color = QColor(0x3B, 0x6D, 0x11); }
        else { kind = model::statusText(Status(tr.status)); }
        rows.append({tr.t, logfile::deviceName(tr.key), kind, QStringLiteral("Trạng thái chuyển sang %1").arg(model::statusText(Status(tr.status))), color});
    }
    if (!m_onlySelected->isChecked() || selected.isEmpty())
        for (const logfile::Event &e : m_data.events) rows.append({e.t, QString(), QStringLiteral("Sự kiện"), e.text, QColor()});
    std::stable_sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) { return a.t < b.t; });

    m_events->setRowCount(0);
    m_events->setRowCount(qMin(rows.size(), 20000));
    for (int r = 0; r < m_events->rowCount(); ++r) {
        const Row &row = rows.at(r);
        auto *tm = new QTableWidgetItem(stamp(row.t));
        tm->setData(Qt::UserRole, row.t);
        m_events->setItem(r, 0, tm);
        m_events->setItem(r, 1, new QTableWidgetItem(row.device));
        auto *kind = new QTableWidgetItem(row.kind);
        if (row.color.isValid()) kind->setForeground(row.color);
        m_events->setItem(r, 2, kind);
        m_events->setItem(r, 3, new QTableWidgetItem(row.text));
    }
    m_events->resizeColumnToContents(0);
    m_events->resizeColumnToContents(1);
    m_events->resizeColumnToContents(2);
}

} // namespace ui
