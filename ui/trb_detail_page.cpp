#include "trb_detail_page.h"
#include "bit_cells.h"
#include "led_indicator.h"
#include "overview_grid.h"
#include "status_pill.h"
#include "theme.h"
#include "time_text.h"
#include "../proto/trb_monitor_proto.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDateTime>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTableWidget>

namespace ui {

using namespace proto;
using model::Status;

namespace {
const QString kDash = QStringLiteral("--");
constexpr int kCardHeight = 32, kTableRowHeight = 28;

QStringList numbered(const QString &fmt, int n)
{
    QStringList l;
    for (int i = 1; i <= n; ++i) l << fmt.arg(i);
    return l;
}

// Thẻ đèn LED lớn: tiêu đề + một hàng đèn có nhãn.
QGroupBox *ledGroup(const QString &title, const QStringList &captions, QList<LedIndicator *> &leds)
{
    auto *g = new QGroupBox(title);
    g->setProperty("compact", true);   // đệm trên/dưới nhỏ hơn thẻ thường
    auto *l = new QHBoxLayout(g);
    l->setSpacing(theme::kSpace);
    // Khoảng giãn bằng nhau ở hai lề và giữa các đèn: đèn cách đều, cân hai bên.
    l->addStretch(1);
    for (const QString &c : captions) {
        auto *led = new LedIndicator(c);
        leds << led;
        l->addWidget(led);
        l->addStretch(1);
    }
    return g;
}

QWidget *checkRow(const QStringList &names, QCheckBox **out)
{
    auto *w = new QWidget;
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    for (int i = 0; i < names.size(); ++i) l->addWidget(out[i] = new QCheckBox(names.at(i)));
    l->addStretch(1);
    return w;
}
}

TrbDetailPage::TrbDetailPage(const model::DeviceStore *store, services::TrbControl *control,
                             const model::Thresholds *thresholds, QWidget *parent)
    : QWidget(parent), m_store(store), m_control(control), m_thresholds(thresholds)
{
    m_mbBox = new QComboBox;
    m_trbBox = new QComboBox;
    for (int i = 0; i < store->mbCount(); ++i) m_mbBox->addItem(QStringLiteral("MB%1").arg(i + 1));
    for (int i = 0; i < store->trbPerMb(); ++i) m_trbBox->addItem(QStringLiteral("TRB%1").arg(i + 1));
    auto *prev = new QPushButton(QStringLiteral("◀ Trước"));
    auto *next = new QPushButton(QStringLiteral("Sau ▶"));
    theme::setRole(prev, "secondary");
    theme::setRole(next, "secondary");
    auto *pillBox = new QHBoxLayout;
    pillBox->setSpacing(theme::kSpace);
    for (int i = 0; i < 4; ++i) {      // Trip, Quá ngưỡng, Mất kết nối đồng thời (hoặc Tốt / Chưa có dữ liệu)
        m_pills << new StatusPill;
        pillBox->addWidget(m_pills.last());
    }
    m_debugPill = new StatusPill;
    m_debugPill->hide();
    // Bản tin lỗi: cùng hàng với ô chọn MB/TRB, căn phải; bấm để xem số bản tin từng loại.
    auto *errLabel = new QLabel(QStringLiteral("Bản tin lỗi"));
    theme::setRole(errLabel, "title");
    m_errButton = new QPushButton(kDash);
    theme::setRole(m_errButton, "secondary");
    m_errButton->setToolTip(QStringLiteral("Tổng số bản tin lỗi (cộng %1 loại). Bấm để xem số bản tin từng loại.").arg(trbmon::kNumErr));
    connect(m_errButton, &QPushButton::clicked, this, &TrbDetailPage::showErrors);
    m_status = new QLabel;
    theme::setRole(m_status, "muted");

    auto *head = new QHBoxLayout;
    head->addWidget(m_mbBox);
    head->addWidget(m_trbBox);
    head->addWidget(prev);
    head->addWidget(next);
    head->addSpacing(12);
    head->addLayout(pillBox);
    head->addWidget(m_status, 1);
    head->addWidget(errLabel);
    head->addWidget(m_errButton);
    head->addWidget(m_debugPill);

    auto *body = new QHBoxLayout;
    // Cửa sổ thấp thì cuộn thay vì ép các bảng thấp lại làm mất dòng cuối.
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(buildMonitor());
    scroll->widget()->setObjectName(QStringLiteral("monitorPane"));
    scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; } #monitorPane { background: transparent; }"));
    body->addWidget(scroll, 1);
    body->addWidget(buildControl());

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(theme::kSpace);
    layout->addLayout(head);
    layout->addLayout(body, 1);

