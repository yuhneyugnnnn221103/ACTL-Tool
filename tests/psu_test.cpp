// Test phần PSU: bảng trường, khung điều khiển, giám sát (qua Link), cấu hình (qua Link với PSU giả lập).
#include "../core/link.h"
#include "../model/psu_store.h"
#include "../proto/psu_config_proto.h"
#include "../proto/psu_control_proto.h"
#include "../proto/psu_monitor_proto.h"
#include "../services/fpga_ota.h"
#include "../services/psu_config.h"
#include "../services/psu_monitor.h"
#include "../services/stm_ota.h"
#include "../services/trb_config.h"
#include "../services/trb_monitor.h"
#include "test_support.h"
#include <QSignalSpy>
#include <QtTest>
#include <functional>

using namespace proto;
using testsupport::FakeTransport;
using testsupport::crcOk;

namespace {

QByteArray monitorFrame(int addr, const QList<double> &values)
{
    QByteArray f(psumon::kLength, 0);
    f[2] = char(psumon::kCmd);
    f[3] = char(addr);
    psumon::table().encode(values, f);
    core::seal(f, psumon::kCrcStart);
    return f;
}

QByteArray configReply(int addr, const QList<double> &values)
{
    QByteArray f(psucfg::kLength, 0);
    f[2] = char(psucfg::kReplyCmd);
    f[3] = char(addr);
    psucfg::table().encode(values, f);
    core::seal(f, psucfg::kCrcStart);
    return f;
}

// PSU giả lập trên RS485: nhớ cấu hình đã ghi, trả lời lệnh đọc.
struct FakePsu {
    QHash<int, QList<double>> config;
    QByteArray lastWrite;
    int corruptField = -1;   // >= 0: lưu sai trường này để thử đọc lại không khớp

    void attach(FakeTransport *t)
    {
        t->onWrite = [this, t](const QByteArray &d) {
            const int addr = quint8(d.at(psucfg::kOffAddr));
            if (quint8(d.at(2)) == psucfg::kWriteCmd) {
                lastWrite = d;
                config[addr] = psucfg::table().decode(d);
                if (corruptField >= 0) config[addr][corruptField] += 1;
            } else if (quint8(d.at(2)) == psucfg::kReadCmd && config.contains(addr)) {
                const QByteArray reply = configReply(addr, config.value(addr));
                QTimer::singleShot(0, t, [t, reply] { t->inject(reply); });
            }
        };
    }
};

}

class PsuTest : public QObject {
    Q_OBJECT
private slots:
    void monitorTableLayout()
    {
        const FieldTable &t = psumon::table();
        QCOMPARE(t.size(), psumon::kNumFields);
        int end = 4;
        for (const Field &f : t.fields()) { QCOMPARE(f.offset, end); end += f.size; }
        QCOMPARE(end, psumon::kLength - 4 - 24);
        QCOMPARE(t.fields().at(psumon::clusterField(1, psumon::TempFet)).offset, 4 + 48 + 18 + 6 + 20); // cụm 2, dưới 3 ADC + AMC + 10 XDP
        QCOMPARE(t.fields().at(psumon::kIdxSupply0).offset, 4 + 4 * 48);
        QCOMPARE(t.fields().at(psumon::kIdxTrip0).offset, 4 + 4 * 48 + 24 + 7);
        QCOMPARE(psumon::clusterFieldLabel(psumon::TempFet), QStringLiteral("Nhiệt độ FET"));
    }

    void configTableLayout()
    {
        const FieldTable &t = psucfg::table();
        int end = 6;
        for (const Field &f : t.fields()) { QCOMPARE(f.offset, end); end += f.size; }
        QCOMPARE(end, psucfg::kLength - 4 - 16);
        // Cụm 2 bắt đầu sau 6 byte tiêu đề + 192 byte của cụm 1; INA1 ở byte 774 (STT 775).
        QCOMPARE(t.fields().at(t.indexOf("Cụm 2 - Ngưỡng", "I_OUT_ADC1_MAX")).offset, 6 + 192);
        QCOMPARE(t.fields().at(t.indexOf("INA", "INA1_CONFIG")).offset, 6 + 4 * 192);
        QCOMPARE(t.fields().at(t.indexOf("Supply - Ngưỡng", "I_DCM_MAX")).offset, 6 + 4 * 192 + 72);
        QCOMPARE(t.fields().at(t.indexOf("Cụm 1 - XDP", "OPERATION")).offset, 6 + 60);
        QCOMPARE(t.fields().at(t.indexOf("Cụm 1 - ADS", "MODE")).offset, 6 + 60 + 38);
        QCOMPARE(t.fields().at(t.indexOf("Cụm 4 - ADS", "CH7_GCAL_LSB")).offset, 6 + 3 * 192 + 190);
        QVERIFY(t.fields().at(t.indexOf("Cụm 1 - XDP", "RETRY")).hex);
        QVERIFY(!t.fields().at(t.indexOf("Cụm 1 - Ngưỡng", "I_OUT_ADC1_MAX")).hex);
    }

