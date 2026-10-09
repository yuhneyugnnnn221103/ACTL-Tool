#include "sim_window.h"
#include "../core/serial_transport.h"
#include "../core/tcp_client_transport.h"
#include "../core/tcp_server_transport.h"
#include "../proto/trb_monitor_proto.h"
#include "../ui/status_pill.h"
#include "../ui/theme.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSerialPortInfo>
#include <QSettings>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

namespace sim {

using namespace proto;
namespace theme = ui::theme;

namespace {
const QColor kOk(0x16, 0xA3, 0x4A), kLost(0x6B, 0x72, 0x80), kTrip(0xDC, 0x26, 0x26), kOver(0xF5, 0x9E, 0x0B),
    kDebug(0x25, 0x63, 0xEB);

// Style sheet của app bỏ qua màu nền của từng ô, nên tự vẽ ô: nền theo trạng thái, đậm hơn khi được chọn.
class CellDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const override
    {
        const QColor base = index.data(Qt::BackgroundRole).value<QColor>();
        const bool sel = opt.state & QStyle::State_Selected;
        p->save();
        const QRect r = opt.rect.adjusted(1, 1, -1, -1);
        p->fillRect(r, sel ? base : base.lighter(140));
        if (sel) { p->setPen(QPen(QColor(0x1F, 0x29, 0x37), 2)); p->drawRect(r.adjusted(1, 1, -1, -1)); }
        p->setPen(sel ? Qt::white : QColor(0x1F, 0x29, 0x37));
        p->drawText(r, Qt::AlignCenter, index.data(Qt::DisplayRole).toString());
        p->restore();
    }
};

QPushButton *button(const QString &text, QWidget *parent, const char *role = nullptr)
{
    auto *b = new QPushButton(text, parent);
    if (role) theme::setRole(b, role);
    return b;
}
}

SimWindow::SimWindow()
{
    setWindowTitle(QStringLiteral("ACTL Sim - giả lập TRB"));
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(3000);
    m_hex = new QCheckBox(QStringLiteral("Hiện bản tin hex"));

    auto *root = new QWidget;
    auto *l = new QVBoxLayout(root);
    l->setContentsMargins(theme::kMargin, theme::kMargin, theme::kMargin, theme::kMargin);
    l->setSpacing(theme::kSpace);
    l->addWidget(buildConnection());
    auto *mid = new QHBoxLayout;
    mid->addWidget(buildGrid(), 1);
    auto *side = new QVBoxLayout;
    side->addWidget(buildActions());
    side->addWidget(buildFaults());
    side->addStretch(1);
    mid->addLayout(side);
    l->addLayout(mid, 1);
    auto *logRow = new QHBoxLayout;
    logRow->addWidget(m_stats = new QLabel);
    logRow->addStretch(1);
    logRow->addWidget(m_hex);
    auto *clear = button(QStringLiteral("Xóa log"), this, "secondary");
    connect(clear, &QPushButton::clicked, m_log, &QPlainTextEdit::clear);
    logRow->addWidget(clear);
    l->addLayout(logRow);
    m_log->setMinimumHeight(120);
    m_log->setMaximumHeight(180);
    l->addWidget(m_log);
    setCentralWidget(root);

    loadSettings();
    rebuildWorld();
    setRunningUi(false);

    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &SimWindow::refresh);
    timer->start(500);
    resize(1180, 760);
}

SimWindow::~SimWindow()
{
    saveSettings();
    m_port.reset();
    m_transport.reset();
}

// ---------------------------------------------------------------- kết nối

