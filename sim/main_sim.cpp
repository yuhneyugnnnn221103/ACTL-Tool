// actl_sim: chương trình giả lập TRB, cắm vào phía thiết bị của Gateway (COM RS485 hoặc Ethernet).
#include "bus_port.h"
#include "../core/serial_transport.h"
#include "../core/tcp_client_transport.h"
#include "../core/tcp_server_transport.h"
#include "../proto/trb_monitor_proto.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QProcess>
#include <QTextStream>
#include <QThread>
#include <QTime>
#include <iostream>

namespace {

// Đọc stdin ở thread riêng (QSocketNotifier không dùng được cho console Windows).
class ConsoleReader : public QThread {
    Q_OBJECT
signals:
    void line(const QString &text);
protected:
    void run() override
    {
        std::string s;
        while (std::getline(std::cin, s)) emit line(QString::fromStdString(s));
    }
};

QTextStream &out()
{
    static QTextStream s(stdout);
    return s;
}

void log(const QString &text)
{
    out() << QTime::currentTime().toString("HH:mm:ss.zzz") << ' ' << text << Qt::endl;
}

QString hex(const QByteArray &b) { return QString::fromLatin1(b.toHex(' ').toUpper()); }

// "*" = mọi giá trị; trả về khoảng [lo, hi].
bool range(const QString &s, int count, int &lo, int &hi)
{
    if (s == "*") { lo = 0; hi = count - 1; return count > 0; }
    bool ok = false;
    const int v = s.toInt(&ok);
    lo = hi = v;
    return ok && v >= 0 && v < count;
}

const char *kHelp =
    "Lệnh:\n"
    "  status                         thống kê\n"
    "  lost MB TRB | back MB TRB      TRB mất kết nối / trở lại (MB, TRB có thể là *)\n"
    "  over MB TRB \"TÊN TRƯỜNG\" N     ép giá trị trường giám sát, ví dụ over 0 1 \"TRM1.I SEN2\" 2500\n"
    "  normal MB TRB                  bỏ mọi giá trị ép, xóa trip, trở lại bình thường\n"
    "  trip MB TRB N BITS             đặt byte trip code N (1..16), BITS dạng số hoặc 0xNN\n"
    "  faults drop|corrupt PERCENT    bơm lỗi trả lời: bỏ hoặc sai CRC\n"
    "  faults delay MIN MAX           trễ trả lời MIN..MAX ms\n"
    "  debugperiod MS                 chu kỳ TRB debug tự gửi khung giám sát (0 = tắt)\n"
    "  help | quit";

} // namespace