    void thresholdMapCoversEveryLimit()
    {
        const auto &map = psucfg::thresholdMap();
        QCOMPARE(map.size(), 4 * 11 + 8);
        QSet<int> seen;
        for (const auto &m : map) {
            QVERIFY(m.monitorField >= 0 && m.monitorField < psumon::kNumFields);
            QVERIFY(!seen.contains(m.monitorField));
            seen.insert(m.monitorField);
            QVERIFY(psucfg::table().fields().at(m.cfgMax).name.endsWith("_MAX"));
            QVERIFY(psucfg::table().fields().at(m.cfgMin).name.endsWith("_MIN"));
        }
        // Giá trị không có ngưỡng trong bản tin cấu hình thì không được ánh xạ.
        QVERIFY(!seen.contains(psumon::clusterField(0, psumon::TempFet)));
        QVERIFY(!seen.contains(psumon::clusterField(0, psumon::VInXdpPeak)));
        QVERIFY(!seen.contains(psumon::kIdxTrip0));
    }

    void defaultsFromVendor()
    {
        const QList<double> d = psucfg::defaultValues();
        const FieldTable &t = psucfg::table();
        for (int c = 1; c <= 4; ++c) {
            const QString g = QStringLiteral("Cụm %1 - XDP").arg(c);
            QCOMPARE(d.at(t.indexOf(g, "OPERATION")), double(0x80));
            QCOMPARE(d.at(t.indexOf(g, "ENABLE_FAULTS")), double(0x3EFF));
            QCOMPARE(d.at(t.indexOf(g, "MASK_FAULTS")), double(0x3EFF));
            QCOMPARE(d.at(t.indexOf(g, "RETRY")), double(0x3FBF));
            QCOMPARE(d.at(t.indexOf(g, "MODE")), 0.0);
        }
    }

    void controlFrame()
    {
        const QByteArray f = psuctl::buildControl(3, {0b0101, true});
        QCOMPARE(f.size(), 12);
        QCOMPARE(f.left(8).toHex(), QByteArray("abcd01030501" "0000"));
        QCOMPARE(f.right(2).toHex(), QByteArray("e1e2"));
        QVERIFY(crcOk(f, 2));
        const QByteArray g = psuctl::buildControl(1, {0xFF, false});
        QCOMPARE(quint8(g.at(4)), quint8(0x0F)); // chỉ 4 bit thấp
        QCOMPARE(quint8(g.at(5)), quint8(0));
    }

    void readRequestAndWriteFrames()
    {
        const QByteArray r = psucfg::buildReadRequest(2);
        QCOMPARE(r.size(), 12);
        QCOMPARE(r.left(4).toHex(), QByteArray("abcd0302"));
        QVERIFY(crcOk(r, 2));

        QList<double> v = psucfg::defaultValues();
        const QByteArray w = psucfg::buildWrite(2, v);
        QCOMPARE(w.size(), 914);
        QCOMPARE(w.left(6).toHex(), QByteArray("abcd0402ffff"));
        QVERIFY(crcOk(w, 2));
        QCOMPARE(psucfg::table().decode(w), v);
    }

    void allFramesCanBeRegistered()
    {
        core::FrameRegistry registry;
        QVERIFY(services::TrbMonitor::registerFrames(registry, true));
        QVERIFY(services::TrbConfig::registerFrames(registry, true));
        QVERIFY(services::PsuMonitor::registerFrames(registry, true));
        QVERIFY(services::PsuConfig::registerFrames(registry, true));
        QVERIFY(services::FpgaOta::registerFrames(registry));
        QVERIFY(services::StmOta::registerFrames(registry));
    }

