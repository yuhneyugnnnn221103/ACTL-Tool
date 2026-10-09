// Test phần TRB: bảng trường và khung bản tin, tách khung, giám sát, cảnh báo, điều khiển, cấu hình
// (qua Link với TRB giả lập), ngưỡng, log CSV.
#include "../core/frame_parser.h"
#include "../core/link.h"
#include "../model/device_store.h"
#include "../model/thresholds.h"
#include "../proto/trb_config_proto.h"
#include "../proto/trb_control_proto.h"
#include "../proto/trb_monitor_proto.h"
#include "../services/alarm_engine.h"
#include "../services/csv_logger.h"
#include "../services/trb_config.h"
#include "../services/trb_control.h"
#include "../services/trb_monitor.h"
#include "../proto/psu_config_proto.h"
#include "../proto/psu_control_proto.h"
#include "../proto/psu_monitor_proto.h"
#include "../proto/stm_ota_proto.h"
#include "../ui/log_filter.h"
#include "test_support.h"
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace proto;
using testsupport::FakeTransport;
using testsupport::crcOk;

namespace {

QByteArray monitorFrame(int mb, int trb, const QList<double> &values)
{
    QByteArray f(trbmon::kLength, 0);
    f[2] = f[3] = char(0x11);
    f[trbmon::kOffMb] = char(mb);
    f[trbmon::kOffTrb] = char(trb);
    trbmon::table().encode(values, f);
    core::seal(f, trbmon::kCrcStart);
    return f;
}

QByteArray configReply(int mb, int trb, const QList<double> &values)
{
    QByteArray f(trbcfg::kLength, 0);
    f[2] = f[3] = char(0xA4);
    f[trbcfg::kOffMb] = char(mb);
    f[trbcfg::kOffTrb] = char(trb);
    trbcfg::table().encode(values, f);
    core::seal(f, 4);
    return f;
}

QList<double> zeros(int n) { return QList<double>(n, 0.0); }

// TRB giả lập trên RS485: nhớ cấu hình đã ghi, trả lời lệnh đọc.
struct FakeTrb {
    QHash<int, QList<double>> config;     // khóa mb << 8 | trb
    int corruptField = -1;                // >= 0: lưu sai trường này để thử đọc lại không khớp
    int writes = 0;

    static int key(int mb, int trb) { return mb << 8 | trb; }

    void attach(FakeTransport *t)
    {
        t->onWrite = [this, t](const QByteArray &d) {
            const int mb = quint8(d.at(4)), trb = quint8(d.at(5));
            if (quint8(d.at(2)) == 0xA1) {
                ++writes;
                config[key(mb, trb)] = trbcfg::table().decode(d);
                if (corruptField >= 0) config[key(mb, trb)][corruptField] += 1;
            } else if (quint8(d.at(2)) == 0xA3 && config.contains(key(mb, trb))) {
                t->injectLater(configReply(mb, trb, config.value(key(mb, trb))));
            }
        };
    }
};

struct Rig {
    core::FrameRegistry registry;
    FakeTransport *transport = new FakeTransport;
    core::Link link;
    explicit Rig(const QString &name = "link") : link(name, transport, &registry) {}
};
}

class TrbTest : public QObject {
    Q_OBJECT
private slots:
    // ---------- Bố cục bản tin ----------
    void monitorTableLayout()
    {
        const FieldTable &t = trbmon::table();
        QCOMPARE(t.size(), trbmon::kNumFields);
        int end = 6;
        for (const Field &f : t.fields()) { QCOMPARE(f.offset, end); end += f.size; }
        QCOMPARE(end, trbmon::kLength - 4 - 37);              // 37 byte dự phòng rồi CRC, tailer
        QCOMPARE(t.fields().at(trbmon::kIdxTrm0).name, QStringLiteral("TRM1.I SEN1"));
        QCOMPARE(t.fields().at(trbmon::kIdxTrm0 + trbmon::kTrmFields).offset, 6 + 50);     // TRM2 sau 50 byte
        QCOMPARE(t.fields().at(trbmon::kIdxTrbV).offset, 6 + 4 * 50);
        QCOMPARE(t.fields().at(trbmon::kIdxTrip0).offset, 6 + 4 * 50 + 6);
        QCOMPARE(t.fields().at(trbmon::kIdxInitAdar).offset, 6 + 4 * 50 + 6 + 16 + 4);
        QCOMPARE(t.fields().at(trbmon::kIdxHumidity).size, 2);
    }

    void configTableLayout()
    {
        const FieldTable &t = trbcfg::table();
        int end = 6;
        for (const Field &f : t.fields()) { QCOMPARE(f.offset, end); end += f.size; }
        QCOMPARE(end, trbcfg::kLength - 4 - 37);
        QCOMPARE(t.fields().at(t.indexOf("CAL", "dy")).offset, 6);
        QCOMPARE(t.fields().at(t.indexOf("ADAR 1", "CONFIG_RESET")).offset, 6 + 9);
        QCOMPARE(t.fields().at(t.indexOf("ADAR 2", "CONFIG_RESET")).offset, 6 + 9 + 25);
        QCOMPARE(t.fields().at(t.indexOf("LUT", "TEMP_MAX_LUT")).offset, 6 + 9 + 8 * 25);
        QCOMPARE(t.fields().at(t.indexOf("TRM 1", "I_SEN_1_MAX")).offset, 6 + 9 + 8 * 25 + 28);
        QCOMPARE(t.fields().at(t.indexOf("TRM 2", "I_SEN_1_MAX")).offset, 6 + 9 + 8 * 25 + 28 + 48);
        QCOMPARE(t.fields().at(t.indexOf("GENERAL", "TEMP_TRB_MAX")).offset, 6 + 9 + 8 * 25 + 28 + 4 * 48);
        QCOMPARE(t.fields().at(t.indexOf("DEBUG", "PERIOD_TR_DEBUG")).size, 4);
    }