QWidget *SimWindow::buildConnection()
{
    auto *box = new QGroupBox(QStringLiteral("Nối tới Gateway"));
    auto *l = new QHBoxLayout(box);

    m_serial = new QRadioButton(QStringLiteral("Cổng COM (RS485)"));
    m_connect = new QRadioButton(QStringLiteral("Ethernet: nối tới Gateway"));
    m_listen = new QRadioButton(QStringLiteral("Ethernet: chờ Gateway nối vào"));
    m_serial->setChecked(true);

    m_ports = new QComboBox;
    m_ports->setEditable(true);
    auto *refresh = button(QStringLiteral("↻"), this, "secondary");
    refresh->setToolTip(QStringLiteral("Quét lại danh sách cổng COM"));
    refresh->setMaximumWidth(36);
    auto scan = [this] {
        const QString keep = m_ports->currentText();
        m_ports->clear();
        for (const QSerialPortInfo &i : QSerialPortInfo::availablePorts()) m_ports->addItem(i.portName());
        if (!keep.isEmpty()) m_ports->setCurrentText(keep);
    };
    connect(refresh, &QPushButton::clicked, this, scan);
    scan();
    m_baud = new QComboBox;
    m_baud->setEditable(true);
    for (const char *b : {"9600", "115200", "921600", "1000000", "2000000"}) m_baud->addItem(b);
    m_baud->setCurrentText("1000000");
    m_host = new QLineEdit("192.168.1.10");
    m_connectPort = new QSpinBox;
    m_connectPort->setRange(1, 65535);
    m_connectPort->setValue(5000);
    m_listenPort = new QSpinBox;
    m_listenPort->setRange(1, 65535);
    m_listenPort->setValue(5000);

    auto *form = new QGridLayout;
    form->addWidget(m_serial, 0, 0);
    form->addWidget(m_ports, 0, 1);
    form->addWidget(refresh, 0, 2);
    form->addWidget(new QLabel(QStringLiteral("Baud")), 0, 3);
    form->addWidget(m_baud, 0, 4);
    form->addWidget(m_connect, 1, 0);
    form->addWidget(m_host, 1, 1);
    form->addWidget(new QLabel(QStringLiteral("Cổng")), 1, 3);
    form->addWidget(m_connectPort, 1, 4);
    form->addWidget(m_listen, 2, 0);
    form->addWidget(new QLabel(QStringLiteral("Cổng")), 2, 3);
    form->addWidget(m_listenPort, 2, 4);
    form->setColumnStretch(1, 1);
    l->addLayout(form, 1);

    m_mb = new QSpinBox;
    m_mb->setRange(1, 20);
    m_mb->setValue(20);
    m_trbPerMb = new QSpinBox;
    m_trbPerMb->setRange(1, 8);
    m_trbPerMb->setValue(8);
    m_debugPeriod = new QSpinBox;
    m_debugPeriod->setRange(0, 60000);
    m_debugPeriod->setSingleStep(100);
    m_debugPeriod->setValue(1000);
    m_debugPeriod->setSuffix(" ms");
    m_debugPeriod->setSpecialValueText(QStringLiteral("tắt"));
    m_debugPeriod->setToolTip(QStringLiteral("Chu kỳ TRB đang ở chế độ Debug tự gửi khung giám sát"));
    auto *opt = new QFormLayout;
    opt->addRow(QStringLiteral("Số MB"), m_mb);
    opt->addRow(QStringLiteral("TRB mỗi MB"), m_trbPerMb);
    opt->addRow(QStringLiteral("Chu kỳ debug"), m_debugPeriod);
    l->addLayout(opt);
    connect(m_mb, QOverload<int>::of(&QSpinBox::valueChanged), this, &SimWindow::rebuildWorld);
    connect(m_trbPerMb, QOverload<int>::of(&QSpinBox::valueChanged), this, &SimWindow::rebuildWorld);
    connect(m_debugPeriod, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int ms) {
        if (m_world) m_world->setDebugPeriodMs(ms);
    });

    auto *right = new QVBoxLayout;
    m_run = button(QStringLiteral("Bắt đầu"), this);
    m_run->setMinimumWidth(120);
    connect(m_run, &QPushButton::clicked, this, &SimWindow::toggleRun);
    m_pill = new ui::StatusPill;
    right->addWidget(m_run);
    right->addWidget(m_pill, 0, Qt::AlignHCenter);
    right->addStretch(1);
    l->addLayout(right);
    return box;
}

