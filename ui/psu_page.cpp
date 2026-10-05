#include "psu_page.h"
#include "overview_grid.h"
#include "status_pill.h"
#include "theme.h"
#include "../proto/psu_monitor_proto.h"
#include <QCheckBox>
#include <QDateTime>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>

namespace ui {

using namespace proto;
using model::Status;

namespace {
const QString kDash = QStringLiteral("--");

QTableWidget *makeTable(int rows, const QStringList &columns, const QStringList &rowLabels)
{
    auto *t = new QTableWidget(rows, columns.size());
    t->setHorizontalHeaderLabels(columns);
    t->setVerticalHeaderLabels(rowLabels);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionMode(QAbstractItemView::NoSelection);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    t->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < columns.size(); ++c) {
            auto *it = new QTableWidgetItem(kDash);
            it->setTextAlignment(Qt::AlignCenter);
            t->setItem(r, c, it);
        }
    return t;
}
}

PsuPage::PsuPage(const AppContext &ctx, QWidget *parent)
    : QWidget(parent), m_ctx(ctx), m_addr(ctx.psuStore->firstAddr())
{
    auto *head = new QHBoxLayout;
    for (int i = 0; i < ctx.psuStore->count(); ++i) {
        const int addr = ctx.psuStore->firstAddr() + i;
        auto *b = new QPushButton(QStringLiteral("PSU %1").arg(addr));
        connect(b, &QPushButton::clicked, this, [this, addr] { setDevice(addr); });
        m_selectors << b;
        head->addWidget(b);
    }
    m_pill = new StatusPill;
    m_status = new QLabel;
    theme::setRole(m_status, "muted");
    head->addSpacing(12);
    head->addWidget(m_pill);
    head->addWidget(m_status, 1);

    auto *body = new QHBoxLayout;
    body->addWidget(buildMonitor(), 1);
    body->addWidget(buildControl());

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(theme::kSpace);
    layout->addLayout(head);
    layout->addLayout(body, 1);
    refresh();
}

QWidget *PsuPage::buildMonitor()
{
    auto titled = [](const QString &title, QWidget *content) {
        auto *g = new QGroupBox(title);
        auto *l = new QVBoxLayout(g);
        l->addWidget(content);
        return g;
    };

    QStringList clusterRows;
    for (int f = 0; f < psumon::kClusterFields; ++f) clusterRows << psumon::clusterFieldLabel(f);
    QStringList clusters;
    for (int c = 1; c <= psumon::kNumCluster; ++c) clusters << QStringLiteral("Cụm %1").arg(c);
    m_cluster = makeTable(psumon::kClusterFields, clusters, clusterRows);

    QStringList supplyRows;
    for (int i = 0; i < psumon::kNumSupply; ++i) supplyRows << psumon::supplyLabel(i);
    m_supply = makeTable(psumon::kNumSupply, {QStringLiteral("Giá trị thô")}, supplyRows);

    auto valueRow = [](int n, QList<QLabel *> &out, const QStringList &prefixes) {
        auto *w = new QWidget;
        auto *l = new QHBoxLayout(w);
        l->setContentsMargins(0, 0, 0, 0);
        for (int i = 0; i < n; ++i) {
            if (!prefixes.isEmpty()) l->addWidget(new QLabel(prefixes.at(i)));
            auto *v = new QLabel(kDash);
            v->setMinimumWidth(26);
            v->setAlignment(Qt::AlignCenter);
            v->setFrameShape(QFrame::StyledPanel);
            out << v;
            l->addWidget(v);
        }
        l->addStretch(1);
        return w;
    };
    QStringList rtcNames;
    for (int i = 0; i < psumon::kNumRtc; ++i) rtcNames << psumon::rtcLabel(i);

    auto *flags = new QGroupBox(QStringLiteral("Thời gian và trip"));
    auto *form = new QFormLayout(flags);
    form->addRow(QStringLiteral("RTC (thô)"), valueRow(psumon::kNumRtc, m_rtc, rtcNames));
    form->addRow(QStringLiteral("Trip code 1–10 (hex)"), valueRow(psumon::kNumTrip, m_trip, {}));

    auto *tables = new QHBoxLayout;
    tables->addWidget(titled(QStringLiteral("4 cụm DCM"), m_cluster), 3);
    tables->addWidget(titled(QStringLiteral("Nguồn phụ (Supply)"), m_supply), 1);

    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->addLayout(tables, 1);
    l->addWidget(flags);
    return w;
}

QWidget *PsuPage::buildControl()
{
    auto *ctl = new QGroupBox(QStringLiteral("Điều khiển"));
    auto *form = new QFormLayout(ctl);
    auto *note = new QLabel(QStringLiteral("Mỗi lệnh ghi đè trạng thái cả 4 cụm: ô được chọn = bật, bỏ chọn = tắt. "
                                           "Bản tin giám sát không báo trạng thái bật/tắt nên không tự điền sẵn."));
    note->setWordWrap(true);
    form->addRow(note);
    for (int i = 0; i < 4; ++i) {
        m_enable[i] = new QCheckBox(QStringLiteral("Cụm %1 bật").arg(i + 1));
        form->addRow(m_enable[i]);
    }
    m_clearTrip = new QCheckBox(QStringLiteral("Clear trip (PSU xóa hết trip code)"));
    form->addRow(m_clearTrip);
    auto *send = new QPushButton(QStringLiteral("Gửi lệnh điều khiển"));
    form->addRow(send);
    connect(send, &QPushButton::clicked, this, &PsuPage::sendControl);

    theme::addShadow(ctl);   // thẻ tĩnh
    auto *w = new QWidget;
    w->setFixedWidth(320);
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(4, 4, 4, 12);   // chừa chỗ cho bóng đổ
    l->addWidget(ctl);
    l->addStretch(1);
    return w;
}