    connect(prev, &QPushButton::clicked, this, [this] { step(-1); });
    connect(next, &QPushButton::clicked, this, [this] { step(+1); });
    auto picked = [this] { setDevice(m_mbBox->currentIndex(), m_trbBox->currentIndex()); };
    connect(m_mbBox, &QComboBox::activated, this, picked);
    connect(m_trbBox, &QComboBox::activated, this, picked);
    refresh();
}

QTableWidget *TrbDetailPage::makeTable(const QStringList &rows, int firstField)
{
    auto *t = new QTableWidget(rows.size(), trbmon::kNumTrm);
    t->setHorizontalHeaderLabels(numbered(QStringLiteral("TRM%1"), trbmon::kNumTrm));
    t->setVerticalHeaderLabels(rows);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setAlternatingRowColors(true);
    t->setSelectionMode(QAbstractItemView::NoSelection);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    t->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    t->verticalHeader()->setMinimumSectionSize(kTableRowHeight);
    t->setMinimumHeight(qMax(32, t->horizontalHeader()->sizeHint().height()) + rows.size() * kTableRowHeight + 24);   // header chưa áp QSS nên lấy tối thiểu 32px
    for (int r = 0; r < rows.size(); ++r) {
        for (int c = 0; c < trbmon::kNumTrm; ++c) {
            auto *it = new QTableWidgetItem(kDash);
            it->setTextAlignment(Qt::AlignCenter);
            t->setItem(r, c, it);
        }
    }
    m_tables.append({t, firstField});
    return t;
}