    void monitorUpdatesStoreAndAlarms()
    {
        core::FrameRegistry registry;
        services::PsuMonitor::registerFrames(registry, true);
        auto *tp = new FakeTransport;
        core::Link link("monitor", tp, &registry);
        link.start();

        model::PsuStore store(1, 5);
        model::Thresholds thr(&psumon::table());
        services::AlarmEngine alarms(&thr, &psumon::table());
        services::PsuMonitor mon(&link, &store, &alarms);

        const int tempAdc = psumon::clusterField(2, psumon::TempAdc2);       // có ngưỡng
        const int tempFet = psumon::clusterField(2, psumon::TempFet);        // không có ngưỡng: chỉ hiển thị
        thr.set(3, 0, tempAdc, {100, 2000});

        QList<double> v(psumon::kNumFields, 0.0);
        v[tempAdc] = 500;
        v[tempFet] = 60000;
        v[psumon::kIdxRtc0] = 26;
        tp->inject(monitorFrame(3, v));
        QCOMPARE(store.psu(3).status, model::Status::Ok);
        QCOMPARE(store.psu(3).values.at(tempAdc), 500.0);
        QCOMPARE(store.psu(3).values.at(tempFet), 60000.0);
        QVERIFY(store.psu(3).alarms.isEmpty());
        QCOMPARE(store.psu(1).status, model::Status::NoData);

        v[tempAdc] = 5000;
        tp->inject(monitorFrame(3, v));
        QCOMPARE(store.psu(3).status, model::Status::Warning);
        QCOMPARE(store.psu(3).alarms, QList<int>{tempAdc});

        v[psumon::kIdxTrip0 + 4] = 7;
        tp->inject(monitorFrame(3, v));
        QCOMPARE(store.psu(3).status, model::Status::Trip);   // Trip ưu tiên hơn Quá ngưỡng

        v.fill(0);                                            // PSU đã clear trip
        v[tempAdc] = 500;
        tp->inject(monitorFrame(3, v));
        QCOMPARE(store.psu(3).status, model::Status::Ok);

        tp->inject(monitorFrame(9, v));                       // ngoài dải địa chỉ
        QCOMPARE(mon.badAddressFrames(), quint64(1));

        QByteArray bad = monitorFrame(2, v);
        bad[100] = char(bad.at(100) ^ 0x55);                  // hỏng CRC
        tp->inject(bad);
        QCOMPARE(store.psu(2).status, model::Status::NoData);

        store.markStale(store.psu(3).lastSeenMs + 5000, 3000);
        QCOMPARE(store.psu(3).status, model::Status::Lost);
    }

    void clusterStatusFollowsAlarmsPerCluster()
    {
        model::PsuStore store(1, 5);
        QCOMPARE(store.clusterStatus(2, 0), model::Status::NoData);
        const int c1 = psumon::clusterField(1, psumon::TempFet);
        store.updatePsu(2, QList<double>(psumon::kNumFields, 0.0), {}, 1000, model::Status::Warning, {c1});
        QCOMPARE(store.clusterStatus(2, 0), model::Status::Ok);
        QCOMPARE(store.clusterStatus(2, 1), model::Status::Warning);
        QCOMPARE(store.clusterAlarms(2, 1), QList<int>{c1});
        QVERIFY(store.clusterAlarms(2, 3).isEmpty());
        // Trip cấp PSU không gán cho cụm nào; cụm vẫn theo ngưỡng của chính nó.
        store.updatePsu(2, QList<double>(psumon::kNumFields, 0.0), {}, 2000, model::Status::Trip, {});
        QCOMPARE(store.clusterStatus(2, 1), model::Status::Ok);
        store.markStale(10000, 3000);
        for (int c = 0; c < 4; ++c) QCOMPARE(store.clusterStatus(2, c), model::Status::Lost);
    }