void PsuPage::setDevice(int addr)
{
    if (!m_ctx.psuStore->contains(addr)) return;
    m_addr = addr;
    refresh();
}

void PsuPage::refresh()
{
    const model::PsuStore *store = m_ctx.psuStore;
    for (int i = 0; i < m_selectors.size(); ++i) {
        const int addr = store->firstAddr() + i;
        const Status st = store->psu(addr).status;
        QString style = QStringLiteral("background:%1; color:%2;")
                            .arg(OverviewGrid::statusColor(st).name(),
                                 st == Status::NoData ? QStringLiteral("#444441") : QStringLiteral("#ffffff"));
        if (addr == m_addr) style += QStringLiteral(" border:3px solid #185FA5; font-weight:bold;");
        m_selectors[i]->setStyleSheet(style);
    }

    const model::PsuState &s = store->psu(m_addr);
    const bool has = s.frames > 0;
    m_pill->setPill(QStringLiteral("PSU %1: %2%3").arg(m_addr).arg(theme::statusMark(s.status) + (theme::statusMark(s.status).isEmpty() ? QString() : QStringLiteral(" ")),
                                                      statusText(s.status)),
                    OverviewGrid::statusColor(s.status), theme::statusTextColor(s.status));
    QString text;
    if (has)
        text = QStringLiteral("Cập nhật %1 s trước · %2 bản tin")
                   .arg((QDateTime::currentMSecsSinceEpoch() - s.lastSeenMs) / 1000.0, 0, 'f', 1).arg(s.frames);
    if (s.status == Status::Lost) text += QStringLiteral(" · SỐ LIỆU BÊN DƯỚI LÀ SỐ LIỆU CŨ");
    m_status->setText(text);

    auto val = [&](int idx) { return has ? QString::number(s.values.at(idx)) : kDash; };
    auto limitText = [&](int field) {
        const model::Limit l = m_ctx.psuThresholds->get(m_addr, 0, field);
        return l.isSet() ? QStringLiteral("Ngưỡng: %1 … %2").arg(l.min).arg(l.max)
                         : QStringLiteral("Không có ngưỡng (chỉ hiển thị)");
    };
    auto fill = [&](QTableWidgetItem *it, int field) {
        it->setText(val(field));
        it->setToolTip(limitText(field));
        const bool bad = s.alarms.contains(field);
        it->setBackground(bad ? QBrush(QColor(0xEF9F27)) : QBrush());
        QFont font = it->font();
        font.setBold(bad);
        it->setFont(font);
    };
    for (int c = 0; c < psumon::kNumCluster; ++c)
        for (int f = 0; f < psumon::kClusterFields; ++f)
            fill(m_cluster->item(f, c), psumon::clusterField(c, psumon::ClusterField(f)));
    for (int i = 0; i < psumon::kNumSupply; ++i) fill(m_supply->item(i, 0), psumon::kIdxSupply0 + i);

    for (int i = 0; i < m_rtc.size(); ++i) m_rtc[i]->setText(val(psumon::kIdxRtc0 + i));
    for (int i = 0; i < m_trip.size(); ++i) {
        const int code = has ? int(s.values.at(psumon::kIdxTrip0 + i)) : 0;
        m_trip[i]->setText(has ? QStringLiteral("%1").arg(code, 2, 16, QLatin1Char('0')).toUpper() : kDash);
        m_trip[i]->setStyleSheet(code ? QStringLiteral("background:#E24B4A; color:white; font-weight:bold;") : QString());
    }
}

void PsuPage::sendControl()
{
    psuctl::ControlCmd c;
    QStringList on, off;
    for (int i = 0; i < 4; ++i) {
        c.clusterMask |= m_enable[i]->isChecked() << i;
        (m_enable[i]->isChecked() ? on : off) << QString::number(i + 1);
    }
    c.clearTrip = m_clearTrip->isChecked();

    // PSU là nguồn công suất và lệnh ghi đè cả 4 cụm, nên luôn cho người vận hành xem lại trước khi gửi.
    const QString none = QStringLiteral("không");
    QString text = QStringLiteral("Gửi tới PSU %1:\n  • BẬT cụm: %2\n  • TẮT cụm: %3")
                       .arg(m_addr).arg(on.isEmpty() ? none : on.join(", "), off.isEmpty() ? none : off.join(", "));
    if (c.clearTrip) text += QStringLiteral("\n  • Clear trip");
    if (QMessageBox::question(this, QStringLiteral("Điều khiển PSU"), text) != QMessageBox::Yes) return;

    m_ctx.psuControl->sendControl(m_addr, c);
    m_clearTrip->setChecked(false); // lệnh một lần
}

} // namespace ui