#include "main_sim.moc"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("actl_sim");

    QCommandLineParser p;
    p.setApplicationDescription("Giả lập TRB cắm vào phía thiết bị của Gateway (RS485 hoặc Ethernet).");
    p.addHelpOption();
    p.addOption({"serial", "Cổng COM nối tới Gateway, ví dụ COM5 hoặc /dev/ttyUSB0.", "port"});
    p.addOption({"baud", "Tốc độ baud (mặc định 1000000).", "baud", "1000000"});
    p.addOption({"connect", "Nối tới Gateway qua TCP, dạng host:port.", "host:port"});
    p.addOption({"listen", "Lắng nghe TCP để Gateway nối vào, dạng port.", "port"});
    p.addOption({"mb", "Số MB (mặc định 20).", "n", "20"});
    p.addOption({"trb-per-mb", "Số TRB mỗi MB (mặc định 8).", "n", "8"});
    p.addOption({"seed", "Hạt giống ngẫu nhiên, để lặp lại được.", "n", "1"});
    p.addOption({"debug-period", "Chu kỳ TRB debug tự gửi khung giám sát, ms (mặc định 1000).", "ms", "1000"});
    p.addOption({"drop", "Tỉ lệ % bỏ không trả lời.", "percent", "0"});
    p.addOption({"corrupt", "Tỉ lệ % trả lời sai CRC.", "percent", "0"});
    p.addOption({"delay", "Trễ trả lời, dạng MIN..MAX ms.", "min..max", "0..0"});
    p.addOption({"no-crc", "Không kiểm CRC khung nhận từ Gateway."});
    p.addOption({"hex", "In mọi khung nhận/gửi dạng hex."});
    p.process(app);

    const int nSource = int(p.isSet("serial")) + int(p.isSet("connect")) + int(p.isSet("listen"));
    if (nSource != 1) {
        std::cerr << "Cần đúng một trong --serial, --connect, --listen. Xem --help.\n";
        return 2;
    }

    sim::SimWorld world(p.value("mb").toInt(), p.value("trb-per-mb").toInt(), p.value("seed").toUInt());
    world.setDebugPeriodMs(p.value("debug-period").toInt());

    core::Transport *transport = nullptr;
    if (p.isSet("serial")) {
        transport = new core::SerialTransport(p.value("serial"), p.value("baud").toInt());
    } else if (p.isSet("connect")) {
        const QString hp = p.value("connect");
        const int colon = hp.lastIndexOf(':');
        bool ok = false;
        const int port = hp.mid(colon + 1).toInt(&ok);
        if (colon <= 0 || !ok) { std::cerr << "--connect cần dạng host:port\n"; return 2; }
        transport = new core::TcpClientTransport(hp.left(colon), quint16(port));
    } else {
        bool ok = false;
        const int port = p.value("listen").toInt(&ok);
        if (!ok) { std::cerr << "--listen cần số cổng\n"; return 2; }
        transport = new core::TcpServerTransport(QHostAddress::Any, quint16(port));
    }

    sim::BusPort bus(transport, &world, !p.isSet("no-crc"), p.value("seed").toUInt());
    sim::Faults faults;
    faults.dropPercent = p.value("drop").toInt();
    faults.corruptPercent = p.value("corrupt").toInt();
    const QStringList d = p.value("delay").split("..");
    faults.delayMinMs = d.value(0).toInt();
    faults.delayMaxMs = qMax(faults.delayMinMs, d.value(1, d.value(0)).toInt());
    bus.setFaults(faults);

    QObject::connect(&bus, &sim::BusPort::message, [](const QString &m) { log(m); });
    if (p.isSet("hex")) {
        QObject::connect(&bus, &sim::BusPort::received, [](const QByteArray &b) { log("RX " + hex(b)); });
        QObject::connect(&bus, &sim::BusPort::sent, [](const QByteArray &b) { log("TX " + hex(b)); });
    }

    auto forEach = [&](const QString &mbArg, const QString &trbArg, const std::function<void(sim::TrbModel *)> &fn) {
        int m0, m1, t0, t1;
        if (!range(mbArg, world.mbCount(), m0, m1) || !range(trbArg, world.trbPerMb(), t0, t1)) {
            log("Địa chỉ không hợp lệ (MB 0.." + QString::number(world.mbCount() - 1) + ", TRB 0.."
                + QString::number(world.trbPerMb() - 1) + ")");
            return;
        }
        for (int mb = m0; mb <= m1; ++mb)
            for (int t = t0; t <= t1; ++t) fn(world.trb(mb, t));
    };

    auto command = [&](const QString &text) {
        const QStringList a = QProcess::splitCommand(text.trimmed());
        if (a.isEmpty()) return;
        const QString c = a.first().toLower();
        if (c == "help") {
            out() << kHelp << Qt::endl;
        } else if (c == "quit" || c == "exit") {
            QCoreApplication::quit();
        } else if (c == "status") {
            const auto &s = world.stats();
            const auto &b = bus.stats();
            const auto &r = bus.rxStats();
            log(QString("%1 | hỏi giám sát %2, điều khiển %3, beam %4, đọc cấu hình %5, ghi cấu hình %6, bỏ qua %7 | "
                        "TRB debug %8, xung đột debug %9 | nhận %10 khung, trả lời %11, bỏ %12, sai CRC cố ý %13 | "
                        "khung lỗi CRC %14, byte bỏ qua %15")
                    .arg(bus.transport()->describe()).arg(s.polls).arg(s.controls).arg(s.beams).arg(s.configReads)
                    .arg(s.configWrites).arg(s.ignored).arg(world.debugCount()).arg(s.debugConflicts)
                    .arg(b.requests).arg(b.replies).arg(b.dropped).arg(b.corrupted).arg(r.crcErrors).arg(r.droppedBytes));
        } else if ((c == "lost" || c == "back" || c == "normal") && a.size() == 3) {
            forEach(a[1], a[2], [&](sim::TrbModel *m) {
                if (c == "lost") m->setOnline(false);
                else { m->setOnline(true); if (c == "normal") { m->clearForced(); m->clearTrip(); } }
            });
        } else if (c == "over" && a.size() == 5) {
            const int field = proto::trbmon::table().indexOf({}, a[3]);
            bool ok = false;
            const double v = a[4].toDouble(&ok);
            if (field < 0 || !ok) { log("Không có trường \"" + a[3] + "\" hoặc giá trị sai"); return; }
            forEach(a[1], a[2], [&](sim::TrbModel *m) { m->force(field, v); });
        } else if (c == "trip" && a.size() == 5) {
            bool ok1 = false, ok2 = false;
            const int n = a[3].toInt(&ok1);
            const uint bits = a[4].toUInt(&ok2, 0);
            if (!ok1 || !ok2 || n < 1 || n > 16 || bits > 0xFF) { log("trip: N = 1..16, BITS = 0..255"); return; }
            forEach(a[1], a[2], [&](sim::TrbModel *m) { m->setTrip(n - 1, quint8(bits)); });
        } else if (c == "faults" && a.size() >= 3) {
            sim::Faults f = bus.faults();
            if (a[1] == "drop") f.dropPercent = a[2].toInt();
            else if (a[1] == "corrupt") f.corruptPercent = a[2].toInt();
            else if (a[1] == "delay" && a.size() == 4) { f.delayMinMs = a[2].toInt(); f.delayMaxMs = qMax(f.delayMinMs, a[3].toInt()); }
            else { log("faults drop|corrupt PERCENT | faults delay MIN MAX"); return; }
            bus.setFaults(f);
            log(QString("Lỗi bơm: bỏ %1%, sai CRC %2%, trễ %3..%4 ms").arg(f.dropPercent).arg(f.corruptPercent)
                    .arg(f.delayMinMs).arg(f.delayMaxMs));
        } else if (c == "debugperiod" && a.size() == 2) {
            world.setDebugPeriodMs(a[1].toInt());
        } else {
            log("Lệnh không hiểu, gõ help");
        }
    };

    ConsoleReader reader;
    QObject::connect(&reader, &ConsoleReader::line, &app, command);
    reader.start();

    log(QString("actl_sim: %1 MB x %2 TRB, %3").arg(world.mbCount()).arg(world.trbPerMb()).arg(transport->describe()));
    log("Gõ help để xem lệnh.");
    bus.open();
    const int rc = app.exec();
    bus.close();
    reader.terminate();
    reader.wait(500);
    delete transport;
    return rc;
}
