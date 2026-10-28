#include "calib_page.h"
#include "log_filter.h"
#include "theme.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTime>

namespace ui {

using namespace proto::calib;
using State = core::Transport::State;

namespace {
const char *const kColumns[4] = {"Phase TX", "Phase RX", "Amp TX", "Amp RX"};

// Lưới 4 hàng x 8 ô tích cho 32 kênh (hàng theo byte của bản tin: kênh 0-7, 8-15, 16-23, 24-31), nút chọn hết / bỏ hết đặt ở cột riêng
// bên phải để không làm lệch khoảng cách giữa các hàng.
QGroupBox *channelGroup(const QString &title, QCheckBox *boxes[], const QString &tip)
{
    auto *g = new QGroupBox(title);
    auto *grid = new QGridLayout;
    grid->setVerticalSpacing(10);
    for (int row = 0; row < 4; ++row)
        for (int i = 0; i < 8; ++i) {
            const int c = row * 8 + i;
            boxes[c] = new QCheckBox(QString::number(c + 1));   // hiển thị kênh 1-32 (bit 0-31 của bản tin)
            boxes[c]->setToolTip(tip.arg(c + 1));
            grid->addWidget(boxes[c], row, i);
        }
    auto *all = new QPushButton(QStringLiteral("Chọn hết"));
    auto *none = new QPushButton(QStringLiteral("Bỏ hết"));
    theme::setRole(all, "secondary");
    theme::setRole(none, "secondary");
    QObject::connect(all, &QPushButton::clicked, g, [boxes] { for (int c = 0; c < kChannels; ++c) boxes[c]->setChecked(true); });
    QObject::connect(none, &QPushButton::clicked, g, [boxes] { for (int c = 0; c < kChannels; ++c) boxes[c]->setChecked(false); });
    auto *buttons = new QVBoxLayout;
    buttons->addWidget(all);
    buttons->addWidget(none);
    buttons->addStretch(1);
    auto *l = new QHBoxLayout(g);
    l->addLayout(grid);
    l->addSpacing(12);
    l->addLayout(buttons);
    l->addStretch(1);
    return g;
}

QSpinBox *byteBox()
{
    auto *b = new QSpinBox;
    b->setRange(0, 255);
    b->setAlignment(Qt::AlignRight);
    b->setFixedWidth(90);
    b->setFrame(false);
    b->setFocusPolicy(Qt::StrongFocus);       // lăn chuột trên bảng cuộn trang, chỉ ô đang được chọn mới đổi giá trị theo con lăn
    return b;
}
}

CalibPage::CalibPage(const AppContext &ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    // Địa chỉ đích.
    m_mb = new QComboBox;
    for (int i = 0; i < ctx.settings.mbCount; ++i) m_mb->addItem(QStringLiteral("MB%1").arg(i + 1));
    m_trb = new QComboBox;
    for (int i = 0; i < ctx.settings.trbPerMb; ++i) m_trb->addItem(QStringLiteral("TRB%1").arg(i + 1));
    auto *prev = new QPushButton(QStringLiteral("◀ Trước"));
    auto *next = new QPushButton(QStringLiteral("Sau ▶"));
    theme::setRole(prev, "secondary");
    theme::setRole(next, "secondary");
    connect(prev, &QPushButton::clicked, this, [this] { step(-1); });
    connect(next, &QPushButton::clicked, this, [this] { step(+1); });
    auto *addr = new QHBoxLayout;
    addr->addWidget(new QLabel(QStringLiteral("Gửi tới")));
    addr->addWidget(m_mb);
    addr->addWidget(m_trb);
    addr->addWidget(prev);
    addr->addWidget(next);
    addr->addStretch(1);

    auto *rx = channelGroup(QStringLiteral("RX CHN EN"), m_rx, QStringLiteral("Bật kênh RX %1"));
    auto *tx = channelGroup(QStringLiteral("TX CHN EN"), m_tx, QStringLiteral("Bật kênh TX %1"));

    // Bảng giá trị từng kênh.
    m_table = new QTableWidget(kChannels, 4);
    m_table->setHorizontalHeaderLabels({QString::fromUtf8(kColumns[0]), QString::fromUtf8(kColumns[1]), QString::fromUtf8(kColumns[2]), QString::fromUtf8(kColumns[3])});
    QStringList rows;
    for (int c = 0; c < kChannels; ++c) rows << QStringLiteral("CH%1").arg(c + 1);   // hiển thị kênh 1-32
    m_table->setVerticalHeaderLabels(rows);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->verticalHeader()->setDefaultSectionSize(38);   // đủ cao để ô nhập hiện đủ cả hai nút tăng giảm
    m_table->setAlternatingRowColors(false);
    for (int c = 0; c < kChannels; ++c)
        for (int k = 0; k < 4; ++k) {
            m_cell[c][k] = byteBox();
            // Ô nhập hẹp, đặt giữa cột: bảng vẫn trải đều hai mép màn hình, khoảng trống giữa các ô là chỗ lăn chuột để cuộn các kênh 1-32.
            auto *holder = new QWidget;
            auto *hl = new QHBoxLayout(holder);
            hl->setContentsMargins(0, 0, 0, 0);
            hl->addStretch(1);
            hl->addWidget(m_cell[c][k]);
            hl->addStretch(1);
            m_table->setCellWidget(c, k, holder);
        }
    m_table->setMinimumHeight(360);

    // Nút.
    auto *import = new QPushButton(QStringLiteral("Nhập file…"));
    auto *exp = new QPushButton(QStringLiteral("Xuất file…"));
    theme::setRole(import, "secondary");
    theme::setRole(exp, "secondary");
    auto *clear = new QPushButton(QStringLiteral("Xóa"));
    theme::setRole(clear, "secondary");
    clear->setToolTip(QStringLiteral("Xóa các giá trị hiện tại: bỏ tích mọi kênh RX/TX và đặt mọi giá trị về 0"));
    connect(clear, &QPushButton::clicked, this, [this] { setData(Data()); m_result->setText(QStringLiteral("Đã xóa các giá trị")); });
    connect(import, &QPushButton::clicked, this, &CalibPage::importFile);
    connect(exp, &QPushButton::clicked, this, &CalibPage::exportFile);
    m_send = new QPushButton(QStringLiteral("Gửi bản tin Calib"));
    connect(m_send, &QPushButton::clicked, this, &CalibPage::send);
    m_result = new QLabel;
    theme::setRole(m_result, "muted");
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(import);
    buttons->addWidget(exp);
    buttons->addWidget(clear);
    buttons->addSpacing(16);
    buttons->addWidget(m_send);
    buttons->addWidget(m_result, 1);

    m_hint = new QLabel(QStringLiteral("Chưa mở cổng COM (RS485): bấm thẻ kết nối ở góc trên bên trái, chọn dòng RS485, chọn cổng và mở. Bản tin Calib gửi qua đường này."));
    theme::setRole(m_hint, "muted");

    auto *l = new QVBoxLayout(this);
    l->addWidget(m_hint);
    l->addLayout(addr);
    auto *masks = new QHBoxLayout;
    masks->addWidget(rx, 1);
    masks->addWidget(tx, 1);
    l->addLayout(masks);
    l->addWidget(m_table, 1);
    l->addLayout(buttons);

    connect(ctx.serviceLink, &core::Link::stateChanged, this, &CalibPage::showState);
    showState(State::Closed);
}

Data CalibPage::data() const
{
    Data d;
    for (int c = 0; c < kChannels; ++c) {
        if (m_rx[c]->isChecked()) d.rxEn |= quint32(1) << c;
        if (m_tx[c]->isChecked()) d.txEn |= quint32(1) << c;
        d.phaseTx[c] = quint8(m_cell[c][0]->value());
        d.phaseRx[c] = quint8(m_cell[c][1]->value());
        d.ampTx[c] = quint8(m_cell[c][2]->value());
        d.ampRx[c] = quint8(m_cell[c][3]->value());
    }
    return d;
}

void CalibPage::setData(const Data &d)
{
    for (int c = 0; c < kChannels; ++c) {
        m_rx[c]->setChecked(d.rxEn >> c & 1);
        m_tx[c]->setChecked(d.txEn >> c & 1);
        m_cell[c][0]->setValue(d.phaseTx[c]);
        m_cell[c][1]->setValue(d.phaseRx[c]);
        m_cell[c][2]->setValue(d.ampTx[c]);
        m_cell[c][3]->setValue(d.ampRx[c]);
    }
}

void CalibPage::target(int &mb, int &trb) const
{
    mb = m_mb->currentIndex();
    trb = m_trb->currentIndex();
}

// Trước / Sau: chuyển lần lượt qua các TRB (hết TRB của MB này thì sang MB kế), quay vòng như trang giám sát.
void CalibPage::step(int delta)
{
    const int per = m_trb->count(), n = m_mb->count() * per;
    const int i = (m_mb->currentIndex() * per + m_trb->currentIndex() + delta + n) % n;
    m_mb->setCurrentIndex(i / per);
    m_trb->setCurrentIndex(i % per);
}

void CalibPage::showState(State s)
{
    m_state = s;
    m_send->setEnabled(s == State::Connected);
    m_hint->setVisible(s != State::Connected);
}

void CalibPage::send()
{
    int mb, trb;
    target(mb, trb);
    const QString name = QStringLiteral("MB%1 / TRB%2").arg(mb + 1).arg(trb + 1);
    if (QMessageBox::question(this, QStringLiteral("Gửi Calib"), QStringLiteral("Gửi bản tin Calib cho %1?").arg(name)) != QMessageBox::Yes) return;
    m_ctx.trbCalib->send(mb, trb, data());
    m_result->setText(QStringLiteral("Đã gửi lúc %1 tới %2").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), name));
}