void SimWindow::setRunningUi(bool running)
{
    for (QWidget *w : {static_cast<QWidget *>(m_serial), static_cast<QWidget *>(m_connect), static_cast<QWidget *>(m_listen),
                       static_cast<QWidget *>(m_ports), static_cast<QWidget *>(m_baud), static_cast<QWidget *>(m_host),
                       static_cast<QWidget *>(m_connectPort), static_cast<QWidget *>(m_listenPort),
                       static_cast<QWidget *>(m_mb), static_cast<QWidget *>(m_trbPerMb)})
        w->setEnabled(!running);
    m_run->setText(running ? QStringLiteral("Dừng") : QStringLiteral("Bắt đầu"));
    theme::setRole(m_run, running ? "danger" : nullptr);
    m_run->style()->unpolish(m_run);
    m_run->style()->polish(m_run);
    if (!running) m_pill->setPill(QStringLiteral("Đang dừng"), kLost);
}

void SimWindow::toggleRun()
{
    if (m_port) stop(); else start();
}

void SimWindow::start()
{
    core::Transport *t = nullptr;
    if (m_serial->isChecked()) {
        const QString port = m_ports->currentText().trimmed();
        if (port.isEmpty()) { log(QStringLiteral("Chưa chọn cổng COM")); return; }
        t = new core::SerialTransport(port, m_baud->currentText().toInt());
    } else if (m_connect->isChecked()) {
        t = new core::TcpClientTransport(m_host->text().trimmed(), quint16(m_connectPort->value()));
    } else {
        t = new core::TcpServerTransport(QHostAddress::Any, quint16(m_listenPort->value()));
    }
    m_transport.reset(t);
    m_port = std::make_unique<BusPort>(t, m_world.get());
    connect(m_port.get(), &BusPort::message, this, [this](const QString &m) {
        log(m);
        const auto st = m_transport ? m_transport->state() : core::Transport::State::Closed;
        if (st == core::Transport::State::Connected) m_pill->setPill(QStringLiteral("Đã kết nối"), kOk);
        else if (st == core::Transport::State::Waiting) m_pill->setPill(QStringLiteral("Chờ kết nối"), kOver);
    });
    connect(m_port.get(), &BusPort::received, this, [this](const QByteArray &b) {
        if (m_hex->isChecked()) log("RX " + QString::fromLatin1(b.toHex(' ').toUpper()));
    });
    connect(m_port.get(), &BusPort::sent, this, [this](const QByteArray &b) {
        if (m_hex->isChecked()) log("TX " + QString::fromLatin1(b.left(24).toHex(' ').toUpper())
                                    + (b.size() > 24 ? QStringLiteral(" … (%1 byte)").arg(b.size()) : QString()));
    });
    applyFaults();
    setRunningUi(true);
    m_pill->setPill(QStringLiteral("Chờ kết nối"), kOver);
    log(QStringLiteral("Bắt đầu: %1").arg(t->describe()));
    m_port->open();
}

void SimWindow::stop()
{
    if (m_port) m_port->close();
    m_port.reset();
    m_transport.reset();
    setRunningUi(false);
    log(QStringLiteral("Đã dừng"));
}

void SimWindow::rebuildWorld()
{
    if (m_port) return;     // spinbox bị khóa khi đang chạy
    m_world = std::make_unique<SimWorld>(m_mb->value(), m_trbPerMb->value(), 1);
    m_world->setDebugPeriodMs(m_debugPeriod->value());

    m_grid->clear();
    m_grid->setRowCount(m_mb->value());
    m_grid->setColumnCount(m_trbPerMb->value());
    QStringList cols, rows;
    for (int t = 0; t < m_trbPerMb->value(); ++t) cols << QStringLiteral("TRB%1").arg(t);
    for (int mb = 0; mb < m_mb->value(); ++mb) rows << QStringLiteral("MB%1").arg(mb);
    m_grid->setHorizontalHeaderLabels(cols);
    m_grid->setVerticalHeaderLabels(rows);
    for (int r = 0; r < m_grid->rowCount(); ++r)
        for (int c = 0; c < m_grid->columnCount(); ++c) {
            auto *it = new QTableWidgetItem;
            it->setTextAlignment(Qt::AlignCenter);
            m_grid->setItem(r, c, it);
        }
    refresh();
}

