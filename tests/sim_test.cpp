// Test chương trình giả lập TRB (actl_sim): mô hình thiết bị, xử lý khung từ Gateway, bơm lỗi,
// và chạy cùng các service của app qua hai transport giả nối chéo nhau.
#include "../core/link.h"
#include "../model/device_store.h"
#include "../proto/trb_config_proto.h"
#include "../proto/trb_control_proto.h"
#include "../proto/trb_monitor_proto.h"
#include "../services/trb_config.h"
#include "../services/trb_control.h"
#include "../services/trb_monitor.h"
#include "../sim/bus_port.h"
#include "test_support.h"
#include <QSignalSpy>
#include <QtTest>

using namespace proto;
using testsupport::FakeTransport;
using testsupport::crcOk;

namespace {

core::Frame asFrame(const QByteArray &raw)
{
    core::Frame f;
    f.raw = raw;
    f.cmd = raw.mid(2, 2);
    return f;
}

// Đường nối app <-> giả lập: Link của app ghi vào t1 thì BusPort nhận ở t2 và ngược lại.
struct Wire {
    core::FrameRegistry appRegistry;
    FakeTransport *appSide = new FakeTransport;
    FakeTransport *simSide = new FakeTransport;
    core::Link link;
    sim::SimWorld world;
    sim::BusPort port;

    explicit Wire(int mb = 2, int trb = 8)
        : link("wire", appSide, &appRegistry), world(mb, trb, 7), port(simSide, &world)
    {
        services::TrbMonitor::registerFrames(appRegistry, true);
        services::TrbConfig::registerFrames(appRegistry, true);
        appSide->onWrite = [this](const QByteArray &d) { simSide->injectLater(d); };
        simSide->onWrite = [this](const QByteArray &d) { appSide->injectLater(d); };
        simSide->open();
        link.start();
    }
};

}

class SimTest : public QObject {
    Q_OBJECT
private slots:
    void pollFrameLayout()
    {
        const QByteArray p = trbmon::buildPoll(3, 5);
        QCOMPARE(p.size(), 12);
        QCOMPARE(p.left(4), QByteArray::fromHex("ABCD1111"));
        QCOMPARE(quint8(p.at(4)), quint8(3));
        QCOMPARE(quint8(p.at(5)), quint8(5));
        QCOMPARE(p.mid(6, 2), QByteArray(2, '\0'));
        QCOMPARE(p.right(2), QByteArray::fromHex("E1E2"));
        QVERIFY(crcOk(p, 4));
    }

    void requestRegistryAcceptsPollAndRejectsReplyLength()
    {
        core::FrameRegistry reg;
        QVERIFY(sim::registerRequestFrames(reg, true));
        core::FrameParser parser(&reg);
        int n = 0;
        parser.feed(trbmon::buildPoll(0, 1) + trbmon::buildPoll(1, 2), [&](const core::Frame &) { ++n; });
        QCOMPARE(n, 2);
    }

    void pollGetsMonitorFrameFromThatTrbOnly()
    {
        sim::SimWorld w(2, 8, 1);
        const QByteArray r = w.handle(asFrame(trbmon::buildPoll(1, 6)));
        QCOMPARE(r.size(), trbmon::kLength);
        QVERIFY(crcOk(r, trbmon::kCrcStart));
        QCOMPARE(quint8(r.at(trbmon::kOffMb)), quint8(1));
        QCOMPARE(quint8(r.at(trbmon::kOffTrb)), quint8(6));
        const QList<double> v = trbmon::table().decode(r);
        QVERIFY(v.at(trbmon::kIdxTrbV) >= 145 && v.at(trbmon::kIdxTrbV) <= 155);
        QCOMPARE(v.at(trbmon::kIdxInitAdar), 255.0);
        QCOMPARE(w.stats().polls, quint64(1));
    }

    void unknownAddressAndOfflineTrbStaySilent()
    {
        sim::SimWorld w(2, 8, 1);
        QVERIFY(w.handle(asFrame(trbmon::buildPoll(5, 0))).isEmpty());   // MB không tồn tại
        QCOMPARE(w.stats().ignored, quint64(1));
        w.trb(0, 2)->setOnline(false);
        QVERIFY(w.handle(asFrame(trbmon::buildPoll(0, 2))).isEmpty());
        w.trb(0, 2)->setOnline(true);
        QVERIFY(!w.handle(asFrame(trbmon::buildPoll(0, 2))).isEmpty());
        QVERIFY(w.handle(asFrame(trbmon::buildPoll(0xFF, 0xFF))).isEmpty()); // broadcast không có trả lời
    }