    void thresholdMapPointsAtRealFields()
    {
        const auto &map = trbcfg::thresholdMap();
        QCOMPARE(map.size(), 4 * (8 + 4 + 4) + 4);   // 4 TRM x (I_SEN + I_SEN_PA + TEMP) + V, I, nhiệt power, nhiệt MCU
        QSet<int> seen;
        for (const auto &m : map) {
            QVERIFY(m.monitorField >= 0 && m.monitorField < trbmon::kNumFields);
            QVERIFY(!seen.contains(m.monitorField));
            seen.insert(m.monitorField);
            QVERIFY(trbcfg::table().fields().at(m.cfgMax).name.endsWith("_MAX"));
            QVERIFY(trbcfg::table().fields().at(m.cfgMin).name.endsWith("_MIN"));
        }
        QVERIFY(seen.contains(trbmon::kIdxTrbV) && seen.contains(trbmon::kIdxMcuTemp));
        QVERIFY(!seen.contains(trbmon::kIdxTrip0));
        // TEMP_TRB dùng chung cho nhiệt độ TRM1..4 (TEMP1..4 của mỗi TRM).
        const int tMax = trbcfg::table().indexOf("GENERAL", "TEMP_TRB_MAX"), tMin = trbcfg::table().indexOf("GENERAL", "TEMP_TRB_MIN");
        int shared = 0;
        for (const auto &m : map) {
            if (m.cfgMax != tMax) continue;
            QCOMPARE(m.cfgMin, tMin);
            QVERIFY(trbmon::table().fields().at(m.monitorField).name.contains("TEMP"));
            ++shared;
        }
        QCOMPARE(shared, 16);
    }

    void encodeDecodeRoundTripIsBigEndian()
    {
        QList<double> v = zeros(trbmon::kNumFields);
        v[trbmon::kIdxTrbV] = 0x1234;
        v[trbmon::kIdxPa] = 0x0A;
        v[trbmon::kIdxTrip0 + 15] = 0xFF;
        const QByteArray f = monitorFrame(3, 5, v);
        QCOMPARE(quint8(f.at(206)), quint8(0x12));
        QCOMPARE(quint8(f.at(207)), quint8(0x34));
        QCOMPARE(trbmon::table().decode(f), v);
    }

    void tripCodeNames()
    {
        // Trip 1-4: vượt max I SEN của TRM1..4; bit 0 = I SEN1, bit 7 = I SEN8
        QCOMPARE(trbmon::tripName(0), QStringLiteral("Max I SEN TRM1"));
        QCOMPARE(trbmon::tripName(3), QStringLiteral("Max I SEN TRM4"));
        QCOMPARE(trbmon::tripBitName(1, 0), QStringLiteral("TRM2 I SEN1"));
        QCOMPARE(trbmon::tripBitName(2, 7), QStringLiteral("TRM3 I SEN8"));
        // Trip 5: PA của TRM1 (bit 0-3) và TRM2 (bit 4-7); Trip 6: TRM3 và TRM4
        QCOMPARE(trbmon::tripName(4), QStringLiteral("Max PA TRM1-2"));
        QCOMPARE(trbmon::tripBitName(4, 0), QStringLiteral("TRM1 PA1"));
        QCOMPARE(trbmon::tripBitName(4, 3), QStringLiteral("TRM1 PA4"));
        QCOMPARE(trbmon::tripBitName(4, 4), QStringLiteral("TRM2 PA1"));
        QCOMPARE(trbmon::tripBitName(4, 7), QStringLiteral("TRM2 PA4"));
        QCOMPARE(trbmon::tripName(5), QStringLiteral("Max PA TRM3-4"));
        QCOMPARE(trbmon::tripBitName(5, 5), QStringLiteral("TRM4 PA2"));
        // Trip 7: system
        QCOMPARE(trbmon::tripName(6), QStringLiteral("System"));
        QCOMPARE(trbmon::tripBitName(6, 0), QStringLiteral("TRM temp max"));
        QCOMPARE(trbmon::tripBitName(6, 1), QStringLiteral("TRM temp min"));
        QCOMPARE(trbmon::tripBitName(6, 2), QStringLiteral("V TRB trip max"));
        QCOMPARE(trbmon::tripBitName(6, 3), QStringLiteral("V TRB trip min"));
        QCOMPARE(trbmon::tripBitName(6, 4), QStringLiteral("I TRB trip max"));
        QCOMPARE(trbmon::tripBitName(6, 5), QStringLiteral("I TRB trip min"));
        QVERIFY(trbmon::tripBitName(6, 6).isEmpty() && trbmon::tripBitName(6, 7).isEmpty());
        // Trip 10-15: như 1-6 nhưng dưới min
        QCOMPARE(trbmon::tripName(9), QStringLiteral("Min I SEN TRM1"));
        QCOMPARE(trbmon::tripName(12), QStringLiteral("Min I SEN TRM4"));
        QCOMPARE(trbmon::tripBitName(12, 7), QStringLiteral("TRM4 I SEN8"));
        QCOMPARE(trbmon::tripName(13), QStringLiteral("Min PA TRM1-2"));
        QCOMPARE(trbmon::tripName(14), QStringLiteral("Min PA TRM3-4"));
        QCOMPARE(trbmon::tripBitName(14, 6), QStringLiteral("TRM4 PA3"));
        // Trip 8, 9, 16 dự phòng
        for (int i : {7, 8, 15}) {
            QCOMPARE(trbmon::tripName(i), QStringLiteral("Dự phòng"));
            QVERIFY(trbmon::tripBitName(i, 0).isEmpty());
        }
        QSet<QString> names;                                    // 13 trip có nghĩa, tên không trùng nhau
        for (int i = 0; i < trbmon::kNumTrip; ++i)
            if (trbmon::tripName(i) != QStringLiteral("Dự phòng")) names.insert(trbmon::tripName(i));
        QCOMPARE(names.size(), 13);
    }

    // ---------- Khung điều khiển ----------
    void controlFrame()
    {
        trbctl::ControlCmd c;
        c.paMask = 0x15;       // bit cao hơn 4 bị loại
        c.clearTrip = true;
        c.start = true;
        c.debugMode = true;
        const QByteArray f = trbctl::buildControl(7, 3, c);
        QCOMPARE(f.size(), 14);
        QCOMPARE(f.left(8).toHex(), QByteArray("abcda2a2" "0703" "05" "13"));   // PA=5, clear|start|debug
        QCOMPARE(f.right(2).toHex(), QByteArray("e1e2"));
        QVERIFY(crcOk(f, 4));

        trbctl::ControlCmd stop;                                   // mặc định: Stop, Normal, không xóa trip
        QCOMPARE(quint8(trbctl::buildControl(0, 0, stop).at(7)), quint8(0));
        trbctl::ControlCmd sync;
        sync.beamSync = true;
        QCOMPARE(quint8(trbctl::buildControl(0, 0, sync).at(7)), quint8(0x04));
    }