QWidget *TrbDetailPage::buildMonitor()
{
    auto titled = [](const QString &title, QWidget *content) {
        auto *g = new QGroupBox(title);
        auto *l = new QVBoxLayout(g);
        l->addWidget(content);
        return g;
    };

    // Ba bảng theo thứ tự trường trong khối TRM: I_SEN 0..7, detector 8..15, còn lại 16..24.
    auto *tables = new QHBoxLayout;
    tables->addWidget(titled(QStringLiteral("Dòng điện I SEN"), makeTable(numbered(QStringLiteral("I SEN%1"), 8), 0)));
    tables->addWidget(titled(QStringLiteral("ADC detector"), makeTable(numbered(QStringLiteral("Detector %1"), 8), 8)));
    tables->addWidget(titled(QStringLiteral("Nhiệt độ / PA / LNA"),
                             makeTable(numbered(QStringLiteral("Nhiệt độ %1"), 4) + numbered(QStringLiteral("Dòng PA %1"), 4)
                                           << QStringLiteral("Dòng LNA"), 16)));

    // Thẻ số liệu một dòng: tên bên trái, giá trị bên phải, thấp hơn thẻ có tiêu đề.
    auto makeCard = [](const QString &name, QList<QLabel *> &labels) {
        auto *card = new QFrame;
        card->setProperty("card", true);
        card->setFixedHeight(kCardHeight);   // mọi thẻ số liệu cao bằng nhau
        auto *cl = new QHBoxLayout(card);
        cl->setContentsMargins(12, 2, 12, 2);
        auto *title = new QLabel(name);
        theme::setRole(title, "title");        // tên in đậm, giá trị in thường, cùng cỡ chữ
        auto *v = new QLabel(kDash);
        v->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        labels << v;
        cl->addWidget(title);
        cl->addStretch(1);
        cl->addWidget(v);
        return card;
    };
    auto *metrics = new QHBoxLayout;
    metrics->setSpacing(theme::kSpace);
    for (const QString &name : {QStringLiteral("Điện áp TRB"), QStringLiteral("Dòng điện TRB"),
                                QStringLiteral("Nhiệt độ power TRB"), QStringLiteral("Nhiệt độ MCU"),
                                QStringLiteral("Độ ẩm power")})
        metrics->addWidget(makeCard(name, m_metrics), 1);
    // Xung TXEN / BEAMSYNC: số thô, lưới 2x2 (hàng TXEN, hàng BEAMSYNC; cột Period, cột Pulse).
    auto *pulses = new QGridLayout;
    pulses->setSpacing(theme::kSpace);
    pulses->addWidget(makeCard(QStringLiteral("Period TXEN"), m_rawMetrics), 0, 0);
    pulses->addWidget(makeCard(QStringLiteral("Pulse TXEN"), m_rawMetrics), 0, 1);
    pulses->addWidget(makeCard(QStringLiteral("Period BEAMSYNC"), m_rawMetrics), 1, 0);
    pulses->addWidget(makeCard(QStringLiteral("Pulse BEAMSYNC"), m_rawMetrics), 1, 1);

    // Đèn trạng thái ADAR, PG, PA đặt trên cùng, to và có nhãn để nhìn là biết ngay.
    auto *leds = new QHBoxLayout;
    leds->setSpacing(theme::kSpace);
    leds->addWidget(ledGroup(QStringLiteral("ADAR"), numbered(QStringLiteral("ADAR%1"), 8), m_ledAdar), 2);
    leds->addWidget(ledGroup(QStringLiteral("PG"), numbered(QStringLiteral("TRM%1"), 4), m_ledPg), 1);
    leds->addWidget(ledGroup(QStringLiteral("PA"), numbered(QStringLiteral("TRM%1"), 4), m_ledPa), 1);

    // Hàng kế: INIT DATA (6 ô ứng 6 bit, 0 xám, 1 xanh) bên trái, lưới 2x2 Period/Pulse bên phải.
    QStringList initNames;
    for (const trbmon::InitBit &b : trbmon::kInitBits) initNames << QString::fromLatin1(b.name);
    auto *leds2 = new QHBoxLayout;
    leds2->setSpacing(theme::kSpace);
    leds2->addWidget(ledGroup(QStringLiteral("INIT DATA"), initNames, m_ledInit), 3);
    leds2->addLayout(pulses, 2);

    // Hàng "Trạng thái": một thẻ thấp, các nhãn TRM xếp sát nhau từ trái (hiện là mã thô State, sau này parse thành chữ).
    auto *state = new QFrame;
    state->setProperty("card", true);
    state->setFixedHeight(kCardHeight);
    auto *stateLayout = new QHBoxLayout(state);
    stateLayout->setContentsMargins(12, 2, 12, 2);
    stateLayout->setSpacing(24);
    auto *stateTitle = new QLabel(QStringLiteral("Trạng thái"));
    theme::setRole(stateTitle, "title");
    stateLayout->addWidget(stateTitle);
    for (int i = 0; i < trbmon::kNumTrm; ++i) {
        auto *label = new QLabel(kDash);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_stateLabels << label;
        stateLayout->addWidget(label);
    }
    stateLayout->addStretch(1);

    // Mỗi trip code là một byte, hiện thành 8 ô bit (bit 0 ... bit 7, LSB ở trái), bit 1 tô đỏ.
    auto *trip = new QGroupBox(QStringLiteral("Trip code"));
    auto *tripLayout = new QVBoxLayout(trip);
    QStringList captions;
    for (int i = 0; i < trbmon::kNumTrip; ++i) captions << trbmon::tripName(i);   // chỉ tên, số trip nằm ở tooltip
    tripLayout->addWidget(BitCells::makeGrid(captions, 4, m_trip));
    for (int i = 0; i < m_trip.size(); ++i) {                  // tooltip từng bit: bit 0 ... bit 7
        QStringList bits;
        for (int b = 0; b < 8; ++b) bits << trbmon::tripBitName(i, b);
        m_trip[i]->setBitNames(bits);
        m_trip[i]->setTooltipTitle(QStringLiteral("Trip %1").arg(i + 1));
    }

    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(theme::kSpace);
    l->addLayout(leds);
    l->addLayout(leds2);
    l->addLayout(tables, 1);
    l->addLayout(metrics);
    l->addWidget(state);
    l->addWidget(trip);
    return w;
}