void CalibPage::importFile()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Nhập file Calib"), QString(), QStringLiteral("Calib (*.json);;Tất cả (*)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { QMessageBox::warning(this, QStringLiteral("Nhập file Calib"), QStringLiteral("Không đọc được file: %1").arg(f.errorString())); return; }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (doc.isNull() || !doc.isObject()) { QMessageBox::warning(this, QStringLiteral("Nhập file Calib"), QStringLiteral("File không phải JSON hợp lệ: %1").arg(pe.errorString())); return; }
    Data d;
    const QString error = fromJson(doc.object(), d);
    if (!error.isEmpty()) { QMessageBox::warning(this, QStringLiteral("Nhập file Calib"), error); return; }
    setData(d);
    m_result->setText(QStringLiteral("Đã nhập %1").arg(QFileInfo(path).fileName()));
}

void CalibPage::exportFile()
{
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Xuất file Calib"), QStringLiteral("calib.json"), QStringLiteral("Calib (*.json)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { QMessageBox::warning(this, QStringLiteral("Xuất file Calib"), QStringLiteral("Không ghi được file: %1").arg(f.errorString())); return; }
    f.write(QJsonDocument(toJson(data())).toJson(QJsonDocument::Indented));
    m_result->setText(QStringLiteral("Đã xuất %1").arg(QFileInfo(path).fileName()));
}

} // namespace ui