    void beamFrameAndBroadcast()
    {
        trbctl::BeamCmd b;
        b.phaseTx = 1; b.phaseRx = 2; b.ampTx = 3; b.ampRx = 4; b.chMask = 0xFA; b.adarMask = 0x81;
        const QByteArray f = trbctl::buildBeam(trbctl::kBroadcast, trbctl::kBroadcast, b);
        QCOMPARE(f.size(), 17);
        QCOMPARE(f.left(12).toHex(), QByteArray("abcd1414" "ffff" "01020304" "0a" "81"));  // CH chỉ 4 bit thấp
        QVERIFY(crcOk(f, 4));
    }

    void controlServiceSendsAndReports()
    {
        Rig rig("monitor");
        rig.link.start();
        services::TrbControl control(&rig.link);
        QSignalSpy done(&control, &services::TrbControl::commandFinished);

        trbctl::ControlCmd c;
        c.paMask = 0x03;
        c.start = true;
        control.sendControl(2, 4, c);
        QVERIFY(done.wait(1000));
        QCOMPARE(done.last().at(1).toBool(), true);
        QVERIFY(done.last().at(0).toString().contains("MB2 / TRB4"));
        QVERIFY(done.last().at(0).toString().contains("Start"));
        QCOMPARE(rig.transport->written.size(), 1);
        QCOMPARE(rig.transport->written.first(), trbctl::buildControl(2, 4, c));

        control.sendControl(trbctl::kBroadcast, trbctl::kBroadcast, {});
        QVERIFY(done.wait(1000));
        QVERIFY(done.last().at(0).toString().contains("tất cả TRB"));

        rig.transport->close();                                 // chưa kết nối Gateway: báo không gửi được
        control.sendControl(0, 0, c);
        QVERIFY(done.wait(1000));
        QCOMPARE(done.last().at(1).toBool(), false);
    }

    void onlyOneTrbMayBeInDebug()
    {
        using Check = services::TrbControl::Check;
        Rig rig("monitor");
        rig.link.start();
        services::TrbControl control(&rig.link);
        QSignalSpy done(&control, &services::TrbControl::commandFinished);
        QSignalSpy changed(&control, &services::TrbControl::debugTrbChanged);
        auto settle = [&](int n) { while (done.size() < n) QVERIFY(done.wait(1000)); };

        trbctl::ControlCmd debug;
        debug.debugMode = true;
        debug.paMask = 0x03;
        debug.start = true;
        trbctl::ControlCmd normal;
        QVERIFY(!control.debugTrb().has_value());

        QCOMPARE(control.sendControl(1, 1, debug), Check::Ok);          // TRB đầu tiên được Debug
        settle(1);
        QCOMPARE(control.debugTrb().value(), services::TrbControl::Device(1, 1));
        QCOMPARE(changed.size(), 1);

        const int sent = rig.transport->written.size();
        QCOMPARE(control.sendControl(2, 2, debug), Check::OtherInDebug); // TRB thứ hai bị từ chối, không gửi gì
        QCOMPARE(control.check(2, 2, debug), Check::OtherInDebug);
        QCOMPARE(control.check(2, 2, normal), Check::Ok);               // Normal cho TRB khác vẫn được
        QCOMPARE(control.sendControl(trbctl::kBroadcast, trbctl::kBroadcast, debug), Check::BroadcastDebug);
        QCOMPARE(control.sendControl(0, trbctl::kBroadcast, debug), Check::BroadcastDebug);   // broadcast theo từng MB cũng vậy
        QCOMPARE(rig.transport->written.size(), sent);
        QCOMPARE(control.debugTrb().value(), services::TrbControl::Device(1, 1));

        QCOMPARE(control.sendControl(1, 1, debug), Check::Ok);          // gửi lại cho chính TRB đang Debug thì được
        settle(2);

        // Chuyển Debug sang TRB khác: TRB cũ về Normal (giữ PA, Start), rồi TRB mới sang Debug.
        QCOMPARE(control.sendControlSwitchDebug(2, 2, debug), Check::Ok);
        settle(4);
        QCOMPARE(control.debugTrb().value(), services::TrbControl::Device(2, 2));
        const QList<QByteArray> w = rig.transport->written;
        trbctl::ControlCmd released = debug;
        released.debugMode = false;
        QCOMPARE(w.at(w.size() - 2), trbctl::buildControl(1, 1, released));
        QCOMPARE(w.at(w.size() - 1), trbctl::buildControl(2, 2, debug));
        QCOMPARE(quint8(w.at(w.size() - 2).at(6)), quint8(0x03));       // PA không bị tắt khi thoát Debug
        QCOMPARE(quint8(w.at(w.size() - 2).at(7)), quint8(0x02));       // Start giữ nguyên, không còn bit debug

        QCOMPARE(control.sendControl(2, 2, normal), Check::Ok);         // Normal cho TRB Debug: hết Debug
        settle(5);
        QVERIFY(!control.debugTrb().has_value());
        QCOMPARE(control.sendControl(3, 3, debug), Check::Ok);          // giờ TRB khác được Debug
        settle(6);
        QCOMPARE(control.debugTrb().value(), services::TrbControl::Device(3, 3));
        QCOMPARE(control.sendControl(trbctl::kBroadcast, trbctl::kBroadcast, normal), Check::Ok);   // broadcast Normal: hết Debug
        settle(7);
        QVERIFY(!control.debugTrb().has_value());
    }

    void debugStateIgnoresCommandThatWasNotSent()
    {
        Rig rig("monitor");                    // chưa mở link: lệnh không ra đường truyền
        services::TrbControl control(&rig.link);
        QSignalSpy done(&control, &services::TrbControl::commandFinished);
        trbctl::ControlCmd debug;
        debug.debugMode = true;
        control.sendControl(1, 1, debug);
        QVERIFY(done.wait(1000));
        QCOMPARE(done.last().at(1).toBool(), false);
        QVERIFY(!control.debugTrb().has_value());
    }