    void forcedValuesAndTripShowUpInMonitorFrame()
    {
        sim::SimWorld w(1, 2, 1);
        const int isen = trbmon::table().indexOf({}, "TRM2.I SEN3");
        QVERIFY(isen >= 0);
        w.trb(0, 1)->force(isen, 2500);
        w.trb(0, 1)->setTrip(0, 0x04);
        const QList<double> v = trbmon::table().decode(w.handle(asFrame(trbmon::buildPoll(0, 1))));
        QCOMPARE(v.at(isen), 2500.0);
        QCOMPARE(v.at(trbmon::kIdxTrip0), 4.0);
        const QList<double> other = trbmon::table().decode(w.handle(asFrame(trbmon::buildPoll(0, 0))));
        QVERIFY(other.at(isen) < 200);          // TRB khác không bị ảnh hưởng
    }

    void controlSetsDebugAndClearTripClearsTrips()
    {
        sim::SimWorld w(1, 2, 1);
        w.trb(0, 0)->setTrip(3, 0xFF);
        trbctl::ControlCmd c;
        c.debugMode = true;
        c.start = true;
        c.paMask = 5;
        QVERIFY(w.handle(asFrame(trbctl::buildControl(0, 0, c))).isEmpty());   // điều khiển không có ACK
        QVERIFY(w.trb(0, 0)->debug());
        QCOMPARE(w.trb(0, 0)->lastControl().paMask, quint8(5));
        QVERIFY(!w.trb(0, 1)->debug());
        QCOMPARE(w.trb(0, 0)->trip(3), quint8(0xFF));
        c.clearTrip = true;
        w.handle(asFrame(trbctl::buildControl(0, 0, c)));
        QCOMPARE(w.trb(0, 0)->trip(3), quint8(0));
    }

    void twoTrbsInDebugAreCountedAsConflict()
    {
        sim::SimWorld w(1, 3, 1);
        trbctl::ControlCmd c;
        c.debugMode = true;
        w.handle(asFrame(trbctl::buildControl(0, 0, c)));
        QCOMPARE(w.stats().debugConflicts, quint64(0));
        w.handle(asFrame(trbctl::buildControl(0, 1, c)));
        QCOMPARE(w.stats().debugConflicts, quint64(1));
        QCOMPARE(w.debugCount(), 2);
        c.debugMode = false;
        w.handle(asFrame(trbctl::buildControl(0, 1, c)));
        w.handle(asFrame(trbctl::buildControl(0, 1, trbctl::ControlCmd{0, false, false, false, true})));
        QCOMPARE(w.stats().debugConflicts, quint64(2));   // lại có hai TRB debug
    }

    void broadcastControlReachesEveryTrb()
    {
        sim::SimWorld w(2, 2, 1);
        trbctl::ControlCmd c;
        c.paMask = 0x0F;
        w.handle(asFrame(trbctl::buildControl(0xFF, 0xFF, c)));
        for (int mb = 0; mb < 2; ++mb)
            for (int t = 0; t < 2; ++t) QCOMPARE(w.trb(mb, t)->lastControl().paMask, quint8(0x0F));
    }

    void debugTrbSendsMonitorFramesByItself()
    {
        sim::SimWorld w(1, 2, 1);
        QSignalSpy spy(&w, &sim::SimWorld::unsolicited);
        w.setDebugPeriodMs(20);
        QTest::qWait(70);
        QCOMPARE(spy.size(), 0);                          // chưa TRB nào debug
        trbctl::ControlCmd c;
        c.debugMode = true;
        w.handle(asFrame(trbctl::buildControl(0, 1, c)));
        QTRY_VERIFY_WITH_TIMEOUT(spy.size() >= 2, 1000);
        const QByteArray f = spy.last().at(0).toByteArray();
        QCOMPARE(f.size(), trbmon::kLength);
        QCOMPARE(quint8(f.at(trbmon::kOffTrb)), quint8(1));
        w.trb(0, 1)->setOnline(false);                    // mất kết nối thì cũng ngừng tự gửi
        const int n = spy.size();
        QTest::qWait(70);
        QCOMPARE(spy.size(), n);
    }