// ---------------------------------------------------------------- lưới TRB

QWidget *SimWindow::buildGrid()
{
    auto *box = new QGroupBox(QStringLiteral("TRB (kéo chuột hoặc bấm tiêu đề MB/TRB để chọn nhiều)"));
    auto *l = new QVBoxLayout(box);
    m_grid = new QTableWidget;
    m_grid->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_grid->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_grid->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_grid->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_grid->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_grid->verticalHeader()->setMinimumSectionSize(20);
    m_grid->setMinimumWidth(520);
    m_grid->setItemDelegate(new CellDelegate(m_grid));
    m_grid->setShowGrid(false);
    connect(m_grid, &QTableWidget::itemSelectionChanged, this, &SimWindow::refresh);

    auto *row = new QHBoxLayout;
    auto *all = button(QStringLiteral("Chọn tất cả"), this, "secondary");
    auto *none = button(QStringLiteral("Bỏ chọn"), this, "secondary");
    connect(all, &QPushButton::clicked, m_grid, &QTableWidget::selectAll);
    connect(none, &QPushButton::clicked, m_grid, &QTableWidget::clearSelection);
    row->addWidget(all);
    row->addWidget(none);
    row->addWidget(m_selection = new QLabel);
    row->addStretch(1);
    auto legend = [&](const QString &text, const QColor &c) {
        auto *lab = new QLabel(QStringLiteral("<span style='color:%1'>■</span> %2").arg(c.name(), text));
        row->addWidget(lab);
    };
    legend("OK", kOk);
    legend(QStringLiteral("Mất"), kLost);
    legend(QStringLiteral("Vượt ngưỡng"), kOver);
    legend("Trip", kTrip);
    legend("Debug", kDebug);
    l->addWidget(m_grid, 1);
    l->addLayout(row);
    return box;
}

void SimWindow::refresh()
{
    if (!m_world) return;
    for (int mb = 0; mb < m_world->mbCount(); ++mb)
        for (int t = 0; t < m_world->trbPerMb(); ++t) {
            TrbModel *m = m_world->trb(mb, t);
            QTableWidgetItem *it = m_grid->item(mb, t);
            if (!it) continue;
            QColor c = kOk;
            QString text = QStringLiteral("OK");
            if (!m->online()) { c = kLost; text = QStringLiteral("Mất"); }
            else if (m->hasTrip()) { c = kTrip; text = QStringLiteral("Trip"); }
            else if (m->hasForced()) { c = kOver; text = QStringLiteral("Ép"); }
            else if (m->debug()) { c = kDebug; text = QStringLiteral("Debug"); }
            it->setData(Qt::BackgroundRole, c);
            it->setText(text);
        }
    m_selection->setText(QStringLiteral("Đã chọn %1 TRB").arg(m_grid->selectedItems().size()));
    const auto &s = m_world->stats();
    QString txt = QStringLiteral("Hỏi giám sát %1 · điều khiển %2 · beam %3 · đọc cấu hình %4 · ghi cấu hình %5 · TRB debug %6")
                      .arg(s.polls).arg(s.controls).arg(s.beams).arg(s.configReads).arg(s.configWrites).arg(m_world->debugCount());
    if (s.debugConflicts) txt += QStringLiteral(" · <b style='color:#DC2626'>XUNG ĐỘT DEBUG %1</b>").arg(s.debugConflicts);
    if (m_port) txt += QStringLiteral(" · bỏ %1 · sai CRC cố ý %2 · khung lỗi nhận %3")
                           .arg(m_port->stats().dropped).arg(m_port->stats().corrupted).arg(m_port->rxStats().crcErrors);
    m_stats->setText(txt);
}

void SimWindow::forSelected(const std::function<void(TrbModel *)> &fn)
{
    const auto items = m_grid->selectedItems();
    if (items.isEmpty()) { log(QStringLiteral("Chưa chọn TRB nào")); return; }
    for (QTableWidgetItem *it : items)
        if (TrbModel *m = m_world->trb(it->row(), it->column())) fn(m);
    refresh();
}

// ---------------------------------------------------------------- thao tác