    void logFramesAreGroupedByCmd()
    {
        using namespace ui;
        auto cls = [](const QByteArray &f) { return classifyFrame(f); };

        // TRB: giám sát, điều khiển, beam có địa chỉ MB/TRB để trang chi tiết lọc theo TRB đang xem.
        FrameClass c = cls(monitorFrame(7, 3, zeros(trbmon::kNumFields)));
        QCOMPARE(c.group, int(LogTrbMonitor));
        QCOMPARE(c.mb, 7);
        QCOMPARE(c.trb, 3);
        c = cls(trbctl::buildControl(4, 5, {}));
        QCOMPARE(c.group, int(LogTrbMonitor));
        QCOMPARE(c.mb, 4);
        c = cls(trbctl::buildBeam(2, 6, {}));
        QCOMPARE(c.group, int(LogTrbMonitor));
        QCOMPARE(c.trb, 6);
        QCOMPARE(cls(trbcfg::buildReadRequest(1, 1)).group, int(LogTrbConfig));
        QCOMPARE(cls(trbcfg::buildWrite(1, 1, zeros(trbcfg::table().size()))).group, int(LogTrbConfig));
        QCOMPARE(cls(configReply(1, 1, zeros(trbcfg::table().size()))).group, int(LogTrbConfig));

        // PSU
        QCOMPARE(cls(psuctl::buildControl(2, {})).group, int(LogPsuMonitor));
        QCOMPARE(cls(psuctl::buildControl(2, {})).mb, -1);
        QByteArray psuMon(psumon::kLength, 0);
        psuMon.replace(2, 2, psumon::cmd());
        QCOMPARE(cls(psuMon).group, int(LogPsuMonitor));
        QCOMPARE(cls(psucfg::buildReadRequest(2)).group, int(LogPsuConfig));
        QCOMPARE(cls(psucfg::buildWrite(2, psucfg::defaultValues())).group, int(LogPsuConfig));
        QByteArray psuReply(psucfg::kLength, 0);
        psuReply.replace(2, 2, psucfg::replyCmd());
        QCOMPARE(cls(psuReply).group, int(LogPsuConfig));

        // Nạp code: FPGA (5555 6666 8888 AAAA 9999) và STM32 (9090 ... 9595)
        for (const char *hex : {"abcd5555", "abcd6666", "abcd8888", "abcdaaaa", "abcd9999"})
            QCOMPARE(cls(QByteArray::fromHex(hex) + QByteArray(10, 0)).group, int(LogFirmware));
        QCOMPARE(cls(stmota::buildBegin(1, 100, 0, 1)).group, int(LogFirmware));
        QCOMPARE(cls(stmota::buildData(1, 0, QByteArray(4, 'x'))).group, int(LogFirmware));
        QCOMPARE(cls(stmota::buildCtrl(1, stmota::kInfo)).group, int(LogFirmware));

        // Không nhận ra hoặc quá ngắn: hiện ở mọi trang (nhóm hệ thống)
        QCOMPARE(cls(QByteArray::fromHex("abcd7777") + QByteArray(10, 0)).group, int(LogSystem));
        QCOMPARE(cls(QByteArray::fromHex("abcd")).group, int(LogSystem));
        QCOMPARE(cls({}).group, int(LogSystem));
    }

    // ---------- Tách khung ----------
    void parserResyncsAfterGarbageAndSplitChunks()
    {
        core::FrameRegistry registry;
        services::TrbMonitor::registerFrames(registry, true);
        core::FrameParser parser(&registry);
        QList<core::Frame> got;
        auto sink = [&](const core::Frame &f) { got << f; };

        const QByteArray a = monitorFrame(1, 2, zeros(trbmon::kNumFields));
        const QByteArray b = monitorFrame(3, 4, zeros(trbmon::kNumFields));
        QByteArray stream = QByteArray::fromHex("00ffab") + a + QByteArray::fromHex("abcd") + b;   // rác và đầu khung cụt
        for (int i = 0; i < stream.size(); i += 50) parser.feed(stream.mid(i, 50), sink);          // chia nhỏ ngẫu nhiên
        QCOMPARE(got.size(), 2);
        QCOMPARE(got.at(0).raw, a);
        QCOMPARE(got.at(1).raw, b);
        QVERIFY(parser.stats().droppedBytes > 0);
    }

    void parserDropsBadCrcAndBadTailer()
    {
        core::FrameRegistry registry;
        services::TrbMonitor::registerFrames(registry, true);
        core::FrameParser parser(&registry);
        int frames = 0;
        auto sink = [&](const core::Frame &) { ++frames; };

        QByteArray bad = monitorFrame(1, 1, zeros(trbmon::kNumFields));
        bad[100] = char(bad.at(100) ^ 0x01);
        parser.feed(bad, sink);
        QCOMPARE(frames, 0);
        QVERIFY(parser.stats().crcErrors >= 1);

        QByteArray tail = monitorFrame(1, 1, zeros(trbmon::kNumFields));
        tail[tail.size() - 1] = char(0x00);
        parser.feed(tail, sink);
        QCOMPARE(frames, 0);

        parser.feed(monitorFrame(1, 1, zeros(trbmon::kNumFields)), sink);   // vẫn nhận được khung tốt sau đó
        QCOMPARE(frames, 1);
    }

    void registryRejectsConflicts()
    {
        core::FrameRegistry r;
        QVERIFY(r.add(trbmon::spec(true)));
        QVERIFY(!r.add(trbmon::spec(true)));                                       // trùng CMD
        QVERIFY(!r.add({QByteArray(1, char(0x11)), 20, 2, true, "trùng byte đầu"})); // CMD 1 byte trùng byte đầu CMD 2 byte
        QVERIFY(r.add({QByteArray(1, char(0x7E)), 20, 2, true, "khác"}));
    }