QWidget *TrbDetailPage::buildControl()
{
    m_target = new QComboBox;
    m_target->addItems({QStringLiteral("TRB hiện tại"), QStringLiteral("Tất cả TRB (broadcast)")});

    auto *ctl = new QGroupBox(QStringLiteral("Điều khiển"));
    auto *cf = new QFormLayout(ctl);
    cf->addRow(QStringLiteral("PA"), checkRow(numbered(QStringLiteral("%1"), 4), m_pa));
    cf->addRow(QStringLiteral("Chạy"), m_start = new QCheckBox(QStringLiteral("Start")));
    m_mode = new QComboBox;
    m_mode->addItems({QStringLiteral("Normal"), QStringLiteral("Debug")});
    cf->addRow(QStringLiteral("Chế độ"), m_mode);
    m_mode->setToolTip(QStringLiteral("Chỉ một TRB được ở Debug: TRB Debug tự phát bản tin giám sát nên hai TRB Debug sẽ tranh bus."));
    // Debug không áp dụng cho "Tất cả TRB": không có mục Debug khi gửi broadcast.
    connect(m_target, &QComboBox::currentIndexChanged, this, [this](int index) {
        auto *model = qobject_cast<QStandardItemModel *>(m_mode->model());
        if (model) model->item(1)->setEnabled(index == 0);
        if (index != 0) m_mode->setCurrentIndex(0);
    });
    QCheckBox *once[2];
    cf->addRow(QStringLiteral(""), checkRow({QStringLiteral("Clear trip"), QStringLiteral("Beamsync")}, once));
    m_clearTrip = once[0];
    m_beamSync = once[1];
    auto *sendCtl = new QPushButton(QStringLiteral("Gửi lệnh điều khiển"));
    cf->addRow(sendCtl);

    auto *beam = new QGroupBox(QStringLiteral("Beam"));
    auto *bf = new QFormLayout(beam);
    auto spin = [] { auto *s = new QSpinBox; s->setRange(0, 255); return s; };
    bf->addRow(QStringLiteral("Phase TX"), m_phaseTx = spin());
    bf->addRow(QStringLiteral("Phase RX"), m_phaseRx = spin());
    bf->addRow(QStringLiteral("Amp TX"), m_ampTx = spin());
    bf->addRow(QStringLiteral("Amp RX"), m_ampRx = spin());
    bf->addRow(QStringLiteral("ADAR"), checkRow(numbered(QStringLiteral("%1"), 4), m_adar));
    bf->addRow(QString(), checkRow({"5", "6", "7", "8"}, m_adar + 4));       // cùng nhãn ADAR với hàng trên
    bf->addRow(QStringLiteral("Kênh"), checkRow({"CH0", "CH1"}, m_ch));
    bf->addRow(QString(), checkRow({"CH2", "CH3"}, m_ch + 2));
    auto *sendBeamBtn = new QPushButton(QStringLiteral("Gửi lệnh beam"));
    bf->addRow(sendBeamBtn);

    connect(sendCtl, &QPushButton::clicked, this, &TrbDetailPage::sendControl);
    connect(sendBeamBtn, &QPushButton::clicked, this, &TrbDetailPage::sendBeam);

    theme::addShadow(ctl);   // thẻ tĩnh, không cập nhật liên tục
    theme::addShadow(beam);
    auto *w = new QWidget;
    w->setFixedWidth(300);
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(4, 4, 4, 12);   // chừa chỗ cho bóng đổ
    l->setSpacing(theme::kMargin);
    auto *tf = new QFormLayout;
    tf->addRow(QStringLiteral("Gửi tới"), m_target);
    l->addLayout(tf);
    l->addWidget(ctl);
    l->addWidget(beam);
    l->addStretch(1);
    return w;
}

