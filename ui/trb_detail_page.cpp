#include "trb_detail_page.h"
#include "led_indicator.h"
#include "overview_grid.h"
#include "status_pill.h"
#include "theme.h"
#include "../proto/trb_monitor_proto.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>

namespace ui {

using namespace proto;
using model::Status;

namespace {
const QString kDash = QStringLiteral("--");

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
    auto *l = new QHBoxLayout(g);
    l->setSpacing(theme::kSpace);
    for (const QString &c : captions) {
        auto *led = new LedIndicator(c);
        leds << led;
        l->addWidget(led);
    }
    l->addStretch(1);
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
    for (int i = 0; i < store->mbCount(); ++i) m_mbBox->addItem(QStringLiteral("MB%1").arg(i));
    for (int i = 0; i < store->trbPerMb(); ++i) m_trbBox->addItem(QStringLiteral("TRB%1").arg(i));
    auto *prev = new QPushButton(QStringLiteral("◀ Trước"));
    auto *next = new QPushButton(QStringLiteral("Sau ▶"));
    theme::setRole(prev, "secondary");
    theme::setRole(next, "secondary");
    m_pill = new StatusPill;
    m_status = new QLabel;
    theme::setRole(m_status, "muted");

    auto *head = new QHBoxLayout;
    head->addWidget(m_mbBox);
    head->addWidget(m_trbBox);
    head->addWidget(prev);
    head->addWidget(next);
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
    t->setSelectionMode(QAbstractItemView::NoSelection);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    t->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
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
    tables->addWidget(titled(QStringLiteral("Dòng điện I_SEN"), makeTable(numbered(QStringLiteral("I_SEN%1"), 8), 0)));
    tables->addWidget(titled(QStringLiteral("ADC detector"), makeTable(numbered(QStringLiteral("Detector %1"), 8), 8)));
    tables->addWidget(titled(QStringLiteral("Nhiệt độ / PA / LNA"),
                             makeTable(numbered(QStringLiteral("Nhiệt độ %1"), 4) + numbered(QStringLiteral("Dòng PA %1"), 4)
                                           << QStringLiteral("Dòng LNA"), 16)));

    auto *metrics = new QHBoxLayout;
    for (const QString &name : {QStringLiteral("Điện áp TRB"), QStringLiteral("Dòng điện TRB"),
                                QStringLiteral("Nhiệt độ power TRB"), QStringLiteral("Nhiệt độ MCU"),
                                QStringLiteral("Độ ẩm power")}) {
        auto *v = new QLabel(kDash);
        v->setAlignment(Qt::AlignCenter);
        v->setProperty("role", "metric");
        m_metrics << v;
        metrics->addWidget(titled(name, v));
    }

    // Đèn trạng thái ADAR, PG, PA đặt trên cùng, to và có nhãn để nhìn là biết ngay.
    auto *leds = new QHBoxLayout;
    leds->setSpacing(theme::kSpace);
    leds->addWidget(ledGroup(QStringLiteral("Init ADAR"), numbered(QStringLiteral("ADAR%1"), 8), m_ledAdar), 2);
    leds->addWidget(ledGroup(QStringLiteral("PG (power good)"), numbered(QStringLiteral("TRM%1"), 4), m_ledPg), 1);
    leds->addWidget(ledGroup(QStringLiteral("PA"), numbered(QStringLiteral("TRM%1"), 4), m_ledPa), 1);

