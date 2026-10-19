#include "psu_page.h"
#include "bit_cells.h"
#include "overview_grid.h"
#include "status_pill.h"
#include "theme.h"
#include "time_text.h"
#include "../proto/psu_control_proto.h"
#include "../proto/psu_monitor_proto.h"
#include <QCheckBox>
#include <QDateTime>
#include <QFormLayout>
#include <QGridLayout>
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
    t->setAlternatingRowColors(true);
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

namespace {
QStringList psuTripCaptions()
{
    QStringList l;
    for (int i = 1; i <= psumon::kNumTrip; ++i) l << QStringLiteral("T%1").arg(i);   // tên và ý nghĩa từng bit nằm ở tooltip
    return l;
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
    m_supply = makeTable(psumon::kNumSupply, {QStringLiteral("Giá trị")}, supplyRows);

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
    form->addRow(QStringLiteral("Trip code (bit 0 ... bit 7)"),
                 BitCells::makeGrid(psuTripCaptions(), 5, m_trip));

    for (int i = 0; i < m_trip.size(); ++i) {          // tooltip từng bit theo bảng trip tạm thời của firmware
        QStringList bits;
        for (int b = 0; b < 8; ++b) bits << psumon::tripBitName(i, b);
        m_trip[i]->setBitNames(bits);
        const QString n = psumon::tripName(i);
        m_trip[i]->setTooltipTitle(n == QStringLiteral("Dự phòng") ? QStringLiteral("Trip %1").arg(i + 1) : QStringLiteral("Trip %1: %2").arg(i + 1).arg(n));
    }

    // Thẻ trạng thái firmware (vùng mở rộng của bản tin giám sát), một hàng gọn để không chiếm chỗ của bảng 4 cụm DCM.
    auto *fw = new QWidget;
    auto *fl = new QHBoxLayout(fw);
    fl->setContentsMargins(0, 0, 0, 0);
    const char *fwNames[] = {"Trạng thái an toàn", "PWR_EN", "Trip liên tiếp", "Slot", "Version"};
    for (int i = 0; i < 5; ++i) {
        auto *name = new QLabel(QString::fromUtf8(fwNames[i]));
        theme::setRole(name, "muted");
        auto *value = new QLabel(kDash);
        value->setAlignment(Qt::AlignCenter);
        value->setFrameShape(QFrame::StyledPanel);
        value->setMinimumWidth(i == 4 ? 90 : 60);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_fw << value;
        fl->addWidget(name);
        fl->addWidget(value);
        fl->addSpacing(8);
    }
    fl->addStretch(1);

    auto *tables = new QHBoxLayout;
    tables->addWidget(titled(QStringLiteral("4 cụm DCM"), m_cluster), 3);
    tables->addWidget(titled(QStringLiteral("Nguồn phụ (Supply, đã dịch phải 4 bit)"), m_supply), 1);

    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(fw);
    l->addLayout(tables, 1);
    l->addWidget(flags);
    return w;
}

QWidget *PsuPage::buildControl()
{
    auto *ctl = new QGroupBox(QStringLiteral("Điều khiển"));
    auto *form = new QFormLayout(ctl);
    for (int c = 0; c < 4; ++c) {
        m_cluster4 << new QCheckBox(QStringLiteral("Bật cụm %1").arg(c + 1));
        form->addRow(m_cluster4.last());
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
    // Điền sẵn ô bật nguồn theo trạng thái PWR_EN firmware báo (cả 4 cụm chung một chân), để lệnh "Clear trip" không vô tình tắt nguồn đang bật.
    const model::PsuState &st = m_ctx.psuStore->psu(addr);
    if (st.frames > 0) {
        const bool pwr = int(st.values.at(psumon::kIdxPwrFlags)) & 1;
        for (QCheckBox *c : m_cluster4) c->setChecked(pwr);
    }
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
        text = QStringLiteral("Cập nhật %1 trước · %2 bản tin")
                   .arg(elapsedText(QDateTime::currentMSecsSinceEpoch() - s.lastSeenMs)).arg(s.frames);
    if (has) text += QStringLiteral(" · An toàn: %1").arg(psumon::safeStateText(int(s.values.at(psumon::kIdxSafeState))));
    if (s.status == Status::Lost) text += QStringLiteral(" · SỐ LIỆU BÊN DƯỚI LÀ SỐ LIỆU CŨ");
    m_status->setText(text);

    auto num = [](double v) { return QString::number(v, 'g', 10); };   // giá trị thô 24 bit không được hiện dạng 1.2e+06
    auto val = [&](int idx) { return has ? num(s.values.at(idx)) : kDash; };
    auto limitText = [&](int field) {
        const model::Limit l = m_ctx.psuThresholds->get(m_addr, 0, field);
        return l.isSet() ? QStringLiteral("Ngưỡng: %1 … %2").arg(num(l.min), num(l.max))
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
    for (int i = 0; i < psumon::kNumSupply; ++i) {
        // Supply là thanh ghi INA228 (dữ liệu ở bit 23..4): hiện đã dịch phải 4 bit, dòng có dấu; tooltip giữ số thô và ngưỡng thô.
        QTableWidgetItem *it = m_supply->item(i, 0);
        const int field = psumon::kIdxSupply0 + i;
        fill(it, field);
        if (has) {
            it->setText(num(psumon::supplyShown(s.values.at(field))));
            it->setToolTip(QStringLiteral("Thô: %1 (dịch phải 4 bit để hiện)\n%2").arg(num(s.values.at(field)), limitText(field)));
        }
    }

    for (int i = 0; i < m_rtc.size(); ++i) m_rtc[i]->setText(val(psumon::kIdxRtc0 + i));
    if (has) {
        const int st = int(s.values.at(psumon::kIdxSafeState)), pf = int(s.values.at(psumon::kIdxPwrFlags));
        const int slot = int(s.values.at(psumon::kIdxSlot));
        m_fw[0]->setText(psumon::safeStateText(st));
        m_fw[1]->setText(pf & 1 ? QStringLiteral("Bật") : QStringLiteral("Tắt"));
        m_fw[1]->setToolTip(QStringLiteral("PC đang yêu cầu: %1").arg(pf & 2 ? QStringLiteral("bật") : QStringLiteral("tắt")));
        m_fw[2]->setText(num(s.values.at(psumon::kIdxTripCount)));
        m_fw[3]->setText(slot == 'A' || slot == 'B' || slot == 'R' ? QString(QChar(slot)) : QStringLiteral("?"));
        m_fw[4]->setText(num(s.values.at(psumon::kIdxVersion)));
    } else {
        for (QLabel *l : m_fw) { l->setText(kDash); l->setToolTip({}); }
    }
    for (int i = 0; i < m_trip.size(); ++i)
        m_trip[i]->setValue(has ? int(s.values.at(psumon::kIdxTrip0 + i)) : 0, has);
}

void PsuPage::sendControl()
{
    const model::PsuState &st = m_ctx.psuStore->psu(m_addr);
    const bool known = st.frames > 0;
    const int safe = known ? int(st.values.at(psumon::kIdxSafeState)) : -1;
    quint8 mask = 0;
    for (int c = 0; c < m_cluster4.size(); ++c) if (m_cluster4.at(c)->isChecked()) mask |= quint8(1 << c);
    const bool on = mask != 0, clear = m_clearTrip->isChecked();

    // Firmware APSU: LOCKOUT bỏ qua mọi lệnh; TRIPPED từ chối lệnh bật cho tới khi Clear trip (và lệnh bật phải ở khung sau Clear trip).
    if (safe == psumon::SafeLockout) {
        QMessageBox::warning(this, QStringLiteral("Điều khiển PSU"),
                             QStringLiteral("PSU %1 đang ở LOCKOUT (trip liên tiếp từ 3 lần): mọi lệnh bị bỏ qua, chỉ reset phần cứng mới thoát.").arg(m_addr));
        return;
    }
    const bool twoStep = safe == psumon::SafeTripped && clear && on;
    if (safe == psumon::SafeTripped && on && !clear) {
        QMessageBox::warning(this, QStringLiteral("Điều khiển PSU"),
                             QStringLiteral("PSU %1 đang TRIPPED: lệnh bật bị từ chối cho tới khi Clear trip. Hãy chọn Clear trip rồi gửi lại.").arg(m_addr));
        return;
    }

    // PSU là nguồn công suất nên luôn cho người vận hành xem lại trước khi gửi.
    QString text = QStringLiteral("Gửi tới PSU %1:\n  • %2").arg(m_addr).arg(on ? QStringLiteral("BẬT nguồn (mask %1)").arg(mask, 4, 2, QLatin1Char('0')) : QStringLiteral("TẮT nguồn"));
    if (clear) text += QStringLiteral("\n  • Clear trip");
    if (twoStep) text += QStringLiteral("\n\nPSU đang TRIPPED: app gửi Clear trip trước, rồi gửi lệnh bật ở khung sau.");
    if (QMessageBox::question(this, QStringLiteral("Điều khiển PSU"), text) != QMessageBox::Yes) return;

    if (twoStep) {
        m_ctx.psuControl->sendClearTripThenOn(m_addr, mask);
    } else {
        psuctl::ControlCmd c;
        c.clusterMask = mask;
        c.clearTrip = clear;
        m_ctx.psuControl->sendControl(m_addr, c);
    }
    m_clearTrip->setChecked(false); // lệnh một lần
}

} // namespace ui