void TrbDetailPage::setDevice(int mb, int trb)
{
    if (!m_store->contains(mb, trb)) return;
    m_mb = mb;
    m_trb = trb;
    m_mbBox->setCurrentIndex(mb);
    m_trbBox->setCurrentIndex(trb);

    // Lệnh điều khiển ghi đè cả 4 bit PA, nên lấy sẵn trạng thái PA đang giám sát được
    // để một lệnh "Clear trip" không vô tình tắt PA.
    const model::TrbState &s = m_store->trb(mb, trb);
    if (s.frames > 0) {
        const int pa = int(s.values.at(trbmon::kIdxPa));
        for (int i = 0; i < 4; ++i) m_pa[i]->setChecked(pa >> i & 1);
    }
    refresh();
    emit deviceChanged(mb, trb);
}

void TrbDetailPage::step(int delta)
{
    const int n = m_store->mbCount() * m_store->trbPerMb();
    const int i = (m_mb * m_store->trbPerMb() + m_trb + delta + n) % n;
    setDevice(i / m_store->trbPerMb(), i % m_store->trbPerMb());
}

void TrbDetailPage::refresh()
{
    const model::TrbState &s = m_store->trb(m_mb, m_trb);
    const bool has = s.frames > 0;

    // Hiện đủ mọi trạng thái hiện tại của TRB, mỗi trạng thái một thẻ (ví dụ Trip + Quá ngưỡng + Mất kết nối).
    const QList<Status> cond = m_store->conditions(m_mb, m_trb);
    for (int i = 0; i < m_pills.size(); ++i) {
        m_pills[i]->setVisible(i < cond.size());
        if (i >= cond.size()) continue;
        const Status st = cond.at(i);
        const QString mark = theme::statusMark(st);
        m_pills[i]->setPill((mark.isEmpty() ? QString() : mark + QLatin1Char(' ')) + statusText(st),
                            OverviewGrid::statusColor(st), theme::statusTextColor(st));
    }
    QString text;
    if (has)
        text = QStringLiteral("Cập nhật %1 trước · %2 bản tin")
                   .arg(elapsedText(QDateTime::currentMSecsSinceEpoch() - s.lastSeenMs)).arg(s.frames);
    m_status->setText(text);

    const auto debug = m_control->debugTrb();
    m_debugPill->setVisible(debug.has_value());
    if (debug)
        m_debugPill->setPill(QStringLiteral("Debug: MB%1 / TRB%2").arg(debug->first + 1).arg(debug->second + 1),
                             OverviewGrid::statusColor(Status::Updating), Qt::white);

    auto val = [&](int idx) { return has ? QString::number(s.values.at(idx)) : kDash; };
    auto bits = [&](int idx) { return has ? int(s.values.at(idx)) : 0; };
    auto limitText = [&](int field) {
        const model::Limit l = m_thresholds->get(m_mb, m_trb, field);
        return l.isSet() ? QStringLiteral("Ngưỡng: %1 … %2").arg(l.min).arg(l.max) : QStringLiteral("Chưa đặt ngưỡng");
    };

    for (const Table &t : m_tables)
        for (int r = 0; r < t.w->rowCount(); ++r)
            for (int c = 0; c < trbmon::kNumTrm; ++c)
            {
                const int f = trbmon::kIdxTrm0 + c * trbmon::kTrmFields + t.firstField + r;
                QTableWidgetItem *it = t.w->item(r, c);
                it->setText(val(f));
                it->setToolTip(limitText(f));
                const bool bad = s.alarms.contains(f);
                it->setBackground(bad ? QBrush(QColor(0xEF9F27)) : QBrush());
                QFont font = it->font();
                font.setBold(bad);
                it->setFont(font);
            }

    const int metricIdx[] = {trbmon::kIdxTrbV, trbmon::kIdxTrbI, trbmon::kIdxTrbTemp, trbmon::kIdxMcuTemp,
                             trbmon::kIdxHumidity};
    for (int i = 0; i < m_metrics.size(); ++i) {
        m_metrics[i]->setText(val(metricIdx[i]));
        m_metrics[i]->setToolTip(limitText(metricIdx[i]));
        m_metrics[i]->setStyleSheet(s.alarms.contains(metricIdx[i])
                                        ? QStringLiteral("background:#EF9F27; font-weight:bold;") : QString());
    }

    // Mất kết nối vẫn giữ nguyên trạng thái đèn lần cuối nhận được; dòng "Cập nhật n s trước" cho biết số liệu cũ đến đâu.
    using Led = LedIndicator::State;
    const bool fresh = has;
    auto setLeds = [&](const QList<LedIndicator *> &list, int field, Led off, const QString &name) {
        for (int i = 0; i < list.size(); ++i) {
            const bool on = bits(field) >> i & 1;
            list[i]->setState(!fresh ? Led::Unknown : on ? Led::On : off,
                              !has ? QString() : QStringLiteral("%1 %2 = %3").arg(name).arg(i + 1).arg(on ? 1 : 0));
        }
    };
    setLeds(m_ledAdar, trbmon::kIdxInitAdar, Led::Fault, QStringLiteral("ADAR"));
    setLeds(m_ledPg, trbmon::kIdxPg, Led::Fault, QStringLiteral("PG TRM"));
    setLeds(m_ledPa, trbmon::kIdxPa, Led::Idle, QStringLiteral("PA TRM")); // PA tắt là trạng thái Stop bình thường
    for (int i = 0; i < trbmon::kNumInitBits; ++i) {   // INIT DATA: 1 = xanh, 0 = xám
        const int bit = trbmon::kInitBits[i].bit;
        const bool on = bits(trbmon::kIdxInitData) >> bit & 1;
        m_ledInit[i]->setState(!has ? Led::Unknown : on ? Led::On : Led::Idle,
                               !has ? QString() : QStringLiteral("%1 (bit %2) = %3").arg(QLatin1String(trbmon::kInitBits[i].name)).arg(bit).arg(on ? 1 : 0));
    }
    const int rawIdx[] = {trbmon::kIdxPeriodTxen, trbmon::kIdxPulseTxen, trbmon::kIdxPeriodBeamsync, trbmon::kIdxPulseBeamsync};
    for (int i = 0; i < m_rawMetrics.size(); ++i) m_rawMetrics[i]->setText(val(rawIdx[i]));
    refreshErrors();
    for (int i = 0; i < m_stateLabels.size(); ++i)
        m_stateLabels[i]->setText(QStringLiteral("TRM%1: %2").arg(i + 1).arg(val(trbmon::kIdxState0 + i)));
    for (int i = 0; i < m_trip.size(); ++i) m_trip[i]->setValue(bits(trbmon::kIdxTrip0 + i), has);
}