    // ---------- Giám sát ----------
    void monitorUpdatesStoreAndStatus()
    {
        Rig rig("monitor");
        services::TrbMonitor::registerFrames(rig.registry, true);
        rig.link.start();
        model::DeviceStore store(20, 8);
        model::Thresholds thr(&trbmon::table());
        services::AlarmEngine alarms(&thr, &trbmon::table());
        services::TrbMonitor mon(&rig.link, &store, &alarms);
        QSignalSpy updated(&mon, &services::TrbMonitor::trbUpdated);
        QSignalSpy changed(&store, &model::DeviceStore::trbStatusChanged);

        thr.set(3, 5, trbmon::kIdxTrbV, {100, 200});
        QList<double> v = zeros(trbmon::kNumFields);
        v[trbmon::kIdxPg] = 0x0F;       // PG tốt, nếu không sẽ tính là trip
        v[trbmon::kIdxTrbV] = 150;
        v[trbmon::kIdxTrm0] = 77;
        rig.transport->inject(monitorFrame(3, 5, v));
        QCOMPARE(store.trb(3, 5).status, model::Status::Ok);
        QCOMPARE(store.trb(3, 5).values.at(trbmon::kIdxTrm0), 77.0);
        QCOMPARE(store.trb(3, 5).frames, quint64(1));
        QCOMPARE(store.trb(3, 4).status, model::Status::NoData);
        QCOMPARE(store.count(model::Status::Ok), 1);
        QCOMPARE(store.count(model::Status::NoData), 20 * 8 - 1);
        QCOMPARE(updated.last().at(2).toBool(), true);          // lần đầu: đổi trạng thái

        v[trbmon::kIdxTrbV] = 250;                              // vượt ngưỡng
        rig.transport->inject(monitorFrame(3, 5, v));
        QCOMPARE(store.trb(3, 5).status, model::Status::Warning);
        QCOMPARE(store.trb(3, 5).alarms, QList<int>{trbmon::kIdxTrbV});

        v[trbmon::kIdxTrip0 + 9] = 4;                           // trip code khác 0: Trip, ưu tiên hơn Quá ngưỡng
        rig.transport->inject(monitorFrame(3, 5, v));
        QCOMPARE(store.trb(3, 5).status, model::Status::Trip);

        v = zeros(trbmon::kNumFields);
        v[trbmon::kIdxPg] = 0x0F;
        v[trbmon::kIdxTrbV] = 150;
        rig.transport->inject(monitorFrame(3, 5, v));
        QCOMPARE(store.trb(3, 5).status, model::Status::Ok);
        QCOMPARE(updated.last().at(2).toBool(), true);
        QCOMPARE(changed.size(), 4);                            // NoData>Ok>Warning>Trip>Ok
        rig.transport->inject(monitorFrame(3, 5, v));
        QCOMPARE(updated.last().at(2).toBool(), false);         // cùng trạng thái: không báo đổi
    }

    void monitorIgnoresBadAddressAndBadCrc()
    {
        Rig rig("monitor");
        services::TrbMonitor::registerFrames(rig.registry, true);
        rig.link.start();
        model::DeviceStore store(20, 8);
        services::TrbMonitor mon(&rig.link, &store);

        rig.transport->inject(monitorFrame(20, 0, zeros(trbmon::kNumFields)));   // MB ngoài dải
        rig.transport->inject(monitorFrame(0, 8, zeros(trbmon::kNumFields)));    // TRB ngoài dải
        QCOMPARE(mon.badAddressFrames(), quint64(2));
        QCOMPARE(store.count(model::Status::NoData), 20 * 8);

        QByteArray bad = monitorFrame(1, 1, zeros(trbmon::kNumFields));
        bad[50] = char(bad.at(50) ^ 0x40);
        rig.transport->inject(bad);
        QCOMPARE(store.trb(1, 1).status, model::Status::NoData);
    }

    void storeMarksStaleAsLost()
    {
        model::DeviceStore store(2, 2);
        QSignalSpy changed(&store, &model::DeviceStore::trbStatusChanged);
        store.updateTrb(1, 1, zeros(trbmon::kNumFields), {}, 1000, model::Status::Ok);
        store.markStale(2500, 3000);
        QCOMPARE(store.trb(1, 1).status, model::Status::Ok);
        store.markStale(5000, 3000);
        QCOMPARE(store.trb(1, 1).status, model::Status::Lost);
        QCOMPARE(store.count(model::Status::Lost), 1);
        QCOMPARE(changed.size(), 2);
        store.markStale(9000, 3000);                              // đã Lost thì không báo lại
        QCOMPARE(changed.size(), 2);
        store.updateTrb(1, 1, zeros(trbmon::kNumFields), {}, 9500, model::Status::Ok);   // có lại tin: hồi phục
        QCOMPARE(store.trb(1, 1).status, model::Status::Ok);
        QVERIFY(!store.contains(2, 0) && !store.contains(0, 2) && !store.contains(-1, 0));
    }

    void statusCountersMayOverlapAndSumPastTheDeviceCount()
    {
        model::DeviceStore store(1, 4);
        const QList<double> v = zeros(trbmon::kNumFields);
        store.updateTrb(0, 0, v, {}, 1000, model::Status::Ok);
        store.updateTrb(0, 1, v, {}, 1000, model::Status::Warning, {5});
        store.updateTrb(0, 2, v, {}, 1000, model::Status::Trip, {5, 6});      // vừa trip vừa quá ngưỡng
        store.updateTrb(0, 3, v, {}, 1000, model::Status::Trip);
        store.markStale(9000, 3000);                                           // cả 4 mất kết nối nhưng nhớ trạng thái cũ
        store.updateTrb(0, 0, v, {}, 9500, model::Status::Ok);                 // TRB 0 có tin lại
        store.updateTrb(0, 2, v, {}, 9500, model::Status::Trip, {5, 6});       // TRB 2 vẫn vừa trip vừa quá ngưỡng

        using model::Status;
        QCOMPARE(store.countCondition(Status::Ok), 1);       // TRB 0
        QCOMPARE(store.countCondition(Status::Lost), 2);     // TRB 1, 3
        QCOMPARE(store.countCondition(Status::Trip), 2);     // TRB 2 (đang trip) + TRB 3 (mất kết nối, trước đó trip)
        QCOMPARE(store.countCondition(Status::Warning), 2);  // TRB 1 (mất kết nối, còn ô quá ngưỡng) + TRB 2
        QVERIFY(store.countCondition(Status::Ok) + store.countCondition(Status::Lost) + store.countCondition(Status::Trip)
                    + store.countCondition(Status::Warning) > 4);   // tổng vượt số TRB
    }