QWidget *SimWindow::buildActions()
{
    auto *box = new QGroupBox(QStringLiteral("Thao tác với TRB đã chọn"));
    auto *l = new QVBoxLayout(box);

    auto *row = new QHBoxLayout;
    auto *lost = button(QStringLiteral("Mất kết nối"), this, "secondary");
    auto *back = button(QStringLiteral("Trở lại"), this, "secondary");
    auto *normal = button(QStringLiteral("Bình thường"), this);
    normal->setToolTip(QStringLiteral("Trở lại, bỏ mọi giá trị ép và xóa trip code"));
    row->addWidget(lost);
    row->addWidget(back);
    row->addWidget(normal);
    l->addLayout(row);
    connect(lost, &QPushButton::clicked, this, [this] { forSelected([](TrbModel *m) { m->setOnline(false); }); });
    connect(back, &QPushButton::clicked, this, [this] { forSelected([](TrbModel *m) { m->setOnline(true); }); });
    connect(normal, &QPushButton::clicked, this, [this] {
        forSelected([](TrbModel *m) { m->setOnline(true); m->clearForced(); m->clearTrip(); });
    });

    l->addWidget(new QLabel(QStringLiteral("<b>Ép giá trị giám sát</b> (để thử cảnh báo vượt ngưỡng)")));
    m_field = new QComboBox;
    m_field->setEditable(true);
    m_field->setInsertPolicy(QComboBox::NoInsert);
    for (const Field &f : trbmon::table().fields()) m_field->addItem(f.name);
    m_field->setCurrentText("TRM1.I SEN1");
    m_value = new QDoubleSpinBox;
    m_value->setRange(0, 4294967295.0);
    m_value->setDecimals(0);
    m_value->setValue(2500);
    auto *force = button(QStringLiteral("Ép"), this);
    auto *unforce = button(QStringLiteral("Bỏ ép"), this, "secondary");
    auto *frow = new QHBoxLayout;
    frow->addWidget(m_value, 1);
    frow->addWidget(force);
    frow->addWidget(unforce);
    l->addWidget(m_field);
    l->addLayout(frow);
    connect(force, &QPushButton::clicked, this, [this] {
        const int f = trbmon::table().indexOf({}, m_field->currentText());
        if (f < 0) { log(QStringLiteral("Không có trường \"%1\"").arg(m_field->currentText())); return; }
        forSelected([f, this](TrbModel *m) { m->force(f, m_value->value()); });
    });
    connect(unforce, &QPushButton::clicked, this, [this] {
        const int f = trbmon::table().indexOf({}, m_field->currentText());
        if (f >= 0) forSelected([f](TrbModel *m) { m->unforce(f); });
    });

    l->addWidget(new QLabel(QStringLiteral("<b>Trip code</b>")));
    m_tripIndex = new QComboBox;
    for (int i = 0; i < trbmon::kNumTrip; ++i) m_tripIndex->addItem(QStringLiteral("Trip %1 - %2").arg(i + 1).arg(trbmon::tripName(i)));
    l->addWidget(m_tripIndex);
    auto *bits = new QHBoxLayout;
    for (int b = 0; b < 8; ++b) {     // bit 0 (LSB) ở trái, giống ô trip trong app chính
        auto *cb = new QCheckBox(QString::number(b));
        m_tripBits.append(cb);
        bits->addWidget(cb);
    }
    l->addLayout(bits);
    auto updateTips = [this] {
        const int i = m_tripIndex->currentIndex();
        for (int b = 0; b < 8; ++b) {
            const QString n = trbmon::tripBitName(i, b);
            m_tripBits[b]->setToolTip(n.isEmpty() ? QStringLiteral("Bit %1 (chưa dùng)").arg(b) : QStringLiteral("Bit %1: %2").arg(b).arg(n));
        }
    };
    connect(m_tripIndex, QOverload<int>::of(&QComboBox::currentIndexChanged), this, updateTips);
    updateTips();
    auto *trow = new QHBoxLayout;
    auto *setTrip = button(QStringLiteral("Đặt trip"), this);
    auto *clearTrip = button(QStringLiteral("Xóa mọi trip"), this, "secondary");
    trow->addWidget(setTrip);
    trow->addWidget(clearTrip);
    l->addLayout(trow);
    connect(setTrip, &QPushButton::clicked, this, [this] {
        quint8 v = 0;
        for (int b = 0; b < 8; ++b) v |= quint8(m_tripBits[b]->isChecked() << b);
        const int i = m_tripIndex->currentIndex();
        forSelected([i, v](TrbModel *m) { m->setTrip(i, v); });
    });
    connect(clearTrip, &QPushButton::clicked, this, [this] { forSelected([](TrbModel *m) { m->clearTrip(); }); });
    return box;
}