// Tổng bản tin lỗi trên nút; tooltip liệt kê các loại khác 0.
void TrbDetailPage::refreshErrors()
{
    const model::TrbState &s = m_store->trb(m_mb, m_trb);
    const bool has = s.frames > 0;
    qint64 total = 0;
    QStringList nonZero;
    for (int i = 0; i < trbmon::kNumErr; ++i) {
        const int n = has ? int(s.values.at(trbmon::kIdxErr0 + i)) : 0;
        total += n;
        if (n) nonZero << QStringLiteral("%1: %2").arg(trbmon::errName(i)).arg(n);
        if (m_errTable) {
            QTableWidgetItem *it = m_errTable->item(i, 1);
            it->setText(has ? QString::number(n) : kDash);
            const bool bad = n > 0;
            it->setBackground(bad ? QBrush(QColor(0xEF9F27)) : QBrush());
            QFont f = it->font();
            f.setBold(bad);
            it->setFont(f);
            m_errTable->item(i, 0)->setFont(f);
        }
    }
    m_errButton->setText(has ? QStringLiteral("Tổng: %1  ▸").arg(total) : kDash);
    m_errButton->setToolTip(!has ? QStringLiteral("Chưa có dữ liệu")
                            : nonZero.isEmpty() ? QStringLiteral("Không có bản tin lỗi. Bấm để xem từng loại.")
                                                : nonZero.join(QLatin1Char('\n')));
    if (m_errDialog) m_errDialog->setWindowTitle(QStringLiteral("Bản tin lỗi · MB%1 / TRB%2 · tổng %3").arg(m_mb + 1).arg(m_trb + 1).arg(has ? QString::number(total) : kDash));
}