    auto *flags = new QGroupBox(QStringLiteral("Mã trạng thái"));
    auto *form = new QFormLayout(flags);
    auto valueRow = [](int n, QList<QLabel *> &out, const QString &prefix) {
        auto *w = new QWidget;
        auto *l = new QHBoxLayout(w);
        l->setContentsMargins(0, 0, 0, 0);
        for (int i = 0; i < n; ++i) {
            if (!prefix.isEmpty()) l->addWidget(new QLabel(prefix.arg(i + 1)));
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
    form->addRow(QStringLiteral("State TRM (mã thô)"), valueRow(trbmon::kNumTrm, m_state, QStringLiteral("TRM%1")));
    form->addRow(QStringLiteral("Trip code 1–16 (hex)"), valueRow(trbmon::kNumTrip, m_trip, QString()));

    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(theme::kSpace);
    l->addLayout(leds);
    l->addLayout(tables, 1);
    l->addLayout(metrics);
    l->addWidget(flags);
    return w;
}

QWidget *TrbDetailPage::buildControl()
{
    m_target = new QComboBox;
    m_target->addItems({QStringLiteral("TRB đang xem"), QStringLiteral("Tất cả TRB (broadcast)")});

    auto *ctl = new QGroupBox(QStringLiteral("Điều khiển"));
    auto *cf = new QFormLayout(ctl);
    cf->addRow(QStringLiteral("PA (TRM)"), checkRow(numbered(QStringLiteral("%1"), 4), m_pa));
    cf->addRow(QStringLiteral("Chạy"), m_start = new QCheckBox(QStringLiteral("Start (bỏ chọn = Stop)")));
    m_mode = new QComboBox;
    m_mode->addItems({QStringLiteral("Normal"), QStringLiteral("Debug")});
    cf->addRow(QStringLiteral("Chế độ"), m_mode);
    QCheckBox *once[2];
    cf->addRow(QStringLiteral("Một lần"), checkRow({QStringLiteral("Clear trip"), QStringLiteral("Beamsync")}, once));
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
    bf->addRow(QStringLiteral("ADAR 1–4"), checkRow(numbered(QStringLiteral("%1"), 4), m_adar));
    bf->addRow(QStringLiteral("ADAR 5–8"), checkRow({"5", "6", "7", "8"}, m_adar + 4));
    bf->addRow(QStringLiteral("Kênh"), checkRow({"CH0", "CH1", "CH2", "CH3"}, m_ch));
    auto *sendBeamBtn = new QPushButton(QStringLiteral("Gửi lệnh beam"));
    bf->addRow(sendBeamBtn);

    connect(sendCtl, &QPushButton::clicked, this, &TrbDetailPage::sendControl);
    connect(sendBeamBtn, &QPushButton::clicked, this, &TrbDetailPage::sendBeam);

    theme::addShadow(ctl);   // thẻ tĩnh, không cập nhật liên tục
    theme::addShadow(beam);
    auto *w = new QWidget;
    w->setFixedWidth(360);
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

    m_pill->setPill(theme::statusMark(s.status) + (theme::statusMark(s.status).isEmpty() ? QString() : QStringLiteral(" "))
                        + statusText(s.status),
                    OverviewGrid::statusColor(s.status), theme::statusTextColor(s.status));
    QString text;
    if (has)
        text = QStringLiteral("Cập nhật %1 s trước · %2 bản tin")
                   .arg((QDateTime::currentMSecsSinceEpoch() - s.lastSeenMs) / 1000.0, 0, 'f', 1).arg(s.frames);
    if (s.status == Status::Lost) text += QStringLiteral(" · SỐ LIỆU BÊN DƯỚI LÀ SỐ LIỆU CŨ");
    m_status->setText(text);

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

    // Số liệu cũ (mất kết nối) thì đèn về "không rõ" thay vì hiện trạng thái đã cũ như đang đúng.
    using Led = LedIndicator::State;
    const bool fresh = has && s.status != Status::Lost;
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
    for (int i = 0; i < m_state.size(); ++i) m_state[i]->setText(val(trbmon::kIdxState0 + i));
    for (int i = 0; i < m_trip.size(); ++i) {
        const int code = bits(trbmon::kIdxTrip0 + i);
        m_trip[i]->setText(has ? QStringLiteral("%1").arg(code, 2, 16, QLatin1Char('0')).toUpper() : kDash);
        m_trip[i]->setStyleSheet(code ? QStringLiteral("background:#E24B4A; color:white; font-weight:bold;") : QString());
    }
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
    m_control->sendControl(mb, trb, c);
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