QWidget *SimWindow::buildFaults()
{
    auto *box = new QGroupBox(QStringLiteral("Bơm lỗi đường truyền"));
    auto *form = new QFormLayout(box);
    auto spin = [this](int max, const QString &suffix) {
        auto *s = new QSpinBox;
        s->setRange(0, max);
        s->setSuffix(suffix);
        connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { applyFaults(); });
        return s;
    };
    m_drop = spin(100, " %");
    m_corrupt = spin(100, " %");
    m_delayMin = spin(5000, " ms");
    m_delayMax = spin(5000, " ms");
    m_drop->setToolTip(QStringLiteral("Tỉ lệ bỏ không trả lời"));
    m_corrupt->setToolTip(QStringLiteral("Tỉ lệ trả lời sai CRC"));
    form->addRow(QStringLiteral("Bỏ trả lời"), m_drop);
    form->addRow(QStringLiteral("Sai CRC"), m_corrupt);
    form->addRow(QStringLiteral("Trễ tối thiểu"), m_delayMin);
    form->addRow(QStringLiteral("Trễ tối đa"), m_delayMax);
    return box;
}

void SimWindow::applyFaults()
{
    if (!m_port) return;
    Faults f;
    f.dropPercent = m_drop->value();
    f.corruptPercent = m_corrupt->value();
    f.delayMinMs = m_delayMin->value();
    f.delayMaxMs = qMax(m_delayMin->value(), m_delayMax->value());
    m_port->setFaults(f);
}

void SimWindow::log(const QString &text)
{
    m_log->appendPlainText(QTime::currentTime().toString("HH:mm:ss.zzz") + "  " + text);
}

// ---------------------------------------------------------------- lưu cấu hình

void SimWindow::loadSettings()
{
    QSettings s("actl_sim.ini", QSettings::IniFormat);
    const int mode = s.value("mode", 0).toInt();
    (mode == 1 ? m_connect : mode == 2 ? m_listen : m_serial)->setChecked(true);
    m_ports->setCurrentText(s.value("serial/port", m_ports->currentText()).toString());
    m_baud->setCurrentText(s.value("serial/baud", "1000000").toString());
    m_host->setText(s.value("tcp/host", "192.168.1.10").toString());
    m_connectPort->setValue(s.value("tcp/connectPort", 5000).toInt());
    m_listenPort->setValue(s.value("tcp/listenPort", 5000).toInt());
    m_mb->setValue(s.value("world/mb", 20).toInt());
    m_trbPerMb->setValue(s.value("world/trbPerMb", 8).toInt());
    m_debugPeriod->setValue(s.value("world/debugPeriodMs", 1000).toInt());
}

void SimWindow::saveSettings() const
{
    QSettings s("actl_sim.ini", QSettings::IniFormat);
    s.setValue("mode", m_connect->isChecked() ? 1 : m_listen->isChecked() ? 2 : 0);
    s.setValue("serial/port", m_ports->currentText());
    s.setValue("serial/baud", m_baud->currentText());
    s.setValue("tcp/host", m_host->text());
    s.setValue("tcp/connectPort", m_connectPort->value());
    s.setValue("tcp/listenPort", m_listenPort->value());
    s.setValue("world/mb", m_mb->value());
    s.setValue("world/trbPerMb", m_trbPerMb->value());
    s.setValue("world/debugPeriodMs", m_debugPeriod->value());
}

} // namespace sim