// Cửa sổ nhỏ không chặn thao tác, tự cập nhật theo TRB đang xem.
void TrbDetailPage::showErrors()
{
    if (!m_errDialog) {
        m_errDialog = new QDialog(this);
        m_errTable = new QTableWidget(trbmon::kNumErr, 2, m_errDialog);
        m_errTable->setHorizontalHeaderLabels({QStringLiteral("Loại bản tin lỗi"), QStringLiteral("Số bản tin")});
        m_errTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_errTable->setAlternatingRowColors(true);
        m_errTable->setSelectionMode(QAbstractItemView::NoSelection);
        m_errTable->verticalHeader()->hide();
        m_errTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        for (int i = 0; i < trbmon::kNumErr; ++i) {
            m_errTable->setItem(i, 0, new QTableWidgetItem(QStringLiteral("%1. %2").arg(i + 1).arg(trbmon::errName(i))));
            auto *n = new QTableWidgetItem(kDash);
            n->setTextAlignment(Qt::AlignCenter);
            m_errTable->setItem(i, 1, n);
        }
        auto *l = new QVBoxLayout(m_errDialog);
        l->addWidget(m_errTable);
        m_errDialog->resize(520, 30 * trbmon::kNumErr + 90);
    }
    refreshErrors();
    m_errDialog->show();
    m_errDialog->raise();
}

bool TrbDetailPage::confirmTarget(int &mb, int &trb)
{
    mb = m_mb;
    trb = m_trb;
    if (m_target->currentIndex() == 0) return true;
    mb = trb = trbctl::kBroadcast;
    return QMessageBox::question(this, QStringLiteral("Gửi tới tất cả TRB"),
                                 QStringLiteral("Lệnh sẽ được gửi tới tất cả TRB trong hệ thống. Tiếp tục?"))
           == QMessageBox::Yes;
}

void TrbDetailPage::sendControl()
{
    int mb, trb;
    if (!confirmTarget(mb, trb)) return;
    trbctl::ControlCmd c;
    for (int i = 0; i < 4; ++i) c.paMask |= m_pa[i]->isChecked() << i;
    c.start = m_start->isChecked();
    c.debugMode = m_mode->currentIndex() == 1;
    c.clearTrip = m_clearTrip->isChecked();
    c.beamSync = m_beamSync->isChecked();
    using Check = services::TrbControl::Check;
    const Check chk = m_control->check(mb, trb, c);
    if (chk == Check::BroadcastDebug) {
        QMessageBox::warning(this, QStringLiteral("Chế độ Debug"),
                             QStringLiteral("Không đặt Debug cho tất cả TRB: chỉ một TRB được chạy Debug."));
        return;
    }
    if (chk == Check::OtherInDebug) {
        const auto other = *m_control->debugTrb();
        if (QMessageBox::question(this, QStringLiteral("Chế độ Debug"),
                QStringLiteral("MB%1 / TRB%2 đang ở chế độ Debug. Chỉ một TRB được chạy Debug vì TRB Debug tự phát bản tin giám sát, "
                               "hai TRB sẽ tranh bus.\n\nĐưa MB%1 / TRB%2 về Normal (giữ nguyên PA và Start) rồi đặt MB%3 / TRB%4 sang Debug?")
                    .arg(other.first + 1).arg(other.second + 1).arg(mb + 1).arg(trb + 1)) != QMessageBox::Yes) return;
        m_control->sendControlSwitchDebug(mb, trb, c);
    } else {
        m_control->sendControl(mb, trb, c);
    }
    m_clearTrip->setChecked(false); // lệnh một lần, như tool cũ
    m_beamSync->setChecked(false);
}

void TrbDetailPage::sendBeam()
{
    int mb, trb;
    if (!confirmTarget(mb, trb)) return;
    trbctl::BeamCmd b;
    b.phaseTx = quint8(m_phaseTx->value());
    b.phaseRx = quint8(m_phaseRx->value());
    b.ampTx = quint8(m_ampTx->value());
    b.ampRx = quint8(m_ampRx->value());
    for (int i = 0; i < 8; ++i) b.adarMask |= m_adar[i]->isChecked() << i;
    for (int i = 0; i < 4; ++i) b.chMask |= m_ch[i]->isChecked() << i;
    m_control->sendBeam(mb, trb, b);
}

} // namespace ui