    void configWriteVerifyAndThresholds()
    {
        core::FrameRegistry registry;
        services::PsuConfig::registerFrames(registry, true);
        auto *tp = new FakeTransport;
        core::Link link("service", tp, &registry);
        link.start();
        FakePsu psu;
        psu.attach(tp);

        model::Thresholds thr(&psumon::table());
        services::PsuConfig cfg(&link, &thr, QString(), 500, 0);
        QSignalSpy finished(&cfg, &services::PsuConfig::finished);
        QSignalSpy device(&cfg, &services::PsuConfig::deviceFinished);

        QList<double> v = psucfg::defaultValues();
        const int maxIdx = psucfg::table().indexOf("Cụm 3 - Ngưỡng", "TEMP_ADC2_MAX");
        const int minIdx = psucfg::table().indexOf("Cụm 3 - Ngưỡng", "TEMP_ADC2_MIN");
        v[maxIdx] = 3000;
        v[minIdx] = 200;
        cfg.writeFull(2, v);
        QVERIFY(finished.wait(3000));
        QCOMPARE(finished.last().at(0).toInt(), 1);
        QCOMPARE(finished.last().at(1).toInt(), 0);
        QVERIFY2(device.last().at(1).toBool(), qPrintable(device.last().at(2).toString()));
        QCOMPARE(psu.config.value(2), v);
        QVERIFY(cfg.cached(2) && *cfg.cached(2) == v);

        const int mon = psumon::clusterField(2, psumon::TempAdc2);
        QCOMPARE(thr.get(2, 0, mon).min, 200.0);
        QCOMPARE(thr.get(2, 0, mon).max, 3000.0);
        QVERIFY(!thr.get(3, 0, mon).isSet());                 // PSU khác không bị ảnh hưởng
    }

    void configVerifyDetectsMismatch()
    {
        core::FrameRegistry registry;
        services::PsuConfig::registerFrames(registry, true);
        auto *tp = new FakeTransport;
        core::Link link("service", tp, &registry);
        link.start();
        FakePsu psu;
        psu.corruptField = 5;
        psu.attach(tp);

        model::Thresholds thr(&psumon::table());
        services::PsuConfig cfg(&link, &thr, QString(), 500, 0);
        QSignalSpy finished(&cfg, &services::PsuConfig::finished);
        QSignalSpy device(&cfg, &services::PsuConfig::deviceFinished);
        cfg.writeFull(1, psucfg::defaultValues());
        QVERIFY(finished.wait(3000));
        QCOMPARE(finished.last().at(1).toInt(), 1);
        QVERIFY(device.last().at(2).toString().contains("KHÔNG khớp"));
    }

    void configWriteFullToManyDevices()
    {
        core::FrameRegistry registry;
        services::PsuConfig::registerFrames(registry, true);
        auto *tp = new FakeTransport;
        core::Link link("service", tp, &registry);
        link.start();
        FakePsu psu;
        psu.attach(tp);
        psu.config[2] = QList<double>(psucfg::table().size(), 9.0);   // cấu hình cũ phải bị ghi đè hoàn toàn

        model::Thresholds thr(&psumon::table());
        services::PsuConfig cfg(&link, &thr, QString(), 500, 0);
        QSignalSpy finished(&cfg, &services::PsuConfig::finished);

        const QList<double> v = psucfg::defaultValues();
        cfg.writeFullMany({1, 2, 3}, v);
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.last().at(0).toInt(), 3);
        QCOMPARE(finished.last().at(1).toInt(), 0);
        for (int a : {1, 2, 3}) QCOMPARE(psu.config.value(a), v);
    }

    void configReadTimeoutAndBatchApply()
    {
        core::FrameRegistry registry;
        services::PsuConfig::registerFrames(registry, true);
        auto *tp = new FakeTransport;
        core::Link link("service", tp, &registry);
        link.start();
        FakePsu psu;
        psu.attach(tp);

        // PSU 1 và 2 có cấu hình khác nhau; PSU 4 không trả lời.
        QList<double> a = psucfg::defaultValues(), b = a;
        const int ina = psucfg::table().indexOf("INA", "INA2_SHUNT_CAL");
        const int tmr = psucfg::table().indexOf("Cụm 4 - XDP", "V_TMR");
        a[ina] = 111;
        b[ina] = 222;
        psu.config[1] = a;
        psu.config[2] = b;

        model::Thresholds thr(&psumon::table());
        services::PsuConfig cfg(&link, &thr, QString(), 200, 0);
        QSignalSpy finished(&cfg, &services::PsuConfig::finished);

        cfg.applyChanges({1, 2, 4}, {{tmr, 0x1234}});
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.last().at(0).toInt(), 2);
        QCOMPARE(finished.last().at(1).toInt(), 1);
        QCOMPARE(psu.config[1].at(tmr), double(0x1234));
        QCOMPARE(psu.config[2].at(tmr), double(0x1234));
        QCOMPARE(psu.config[1].at(ina), 111.0);   // trường không sửa được giữ nguyên theo từng PSU
        QCOMPARE(psu.config[2].at(ina), 222.0);
        QVERIFY(!psu.config.contains(4));
    }
};

QTEST_MAIN(PsuTest)
#include "psu_test.moc"