    void gatewayBurstOfEightFramesInOneChunkIsAllParsed()
    {
        Rig rig("monitor");
        services::TrbMonitor::registerFrames(rig.registry, true);
        rig.link.start();
        model::DeviceStore store(2, 8);
        services::TrbMonitor mon(&rig.link, &store, nullptr);
        QSignalSpy updated(&mon, &services::TrbMonitor::trbUpdated);
        QList<double> v = zeros(trbmon::kNumFields);
        v[trbmon::kIdxPg] = 0x0F;
        QByteArray burst;
        for (int t = 0; t < 8; ++t) burst += monitorFrame(1, t, v);       // Gateway gửi dồn 8 TRB của MB1 trong một lần
        QCOMPARE(burst.size(), 8 * trbmon::kLength);
        rig.transport->inject(burst);
        QCOMPARE(updated.size(), 8);
        QCOMPARE(store.count(model::Status::Ok), 8);
        QCOMPARE(store.trb(1, 7).frames, quint64(1));
        // Chia mảnh bất kỳ giữa các khung (TCP không giữ ranh giới) cũng không mất khung nào.
        for (int off = 0; off < burst.size(); off += 333) rig.transport->inject(burst.mid(off, 333));
        QCOMPARE(updated.size(), 16);
    }

    void powerGoodFailureCountsAsTrip()
    {
        Rig rig("monitor");
        services::TrbMonitor::registerFrames(rig.registry, true);
        rig.link.start();
        model::DeviceStore store(2, 2);
        services::TrbMonitor mon(&rig.link, &store, nullptr);
        QSignalSpy pg(&mon, &services::TrbMonitor::powerGoodChanged);

        QList<double> v = zeros(trbmon::kNumFields);
        v[trbmon::kIdxPg] = 0x0F;
        rig.transport->inject(monitorFrame(1, 1, v));
        QCOMPARE(store.trb(1, 1).status, model::Status::Ok);
        QCOMPARE(pg.size(), 0);

        v[trbmon::kIdxPg] = 0b1010;                     // PG TRM1 và TRM3 mất
        rig.transport->inject(monitorFrame(1, 1, v));
        QCOMPARE(store.trb(1, 1).status, model::Status::Trip);
        QCOMPARE(pg.size(), 1);
        QCOMPARE(pg.last().at(2).toInt(), 0b0101);
        rig.transport->inject(monitorFrame(1, 1, v));   // không đổi: không báo lại
        QCOMPARE(pg.size(), 1);

        v[trbmon::kIdxPg] = 0xF0 | 0x0F;                // bit cao không tính
        rig.transport->inject(monitorFrame(1, 1, v));
        QCOMPARE(store.trb(1, 1).status, model::Status::Ok);
        QCOMPARE(pg.last().at(2).toInt(), 0);
        QCOMPARE(trbmon::pgFaultBits(0x07), 0b1000);
    }

    void lostTrbRemembersWhatItWasBeforeAndKeepsItsData()
    {
        model::DeviceStore store(1, 2);
        QList<double> v = zeros(trbmon::kNumFields);
        v[trbmon::kIdxTrip0] = 4;
        store.updateTrb(0, 0, v, {}, 1000, model::Status::Trip, {3});
        store.updateTrb(0, 1, zeros(trbmon::kNumFields), {}, 1000, model::Status::Warning, {5});
        QCOMPARE(store.trb(0, 0).lastLive, model::Status::Trip);
        store.markStale(9000, 3000);
        for (int t = 0; t < 2; ++t) QCOMPARE(store.trb(0, t).status, model::Status::Lost);
        QCOMPARE(store.trb(0, 0).lastLive, model::Status::Trip);       // trạng thái cũ còn được nhớ
        QCOMPARE(store.trb(0, 1).lastLive, model::Status::Warning);
        QCOMPARE(store.trb(0, 0).values.at(trbmon::kIdxTrip0), 4.0);   // số liệu và ô vượt ngưỡng cuối cùng còn nguyên
        QCOMPARE(store.trb(0, 1).alarms, QList<int>({5}));
        store.updateTrb(0, 0, v, {}, 9500, model::Status::Ok);
        QCOMPARE(store.trb(0, 0).lastLive, model::Status::Ok);
    }

    // ---------- Ngưỡng và cảnh báo ----------
    void thresholdsPerDeviceFallBackToDefault()
    {
        model::Thresholds thr(&trbmon::table());
        const int f = trbmon::kIdxTrbV;
        QVERIFY(!thr.get(1, 1, f).isSet());
        thr.setDefault(f, {10, 20});
        QCOMPARE(thr.get(1, 1, f).max, 20.0);
        thr.set(1, 1, f, {30, 40});
        QCOMPARE(thr.get(1, 1, f).min, 30.0);
        QCOMPARE(thr.get(2, 2, f).min, 10.0);                    // thiết bị khác vẫn dùng mặc định
        thr.set(1, 1, f, {0, 0});                                // 0/0 = chưa đặt: quay về mặc định
        QCOMPARE(thr.get(1, 1, f).min, 10.0);
        QVERIFY(model::Limit({5, 6}).violatedBy(4));
        QVERIFY(model::Limit({5, 6}).violatedBy(7));
        QVERIFY(!model::Limit({5, 6}).violatedBy(5));
        QVERIFY(!model::Limit({0, 0}).violatedBy(1000));
    }

    void thresholdsSaveLoadRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("thresholds.json");
        model::Thresholds a(&trbmon::table());
        a.setDefault(trbmon::kIdxMcuTemp, {1, 99});
        a.set(19, 7, trbmon::kIdxTrbI, {11, 22});
        QVERIFY(a.save(path));

        model::Thresholds b(&trbmon::table());
        QString err;
        QVERIFY2(b.load(path, &err), qPrintable(err));
        QCOMPARE(b.get(5, 5, trbmon::kIdxMcuTemp).max, 99.0);
        QCOMPARE(b.get(19, 7, trbmon::kIdxTrbI).min, 11.0);
        QVERIFY(!b.get(19, 6, trbmon::kIdxTrbI).isSet());