    void configWriteReadRoundTripThroughRealServices()
    {
        Wire wire;
        services::TrbConfig cfg(&wire.link, nullptr, QString(), 500, 0);
        QSignalSpy finished(&cfg, &services::TrbConfig::finished);
        QSignalSpy device(&cfg, &services::TrbConfig::deviceFinished);

        QList<double> v(trbcfg::table().size(), 0.0);
        const int dx = trbcfg::table().indexOf("CAL", "dx");
        v[dx] = 1234;
        cfg.writeFull({1, 3}, v);
        QVERIFY(finished.wait(3000));
        QCOMPARE(finished.last().at(0).toInt(), 1);
        QVERIFY(device.last().at(3).toString().contains("khớp"));
        QCOMPARE(wire.world.trb(1, 3)->config().at(dx), 1234.0);
        QCOMPARE(wire.world.trb(1, 2)->config().at(dx), 0.0);       // TRB khác không bị ghi
        QCOMPARE(wire.world.stats().configWrites, quint64(1));
    }

    void offlineTrbConfigReadTimesOutAfterThreeAsks()
    {
        Wire wire;
        wire.world.trb(0, 4)->setOnline(false);
        services::TrbConfig cfg(&wire.link, nullptr, QString(), 80, 0);
        QSignalSpy finished(&cfg, &services::TrbConfig::finished);
        QSignalSpy device(&cfg, &services::TrbConfig::deviceFinished);
        cfg.readDevices({{0, 4}});
        QVERIFY(finished.wait(3000));
        QCOMPARE(device.last().at(2).toBool(), false);
        QVERIFY(device.last().at(3).toString().contains("không trả lời"));
        QCOMPARE(wire.world.stats().configReads, quint64(3));      // lần đầu + hỏi lại 2 lần
    }

    void appControlReachesSimAndDebugRuleHolds()
    {
        Wire wire;
        services::TrbControl control(&wire.link);
        trbctl::ControlCmd c;
        c.debugMode = true;
        QCOMPARE(control.sendControl(0, 1, c), services::TrbControl::Check::Ok);
        QTRY_VERIFY_WITH_TIMEOUT(wire.world.trb(0, 1)->debug(), 1000);
        QCOMPARE(control.sendControl(0, 2, c), services::TrbControl::Check::OtherInDebug);  // app chặn trước khi gửi
        QTest::qWait(50);
        QVERIFY(!wire.world.trb(0, 2)->debug());
        QCOMPARE(wire.world.stats().debugConflicts, quint64(0));
    }

    void debugFramesReachAppMonitor()
    {
        Wire wire;
        model::DeviceStore store(2, 8);
        services::TrbMonitor monitor(&wire.link, &store);
        connect(&wire.link, &core::Link::frameReceived, &monitor, &services::TrbMonitor::onFrame);
        services::TrbControl control(&wire.link);
        trbctl::ControlCmd c;
        c.debugMode = true;
        control.sendControl(1, 2, c);
        wire.world.setDebugPeriodMs(20);
        QSignalSpy updated(&monitor, &services::TrbMonitor::trbUpdated);
        QTRY_VERIFY_WITH_TIMEOUT(updated.size() >= 1, 2000);
        QCOMPARE(updated.last().at(0).toInt(), 1);
        QCOMPARE(updated.last().at(1).toInt(), 2);
    }

    void faultInjectionDropsAndCorrupts()
    {
        FakeTransport t;
        sim::SimWorld w(1, 1, 1);
        sim::BusPort port(&t, &w);
        t.open();
        sim::Faults f;
        f.dropPercent = 100;
        port.setFaults(f);
        t.inject(trbmon::buildPoll(0, 0));
        QTest::qWait(30);
        QVERIFY(t.written.isEmpty());
        QCOMPARE(port.stats().dropped, quint64(1));

        f.dropPercent = 0;
        f.corruptPercent = 100;
        port.setFaults(f);
        t.inject(trbmon::buildPoll(0, 0));
        QTRY_COMPARE_WITH_TIMEOUT(t.written.size(), 1, 500);
        QVERIFY(!crcOk(t.written.first(), trbmon::kCrcStart));   // đúng độ dài nhưng sai CRC
        QCOMPARE(t.written.first().size(), trbmon::kLength);
    }

    void badCrcRequestIsIgnored()
    {
        FakeTransport t;
        sim::SimWorld w(1, 1, 1);
        sim::BusPort port(&t, &w);
        t.open();
        QByteArray p = trbmon::buildPoll(0, 0);
        p[p.size() - 4] = char(p.at(p.size() - 4) ^ 0x55);
        t.inject(p);
        QTest::qWait(30);
        QVERIFY(t.written.isEmpty());
        QCOMPARE(w.stats().polls, quint64(0));
        t.inject(trbmon::buildPoll(0, 0));                       // khung đúng ngay sau đó vẫn được trả lời
        QTRY_COMPARE_WITH_TIMEOUT(t.written.size(), 1, 500);
    }
};

QTEST_MAIN(SimTest)
#include "sim_test.moc"