        QFile bad(dir.filePath("bad.json"));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("{ not json");
        bad.close();
        QVERIFY(!b.load(dir.filePath("bad.json"), &err));
        QVERIFY(!b.load(dir.filePath("missing.json"), &err));
    }

    void alarmEngineReportsOnlyTransitions()
    {
        model::Thresholds thr(&trbmon::table());
        thr.set(0, 0, trbmon::kIdxTrbV, {100, 200});
        services::AlarmEngine engine(&thr, &trbmon::table());
        QSignalSpy events(&engine, &services::AlarmEngine::alarmEvent);

        QList<double> v = zeros(trbmon::kNumFields);
        v[trbmon::kIdxTrbV] = 150;
        QVERIFY(engine.check(0, 0, v).isEmpty());
        QCOMPARE(events.size(), 0);

        v[trbmon::kIdxTrbV] = 300;
        QCOMPARE(engine.check(0, 0, v), QList<int>{trbmon::kIdxTrbV});
        QCOMPARE(events.size(), 1);
        QCOMPARE(events.last().at(3).toBool(), true);
        QVERIFY(events.last().at(2).toString().contains("TRB.V"));
        engine.check(0, 0, v);                                    // còn vượt: không lặp sự kiện
        QCOMPARE(events.size(), 1);

        v[trbmon::kIdxTrbV] = 50;                                 // dưới min vẫn là vượt, không phát lại
        QCOMPARE(engine.check(0, 0, v).size(), 1);
        QCOMPARE(events.size(), 1);

        v[trbmon::kIdxTrbV] = 150;
        QVERIFY(engine.check(0, 0, v).isEmpty());
        QCOMPARE(events.size(), 2);
        QCOMPARE(events.last().at(3).toBool(), false);

        QVERIFY(engine.check(1, 1, v).isEmpty());                 // thiết bị khác không có ngưỡng
    }

    // ---------- Cấu hình ----------
    void configJsonRoundTripAndRangeCheck()
    {
        QList<double> v = zeros(trbcfg::table().size());
        v[trbcfg::table().indexOf("GENERAL", "TEMP_MCU_MAX")] = 1234;
        v[trbcfg::table().indexOf("ADAR 3", "LDO")] = 200;
        v[trbcfg::table().indexOf("DEBUG", "PERIOD_TR_DEBUG")] = 4000000000.0;
        const QJsonObject json = trbcfg::toJson(v);
        QCOMPARE(json.value("GENERAL").toObject().value("TEMP_MCU_MAX").toInt(), 1234);

        QList<double> back = zeros(v.size());
        trbcfg::fromJson(json, back);
        QCOMPARE(back, v);

        QList<double> keep = v;                                   // ngoài dải độ rộng hoặc kiểu sai thì giữ nguyên
        QJsonObject odd;
        odd["ADAR 3"] = QJsonObject{{"LDO", 256}, {"CONFIG_RESET", -1}, {"CONFIG_LDO", "x"}};
        odd["GENERAL"] = QJsonObject{{"TEMP_MCU_MAX", 65536}};
        trbcfg::fromJson(odd, keep);
        QCOMPARE(keep, v);
    }

    void configFramesAreWellFormed()
    {
        const QByteArray r = trbcfg::buildReadRequest(6, 2);
        QCOMPARE(r.toHex(), QByteArray("abcda3a3" "0602") + r.mid(6).toHex());
        QCOMPARE(r.size(), 10);
        QVERIFY(crcOk(r, 4));

        QList<double> v = zeros(trbcfg::table().size());
        v[0] = 0x010203;
        const QByteArray w = trbcfg::buildWrite(6, 2, v);
        QCOMPARE(w.size(), 520);
        QCOMPARE(w.left(6).toHex(), QByteArray("abcda1a10602"));
        QCOMPARE(w.mid(6, 3).toHex(), QByteArray("010203"));
        QVERIFY(crcOk(w, 4));
    }

    void configWriteVerifyAndThresholds()
    {
        Rig rig("service");
        services::TrbConfig::registerFrames(rig.registry, true);
        rig.link.start();
        FakeTrb trb;
        trb.attach(rig.transport);
        model::Thresholds thr(&trbmon::table());
        services::TrbConfig cfg(&rig.link, &thr, QString(), 500, 0);
        QSignalSpy finished(&cfg, &services::TrbConfig::finished);
        QSignalSpy device(&cfg, &services::TrbConfig::deviceFinished);
        QSignalSpy read(&cfg, &services::TrbConfig::configRead);

        QList<double> v = zeros(trbcfg::table().size());
        const FieldTable &t = trbcfg::table();
        v[t.indexOf("GENERAL", "VOLTAGE_TRB_MAX")] = 3000;
        v[t.indexOf("GENERAL", "VOLTAGE_TRB_MIN")] = 100;
        v[t.indexOf("TRM 2", "I_SEN_3_MAX")] = 900;
        v[t.indexOf("TRM 2", "I_SEN_3_MIN")] = 10;
        cfg.writeFull({4, 6}, v);
        QVERIFY(finished.wait(3000));
        QCOMPARE(finished.last().at(0).toInt(), 1);
        QCOMPARE(finished.last().at(1).toInt(), 0);
        QVERIFY2(device.last().at(2).toBool(), qPrintable(device.last().at(3).toString()));
        QCOMPARE(trb.config.value(FakeTrb::key(4, 6)), v);
        QVERIFY(cfg.cached(4, 6) && *cfg.cached(4, 6) == v);
        QVERIFY(cfg.cached(4, 7) == nullptr);
        QVERIFY(read.size() >= 1);

        QCOMPARE(thr.get(4, 6, trbmon::kIdxTrbV).max, 3000.0);                     // ngưỡng cập nhật từ cấu hình đọc về
        QCOMPARE(thr.get(4, 6, trbmon::kIdxTrbV).min, 100.0);
        QCOMPARE(thr.get(4, 6, trbmon::kIdxTrm0 + trbmon::kTrmFields + 2).max, 900.0);   // TRM2 I_SEN3
        QVERIFY(!thr.get(4, 7, trbmon::kIdxTrbV).isSet());
    }

    void configVerifyDetectsMismatch()
    {
        Rig rig("service");
        services::TrbConfig::registerFrames(rig.registry, true);
        rig.link.start();
        FakeTrb trb;
        trb.corruptField = 10;
        trb.attach(rig.transport);
        model::Thresholds thr(&trbmon::table());
        services::TrbConfig cfg(&rig.link, &thr, QString(), 500, 0);
        QSignalSpy finished(&cfg, &services::TrbConfig::finished);
        QSignalSpy device(&cfg, &services::TrbConfig::deviceFinished);

        cfg.writeFull({0, 0}, zeros(trbcfg::table().size()));
        QVERIFY(finished.wait(3000));
        QCOMPARE(finished.last().at(1).toInt(), 1);
        QVERIFY(device.last().at(3).toString().contains("KHÔNG khớp"));
    }

    void configBatchApplyKeepsOtherFieldsAndReportsTimeouts()
    {
        Rig rig("service");
        services::TrbConfig::registerFrames(rig.registry, true);
        rig.link.start();
        FakeTrb trb;
        trb.attach(rig.transport);

        const FieldTable &t = trbcfg::table();
        const int own = t.indexOf("CAL", "dx");
        const int shared = t.indexOf("GENERAL", "TEMP_OFFSET_MCU");
        QList<double> a = zeros(t.size()), b = zeros(t.size());
        a[own] = 111;
        b[own] = 222;
        trb.config[FakeTrb::key(0, 1)] = a;
        trb.config[FakeTrb::key(0, 2)] = b;                       // TRB 0/3 không có trong trb.config: không trả lời

        model::Thresholds thr(&trbmon::table());
        services::TrbConfig cfg(&rig.link, &thr, QString(), 150, 0);
        QSignalSpy finished(&cfg, &services::TrbConfig::finished);
        QSignalSpy progress(&cfg, &services::TrbConfig::progress);
        QSignalSpy device(&cfg, &services::TrbConfig::deviceFinished);

        cfg.applyChanges({{0, 1}, {0, 2}, {0, 3}}, {{shared, 42}});
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.last().at(0).toInt(), 2);
        QCOMPARE(finished.last().at(1).toInt(), 1);
        QCOMPARE(trb.config[FakeTrb::key(0, 1)].at(shared), 42.0);
        QCOMPARE(trb.config[FakeTrb::key(0, 2)].at(shared), 42.0);
        QCOMPARE(trb.config[FakeTrb::key(0, 1)].at(own), 111.0);  // trường không sửa giữ nguyên theo từng TRB
        QCOMPARE(trb.config[FakeTrb::key(0, 2)].at(own), 222.0);
        QVERIFY(!trb.config.contains(FakeTrb::key(0, 3)));
        QCOMPARE(device.last().at(2).toBool(), false);
        QVERIFY(device.last().at(3).toString().contains("không trả lời"));
        QCOMPARE(progress.last().at(0).toInt(), 3);
        QCOMPARE(progress.last().at(1).toInt(), 3);
        QVERIFY(!cfg.busy());
    }

    void configWriteFullToSeveralTrbs()
    {
        Rig rig("service");
        services::TrbConfig::registerFrames(rig.registry, true);
        rig.link.start();
        FakeTrb trb;
        trb.attach(rig.transport);
        trb.config[FakeTrb::key(1, 1)] = QList<double>(trbcfg::table().size(), 9.0);   // cấu hình cũ bị ghi đè hoàn toàn

        model::Thresholds thr(&trbmon::table());
        services::TrbConfig cfg(&rig.link, &thr, QString(), 500, 0);
        QSignalSpy finished(&cfg, &services::TrbConfig::finished);

        QList<double> v = zeros(trbcfg::table().size());
        v[trbcfg::table().indexOf("GENERAL", "PULSE_TR_MAX")] = 5;
        cfg.writeFullMany({{1, 1}, {1, 2}, {19, 7}}, v);
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.last().at(0).toInt(), 3);
        QCOMPARE(finished.last().at(1).toInt(), 0);
        for (auto k : {FakeTrb::key(1, 1), FakeTrb::key(1, 2), FakeTrb::key(19, 7)}) QCOMPARE(trb.config.value(k), v);
        QCOMPARE(trb.writes, 3);
    }

    void configIgnoresNewJobWhileBusy()
    {
        Rig rig("service");
        services::TrbConfig::registerFrames(rig.registry, true);
        rig.link.start();
        FakeTrb trb;
        trb.attach(rig.transport);
        model::Thresholds thr(&trbmon::table());
        services::TrbConfig cfg(&rig.link, &thr, QString(), 500, 0);
        QSignalSpy finished(&cfg, &services::TrbConfig::finished);

        const QList<double> v = zeros(trbcfg::table().size());
        cfg.writeFull({0, 0}, v);
        QVERIFY(cfg.busy());
        cfg.writeFull({0, 1}, v);                                 // bị bỏ qua vì đang bận
        QVERIFY(finished.wait(3000));
        QCOMPARE(finished.size(), 1);
        QVERIFY(!trb.config.contains(FakeTrb::key(0, 1)));
    }

    // ---------- Log CSV ----------
    void csvLoggerWritesHeaderRowsAndThrottles()
    {
        QTemporaryDir dir;
        {
            services::CsvLogger logger(dir.path(), 10000, 1024 * 1024, &trbmon::table(), &proto::trbmon::table());
            model::TrbState s;
            s.status = model::Status::Warning;
            s.lastSeenMs = 1'700'000'000'000;
            s.values = zeros(trbmon::kNumFields);
            s.values[trbmon::kIdxTrbV] = 123;
            logger.logTrb(2, 3, s, false);
            s.lastSeenMs += 2000;                                 // chưa đủ chu kỳ 10 s: bỏ qua
            logger.logTrb(2, 3, s, false);
            s.lastSeenMs += 2000;
            logger.logTrb(2, 3, s, true);                         // ép ghi (đổi trạng thái)
            logger.logEvent(QStringLiteral("sự kiện \"thử\""));
            logger.flush();
        }

        const QDir day(dir.path() + "/" + QDate::currentDate().toString(Qt::ISODate));
        const QStringList trbFiles = day.entryList({"trb_*.csv"});
        QCOMPARE(trbFiles.size(), 1);
        QFile f(day.filePath(trbFiles.first()));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QList<QByteArray> lines = f.readAll().split('\n');
        QVERIFY(lines.at(0).startsWith("\xEF\xBB\xBF" "time,mb,trb,status,TRM1.I SEN1"));   // UTF-8 BOM cho Excel
        QVERIFY(lines.at(0).contains("TRB.V"));
        QCOMPARE(lines.size(), 4);                                // header, 2 dòng dữ liệu, dòng rỗng cuối
        QVERIFY(lines.at(1).contains(",2,3,"));
        QVERIFY(lines.at(1).contains(",123,"));

        const QStringList events = day.entryList({"events_*.csv"});
        QCOMPARE(events.size(), 1);
        QFile e(day.filePath(events.first()));
        QVERIFY(e.open(QIODevice::ReadOnly));
        QVERIFY(e.readAll().contains("\"sự kiện \"\"thử\"\"\""));  // dấu nháy kép được escape
    }
};

QTEST_MAIN(TrbTest)
#include "trb_test.moc"
