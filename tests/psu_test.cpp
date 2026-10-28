// Test phần PSU: bảng trường, khung điều khiển, giám sát (qua Link), cấu hình (qua Link với PSU giả lập).
#include "../core/link.h"
#include "../model/device_store.h"
#include "../model/psu_store.h"
#include "../proto/device_select_proto.h"
#include "../proto/psu_config_proto.h"
#include "../proto/psu_control_proto.h"
#include "../proto/psu_monitor_proto.h"
#include "../proto/trb_monitor_proto.h"
#include "../proto/stm_ota_proto.h"
#include "../core/frame_parser.h"
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
    f.replace(2, 2, psumon::cmd());
    f[psumon::kOffAddr] = char(addr);
    psumon::table().encode(values, f);
    core::seal(f, psumon::kCrcStart);
    return f;
}

QByteArray configReply(int addr, const QList<double> &values)
{
    QByteArray f(psucfg::kLength, 0);
    f.replace(2, 2, psucfg::replyCmd());
    f[psucfg::kOffAddr] = char(addr);
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
        t->onWrite = [this, t](const QByteArray &raw) {
            QVERIFY(quint8(raw.at(2)) == devsel::Psu);                      // byte thiết bị; vai Gateway bỏ đi rồi mới tới PSU
            const QByteArray d = devsel::strip(raw);
            const int addr = quint8(d.at(psucfg::kOffAddr));
            if (d.mid(2, 2) == psucfg::writeCmd()) {
                lastWrite = d;
                config[addr] = psucfg::table().decode(d);
                if (corruptField >= 0) config[addr][corruptField] += 1;
            } else if (d.mid(2, 2) == psucfg::readCmd() && config.contains(addr)) {
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
        int end = psumon::kOffAddr + 1;
        for (int i = 0; i < psumon::kIdxExt0; ++i) { const Field &f = t.fields().at(i); QCOMPARE(f.offset, end); end += f.size; }
        QCOMPARE(psumon::kLength, 310);
        QCOMPARE(end, 5 + 4 * 48 + 24 + 7 + 10);             // hết Trip; vùng mở rộng firmware 24 byte (chỉ dùng 5 trường) nằm sau
        QCOMPARE(t.fields().at(psumon::clusterField(1, psumon::TempFet)).offset, 5 + 48 + 18 + 6 + 20); // cụm 2, dưới 3 ADC + AMC + 10 XDP
        QCOMPARE(t.fields().at(psumon::kIdxSupply0).offset, 5 + 4 * 48);
        QCOMPARE(t.fields().at(psumon::kIdxTrip0).offset, 5 + 4 * 48 + 24 + 7);
        QCOMPARE(psumon::clusterFieldLabel(psumon::TempFet), QStringLiteral("Nhiệt độ FET"));
    }

    void monitorFirmwareExtensionLayout()
    {
        const FieldTable &t = psumon::table();
        auto first = [&](int idx) { return t.fields().at(idx).offset + 1; };    // byte đếm từ 1
        QCOMPARE(first(psumon::kIdxSafeState(0)), 239);                         // cụm 1: +0 safe, +1 EN, +2 trip
        QCOMPARE(first(psumon::kIdxEn(0)), 240);
        QCOMPARE(first(psumon::kIdxTripCount(0)), 241);
        QCOMPARE(first(psumon::kIdxSafeState(3)), 248);                         // cụm 4: +9
        QCOMPARE(first(psumon::kIdxTripCount(3)), 250);
        QCOMPARE(first(psumon::kIdxSlot), 251);                                 // +12
        QCOMPARE(first(psumon::kIdxVersion), 252);                              // +13..+16
        QCOMPARE(t.fields().at(psumon::kIdxVersion).size, 4);

        QList<double> v(psumon::kNumFields, 0.0);
        v[psumon::kIdxSafeState(2)] = psumon::SafeOn;
        v[psumon::kIdxSlot] = 'B';
        v[psumon::kIdxVersion] = 20261013;
        const QByteArray f = psumon::buildFrame(2, v);
        QCOMPARE(f.size(), 310);
        QVERIFY(crcOk(f, 2));
        const QList<double> back = t.decode(f);
        QCOMPARE(back.at(psumon::kIdxSafeState(2)), double(psumon::SafeOn));
        QCOMPARE(back.at(psumon::kIdxSlot), double('B'));
        QCOMPARE(back.at(psumon::kIdxVersion), 20261013.0);
        QCOMPARE(psumon::safeStateText(psumon::SafeTripped), QStringLiteral("TRIPPED"));
    }

    void supplyRawValuesAreSignedForCurrentOnly()
    {
        QList<double> v(psumon::kNumFields, 0.0);
        v[psumon::kIdxSupply0] = -4800;               // I_DCM: 24 bit bù 2
        v[psumon::kIdxSupply0 + 1] = 0xF00000;        // V_DCM: không dấu, bit cao 1 vẫn là số dương
        const QList<double> back = psumon::table().decode(psumon::buildFrame(1, v));
        QCOMPARE(back.at(psumon::kIdxSupply0), -4800.0);
        QCOMPARE(back.at(psumon::kIdxSupply0 + 1), 15728640.0);
        QCOMPARE(psumon::supplyShown(0x1230), 0x123 * 1.0);   // dịch phải 4 bit
        QCOMPARE(psumon::supplyShown(-16), -1.0);
        QCOMPARE(psumon::supplyShown(-17), -2.0);             // bậc thang xuống như dịch số học
    }

    void pollFrameAndTripNames()
    {
        const QByteArray p = psumon::buildPoll(4);
        QCOMPARE(p.size(), 13);
        QCOMPARE(p.mid(2, 3).toHex(), QByteArray("020204"));
        QVERIFY(crcOk(p, 2));
        QCOMPARE(psumon::tripBitName(0, 2), QStringLiteral("I_5V vượt MAX"));
        QCOMPARE(psumon::tripBitName(1, 7), QStringLiteral("V_3V3_2 dưới MIN"));
        QCOMPARE(psumon::tripBitName(2, 5), QStringLiteral("INA2 SHNTUL"));
        QCOMPARE(psumon::tripBitName(4, 4), QStringLiteral("DCM1 chân FT báo lỗi"));
        QVERIFY(psumon::tripBitName(5, 4).isEmpty());
        QVERIFY(psumon::tripBitName(7, 0).isEmpty());
    }

    void supplyThresholdUsesSignedCurrentAndZeroDisables()
    {
        const FieldTable &c = psucfg::table();
        QVERIFY(c.fields().at(c.indexOf("Supply - Ngưỡng", "I_DCM_MIN")).isSigned);
        QVERIFY(!c.fields().at(c.indexOf("Supply - Ngưỡng", "V_DCM_MIN")).isSigned);
        QList<double> v = psucfg::defaultValues();
        v[c.indexOf("Supply - Ngưỡng", "I_DCM_MIN")] = -1000;
        QCOMPARE(c.decode(psucfg::buildWrite(1, v)).at(c.indexOf("Supply - Ngưỡng", "I_DCM_MIN")), -1000.0);
        QVERIFY(model::Limit({-1000, 5000}).violatedBy(-1001));
        QVERIFY(!model::Limit({0, 5000}).violatedBy(-1));       // 0 = tắt cận dưới
        QVERIFY(!model::Limit({10, 0}).violatedBy(99999));      // 0 = tắt cận trên
    }

    void configTableLayout()
    {
        const FieldTable &t = psucfg::table();
        int end = psucfg::kOffFirstField;
        for (const Field &f : t.fields()) { QCOMPARE(f.offset, end); end += f.size; }
        QCOMPARE(end, psucfg::kLength - 4 - 16);
        // Cụm 2 bắt đầu sau 6 byte tiêu đề + 192 byte của cụm 1; INA1 ở byte 774 (STT 775).
        QCOMPARE(t.fields().at(t.indexOf("Cụm 2 - Ngưỡng", "I_OUT_ADC1_MAX")).offset, 7 + 192);
        QCOMPARE(t.fields().at(t.indexOf("INA", "INA1_CONFIG")).offset, 7 + 4 * 192);
        QCOMPARE(t.fields().at(t.indexOf("Supply - Ngưỡng", "I_DCM_MAX")).offset, 7 + 4 * 192 + 72);
        QCOMPARE(t.fields().at(t.indexOf("Cụm 1 - XDP", "OPERATION")).offset, 7 + 60);
        QCOMPARE(t.fields().at(t.indexOf("Cụm 1 - ADS", "MODE")).offset, 7 + 60 + 38);
        QCOMPARE(t.fields().at(t.indexOf("Cụm 4 - ADS", "CH7_GCAL_LSB")).offset, 7 + 3 * 192 + 190);
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
        QCOMPARE(f.size(), 13);
        QCOMPARE(f.left(9).toHex(), QByteArray("abcd0101" "03" "05" "01" "0000"));
        QCOMPARE(f.right(2).toHex(), QByteArray("e1e2"));
        QVERIFY(crcOk(f, 2));
        const QByteArray g = psuctl::buildControl(1, {0xFF, false});
        QCOMPARE(quint8(g.at(psuctl::kOffMask)), quint8(0x0F)); // chỉ 4 bit thấp
        QCOMPARE(quint8(g.at(psuctl::kOffClearTrip)), quint8(0));
    }

    void readRequestAndWriteFrames()
    {
        const QByteArray r = psucfg::buildReadRequest(2);
        QCOMPARE(r.size(), 13);
        QCOMPARE(r.left(8).toHex(), QByteArray("abcd0303" "02" "000000"));
        QVERIFY(crcOk(r, 2));

        QList<double> v = psucfg::defaultValues();
        const QByteArray w = psucfg::buildWrite(2, v);
        QCOMPARE(w.size(), 915);
        QCOMPARE(w.left(7).toHex(), QByteArray("abcd0404" "02" "ffff"));
        QVERIFY(crcOk(w, 2));
        QCOMPARE(psucfg::table().decode(w), v);
    }

    void everyPsuCommandIsTwoBytes()
    {
        // Mọi CMD phía PSU (giám sát, điều khiển, cấu hình, nạp STM32) là 2 byte, mã lặp hai lần như TRB.
        const QList<QByteArray> cmds = {psumon::cmd(), psuctl::cmd(), psucfg::readCmd(), psucfg::writeCmd(), psucfg::replyCmd(),
                                        stmota::cmdBytes(stmota::kBegin), stmota::cmdBytes(stmota::kData), stmota::cmdBytes(stmota::kEnd),
                                        stmota::cmdBytes(stmota::kCommit), stmota::cmdBytes(stmota::kAck), stmota::cmdBytes(stmota::kInfo)};
        QSet<QByteArray> seen;
        for (const QByteArray &c : cmds) {
            QCOMPARE(c.size(), 2);
            QCOMPARE(c.at(0), c.at(1));
            seen.insert(c);
        }
        QCOMPARE(seen.size(), cmds.size());
        QCOMPARE(psumon::cmd().toHex(), QByteArray("8181"));
        QCOMPARE(psucfg::replyCmd().toHex(), QByteArray("8282"));
        QCOMPARE(psumon::spec(true).cmd.size(), 2);
        QCOMPARE(psucfg::readReplySpec(true).cmd.size(), 2);
        QCOMPARE(stmota::ackSpec().cmd.size(), 2);
    }

    void stmOtaFramesLayout()
    {
        const QByteArray b = stmota::buildBegin(3, 0x01020304, 0xAABBCCDD, 7);
        QCOMPARE(b.size(), 21);
        QCOMPARE(b.left(17).toHex(), QByteArray("abcd9090" "03" "01020304" "aabbccdd" "00000007"));
        QVERIFY(crcOk(b, 2));

        const QByteArray d = stmota::buildData(2, 0x0102, QByteArray(10, 'x'));
        QCOMPARE(d.size(), 269);
        QCOMPARE(d.left(9).toHex(), QByteArray("abcd9191" "02" "0102" "000a"));
        QCOMPARE(d.mid(9, 10), QByteArray(10, 'x'));
        QCOMPARE(quint8(d.at(19)), quint8(0xFF));                 // chunk cuối đệm 0xFF
        QVERIFY(crcOk(d, 2));

        for (quint8 code : {stmota::kEnd, stmota::kCommit, stmota::kInfo}) {
            const QByteArray c = stmota::buildCtrl(5, code);
            QCOMPARE(c.size(), 9);
            QCOMPARE(c.left(5), stmota::cmdBytes(code).prepend(QByteArray::fromHex("abcd")).append(char(5)));
            QVERIFY(crcOk(c, 2));
        }
    }

    void stmOtaAckParses()
    {
        core::FrameRegistry registry;
        QVERIFY(registry.add(stmota::ackSpec()));
        QByteArray ack(17, 0);
        ack.replace(2, 2, stmota::cmdBytes(stmota::kAck));
        ack[4] = char(3);                       // địa chỉ PSU
        ack[5] = char(stmota::kInfo);           // ack_cmd vẫn là mã 1 byte
        ack[6] = char(stmota::Ok);
        ack.replace(7, 4, QByteArray::fromHex("0000002A"));
        ack[11] = 'B';
        core::seal(ack, 2);

        core::FrameParser parser(&registry);
        QList<core::Frame> got;
        parser.feed(ack, [&](const core::Frame &f) { got << f; });
        QCOMPARE(got.size(), 1);
        const stmota::Ack a = stmota::parseAck(got.first().raw);
        QCOMPARE(a.addr, quint8(3));
        QCOMPARE(a.ackCmd, stmota::kInfo);
        QCOMPARE(a.status, quint8(stmota::Ok));
        QCOMPARE(a.info, quint32(42));
        QCOMPARE(a.slot, 'B');
    }

    void fpgaStatusCarriesVersionAndWriteVersionFrame()
    {
        namespace fo = proto::fpgaota;
        QCOMPARE(fo::versionFromFileName("C:/x/20261013.bin"), quint32(20261013));
        QCOMPARE(fo::versionFromFileName("2026101302.bin"), quint32(2026101302)); // cùng ngày, thêm số thứ tự
        QCOMPARE(fo::versionFromFileName("v1.2.bin"), quint32(0));
        QCOMPARE(fo::versionFromFileName("20261013_2.bin"), quint32(0));
        QCOMPARE(fo::versionFromFileName("99999999999.bin"), quint32(0));          // quá 32 bit
        QCOMPARE(fo::versionBytes(20261013), QByteArray::fromHex("01352895"));
        QCOMPARE(fo::versionValue(fo::versionBytes(2026101302)), quint32(2026101302));
        QCOMPARE(fo::versionText(fo::versionBytes(20261013)), QString("20261013"));
        QCOMPARE(fo::versionText(QByteArray(4, '\0')), QString());
        QCOMPARE(fo::versionText(QByteArray(4, char(0xFF))), QString());

        core::FrameRegistry registry;
        QVERIFY(services::FpgaOta::registerFrames(registry));
        QByteArray reply(40, '\0');
        reply[0] = char(0xAB); reply[1] = char(0xCD); reply[2] = reply[3] = char(0x99);
        reply[4] = 1; reply[5] = 2;
        reply.replace(18, 4, fo::versionBytes(20261013));
        reply[38] = char(0xE1); reply[39] = char(0xE2);
        core::seal(reply, 4);
        core::FrameParser parser(&registry);
        QList<core::Frame> got;
        parser.feed(reply, [&](const core::Frame &f) { got << f; });
        QCOMPARE(got.size(), 1);
        const fo::Status st = fo::parseStatus(got.first().raw);
        QCOMPARE(st.code, quint8(2));
        QCOMPARE(fo::versionValue(st.version), quint32(20261013));

        const QByteArray w = fo::buildWriteVersion(2, 5, fo::versionBytes(20261014));
        QCOMPARE(w.size(), 269);
        QCOMPARE(w.mid(2, 2), QByteArray::fromHex("4444"));
        QCOMPARE(quint8(w[4]), quint8(2));
        QCOMPARE(quint8(w[5]), quint8(5));
        QCOMPARE(fo::versionValue(w.mid(6, 4)), quint32(20261014));
        QCOMPARE(w.right(2), QByteArray::fromHex("E1E2"));
        const quint16 crc = core::crc16(reinterpret_cast<const quint8 *>(w.constData()) + 4, 269 - 8);
        QCOMPARE(quint8(w[265]), quint8(crc >> 8));
        QCOMPARE(quint8(w[266]), quint8(crc & 0xFF));
    }

    // Nạp code: các gói đi liên tiếp, chỉ hỏi 0x88 sau gói cuối (cả broadcast lẫn gửi riêng).
    void fpgaLoadQueriesOnlyAfterLastPacket_data()
    {
        QTest::addColumn<bool>("broadcast");
        QTest::newRow("broadcast") << true;
        QTest::newRow("unicast") << false;
    }

    void fpgaLoadQueriesOnlyAfterLastPacket()
    {
        QFETCH(bool, broadcast);
        namespace fo = proto::fpgaota;
        core::FrameRegistry registry;
        services::FpgaOta::registerFrames(registry);
        auto *tp = new FakeTransport;
        core::Link link("service", tp, &registry);
        link.start();
        QByteArray status(40, '\0');                          // trả lời 99 99 mọi lệnh hỏi 0x88
        status[2] = status[3] = char(0x99);
        status[38] = char(0xE1); status[39] = char(0xE2);
        core::seal(status, 4);
        tp->onWrite = [&](const QByteArray &raw) { const QByteArray d = devsel::strip(raw); if (d.size() == 269 && quint8(d[2]) == 0x88) tp->injectLater(status); };

        services::FpgaOta ota(&link);
        ota.setNodes({{0, 0}, {0, 1}});
        ota.checkOnline();
        QTRY_VERIFY(!ota.busy());
        QCOMPARE(ota.nodes().size(), 2);

        services::FpgaOta::Options o;
        o.broadcast = broadcast;
        o.packetGapMs = 0;
        tp->written.clear();
        ota.load(QByteArray(256 * 3 - 10, 'x'), o);              // 3 gói
        QTRY_VERIFY_WITH_TIMEOUT(!ota.busy(), 3000);

        QList<quint8> cmds;                                      // chuỗi lệnh đã gửi
        for (const QByteArray &d : tp->written) { QCOMPARE(quint8(d[2]), quint8(devsel::Trb)); cmds << quint8(d[3]); }   // mọi khung nạp FPGA mang DEV = TRB, CMD ngay sau đó
        const int loads = broadcast ? 3 : 6;                     // gửi riêng: 3 gói x 2 TRB
        QCOMPARE(cmds.mid(0, loads), QList<quint8>(loads, 0x55));   // toàn bộ gói đi trước, không xen lệnh hỏi
        QCOMPARE(cmds.mid(loads), (QList<quint8>{0x88, 0x88}));     // rồi mới hỏi từng TRB
    }

    // Nạp STM32 theo trình tự mới: INFO -> ERASE (một ACK khi xong) -> BEGIN -> DATA... -> END -> COMMIT.
    void stmOtaFlowUsesEraseBeforeBegin()
    {
        core::FrameRegistry registry;
        QVERIFY(services::StmOta::registerFrames(registry));
        auto *tp = new FakeTransport;
        core::Link link("service", tp, &registry);
        link.start();

        auto ack = [](quint8 addr, quint8 ackCmd, quint8 status, quint32 info, char slot, quint8 trial) {
            QByteArray a(17, 0);
            a.replace(2, 2, stmota::cmdBytes(stmota::kAck));
            a[4] = char(addr); a[5] = char(ackCmd); a[6] = char(status);
            for (int i = 0; i < 4; ++i) a[7 + i] = char(info >> (24 - 8 * i));
            a[11] = slot; a[12] = char(trial);
            core::seal(a, 2);
            return a;
        };
        QList<quint8> cmds;
        tp->onWrite = [&](const QByteArray &raw) {
            const QByteArray d = devsel::strip(raw);                        // nạp STM32 gửi cho PSU (byte thiết bị 0x02), Gateway bỏ đi
            const quint8 c = quint8(d[2]);
            cmds << c;
            switch (c) {
            case stmota::kInfo:  tp->injectLater(ack(3, c, stmota::Ok, 77, 'A', 0x80)); break;
            case stmota::kErase: tp->injectLater(ack(3, c, stmota::Ok, 1, 'A', 0)); break;
            case stmota::kBegin: tp->injectLater(ack(3, c, stmota::Ok, 0, 'A', 0)); break;
            case stmota::kData:  tp->injectLater(ack(3, c, stmota::Ok, 0, 'A', 0)); break;
            case stmota::kEnd:   tp->injectLater(ack(3, c, stmota::Ok, 0, 'A', 0)); break;
            case stmota::kCommit: tp->injectLater(ack(3, c, stmota::Ok, 0, 'A', 0)); break;
            }
        };

        services::StmOta ota(&link);
        QSignalSpy info(&ota, &services::StmOta::deviceInfo), fin(&ota, &services::StmOta::deviceFinished);
        ota.start({3}, QByteArray(10, 'a'), QByteArray(300, 'b'), 20261013, true);   // đang chạy A: nạp ảnh B (300 byte = 2 gói)
        QTRY_VERIFY_WITH_TIMEOUT(!ota.busy(), 3000);
        const QList<quint8> expect{stmota::kInfo, stmota::kErase, stmota::kBegin, stmota::kData, stmota::kData, stmota::kEnd, stmota::kCommit};
        QCOMPARE(cmds, expect);
        QCOMPARE(info.size(), 1);
        QCOMPARE(info.first().at(1).toChar().toLatin1(), 'A');
        QCOMPARE(info.first().at(3).toUInt(), 0x80u);                       // trial: đã xác nhận
        QCOMPARE(fin.size(), 1);
        QVERIFY(fin.first().at(1).toBool());
    }

    void stmOtaEraseFailureAndRollback()
    {
        core::FrameRegistry registry;
        services::StmOta::registerFrames(registry);
        auto *tp = new FakeTransport;
        core::Link link("service", tp, &registry);
        link.start();
        auto ack = [](quint8 ackCmd, quint8 status, quint32 info, char slot) {
            QByteArray a(17, 0);
            a.replace(2, 2, stmota::cmdBytes(stmota::kAck));
            a[4] = char(1); a[5] = char(ackCmd); a[6] = char(status); a[10] = char(info); a[11] = slot;
            core::seal(a, 2);
            return a;
        };
        tp->onWrite = [&](const QByteArray &raw) {
            const QByteArray d = devsel::strip(raw);                        // nạp STM32 gửi cho PSU (byte thiết bị 0x02), Gateway bỏ đi
            const quint8 c = quint8(d[2]);
            if (c == stmota::kInfo) tp->injectLater(ack(c, stmota::Ok, 5, 'R'));                  // recovery
            else if (c == stmota::kErase) tp->injectLater(ack(c, stmota::EraseFail, 0x55, 'R'));
            else if (c == stmota::kRollback) tp->injectLater(ack(c, stmota::NoTarget, 0, 'A'));
        };
        services::StmOta ota(&link);
        QSignalSpy fin(&ota, &services::StmOta::deviceFinished);
        ota.start({1}, QByteArray(100, 'a'), {}, 1, true);            // recovery: dùng ảnh A
        QTRY_VERIFY_WITH_TIMEOUT(!ota.busy(), 3000);
        QCOMPARE(fin.size(), 1);
        QVERIFY(!fin.first().at(1).toBool());
        QVERIFY(fin.first().at(2).toString().contains("xóa"));

        ota.rollback({1});
        QTRY_VERIFY_WITH_TIMEOUT(!ota.busy(), 3000);
        QCOMPARE(fin.size(), 2);
        QVERIFY(!fin.last().at(1).toBool());
        QVERIFY(fin.last().at(2).toString().contains("slot còn lại"));
        QCOMPARE(stmota::statusText(stmota::NotErased), QStringLiteral("chưa ERASE slot đích"));
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
        v[psumon::kIdxEn(2)] = 1;                                            // cụm 3 đang bật nguồn: mới so ngưỡng
        v[psumon::kIdxSafeState(2)] = psumon::SafeOn;
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

    // Cụm tắt (EN = 0, không trip): không so ngưỡng, ô hiện OFF; cụm TRIPPED/LOCKOUT là Trip và kéo PSU thành Trip dù EN đã về 0.
    void offClusterIsNotCheckedAndTrippedClusterTripsPsu()
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

        const int t0 = psumon::clusterField(0, psumon::TempAdc2), t1 = psumon::clusterField(1, psumon::TempAdc2);
        thr.set(2, 0, t0, {100, 2000});                                       // ngưỡng dưới 100: giá trị 0 của cụm tắt sẽ vi phạm nếu có so
        thr.set(2, 0, t1, {100, 2000});
        QList<double> v(psumon::kNumFields, 0.0);
        v[psumon::kIdxEn(0)] = 1; v[psumon::kIdxSafeState(0)] = psumon::SafeOn; v[t0] = 500;   // cụm 1 bật, trong ngưỡng
        v[psumon::kIdxSafeState(1)] = psumon::SafeOff;                                          // cụm 2 tắt, giá trị 0
        tp->inject(monitorFrame(2, v));
        QCOMPARE(store.psu(2).status, model::Status::Ok);                    // cụm 2 không bị tính quá ngưỡng
        QVERIFY(store.psu(2).alarms.isEmpty());
        QVERIFY(store.clusterOff(2, 1));
        QVERIFY(!store.clusterOff(2, 0));
        QCOMPARE(store.clusterStatus(2, 0), model::Status::Ok);

        v[psumon::kIdxEn(1)] = 1; v[psumon::kIdxSafeState(1)] = psumon::SafeOn;   // bật cụm 2 mà giá trị vẫn 0: bây giờ vi phạm
        tp->inject(monitorFrame(2, v));
        QCOMPARE(store.psu(2).status, model::Status::Warning);
        QCOMPARE(store.clusterStatus(2, 1), model::Status::Warning);
        QVERIFY(!store.clusterOff(2, 1));

        v[psumon::kIdxEn(1)] = 0; v[psumon::kIdxSafeState(1)] = psumon::SafeTripped;   // trip: firmware cắt EN, nhưng không được hiện OFF
        tp->inject(monitorFrame(2, v));
        QVERIFY(store.clusterTripped(2, 1));
        QVERIFY(!store.clusterOff(2, 1));
        QCOMPARE(store.clusterStatus(2, 1), model::Status::Trip);
        QCOMPARE(store.clusterStatus(2, 0), model::Status::Ok);               // cụm khác không bị kéo theo
        QCOMPARE(store.psu(2).status, model::Status::Trip);                   // PSU Trip khi một cụm trip

        v[psumon::kIdxSafeState(1)] = psumon::SafeLockout;
        tp->inject(monitorFrame(2, v));
        QCOMPARE(store.clusterStatus(2, 1), model::Status::Trip);
        QCOMPARE(store.psu(2).status, model::Status::Trip);
    }

    // PSU cũng có X (khung rỗng của Gateway: AB CD 81 81 ADDR, dữ liệu và CRC toàn 0, E1 E2) và ? (quá hạn) như TRB.
    void silentPsuIsMarkedWithXAndLostLikeTrb()
    {
        core::FrameRegistry registry;
        services::PsuMonitor::registerFrames(registry, true);
        auto *tp = new FakeTransport;
        core::Link link("monitor", tp, &registry);
        link.start();
        model::PsuStore store(1, 5);
        services::PsuMonitor mon(&link, &store);
        mon.setEmptyLimit(3);
        auto empty = [](int addr) {
            QByteArray f(psumon::kLength, '\0');
            f[0] = char(0xAB); f[1] = char(0xCD);
            f.replace(2, 2, psumon::cmd());
            f[psumon::kOffAddr] = char(addr);
            f[psumon::kLength - 2] = char(0xE1); f[psumon::kLength - 1] = char(0xE2);
            return f;
        };
        QList<double> v(psumon::kNumFields, 0.0);

        tp->inject(empty(2));                                           // chưa từng có dữ liệu: X ngay, trạng thái vẫn "chưa có dữ liệu"
        QVERIFY(store.psu(2).noReply);
        QCOMPARE(store.psu(2).status, model::Status::NoData);
        QVERIFY(!store.psu(3).noReply);

        tp->inject(monitorFrame(2, v));                                  // có bản tin thật: hết X
        QVERIFY(!store.psu(2).noReply);
        QCOMPARE(store.psu(2).status, model::Status::Ok);
        tp->inject(empty(2));                                           // đã có dữ liệu: X hiện ngay dù chưa đủ 3 khung
        QVERIFY(store.psu(2).noReply);
        QCOMPARE(store.psu(2).status, model::Status::Ok);
        tp->inject(empty(2));
        tp->inject(empty(2));                                           // đủ 3 khung rỗng liên tiếp: mất kết nối, giữ số liệu cuối
        QCOMPARE(store.psu(2).status, model::Status::Lost);
        QVERIFY(store.psu(2).frames > 0);
        tp->inject(monitorFrame(2, v));
        QVERIFY(!store.psu(2).noReply);
        QCOMPARE(store.psu(2).status, model::Status::Ok);
    }

    // Gateway gửi một lượt 20 TRB + 5 PSU = 25 x 310 = 7750 byte (hai loại khung cùng 310 byte, phân biệt bằng CMD 23 23 / 81 81):
    // nhận trọn một lần hoặc bị TCP cắt ở ranh giới bất kỳ, cả 25 thiết bị đều vào đúng store.
    void mixedBurstOf20TrbAnd5Psu()
    {
        for (int chunk : {7750, 1460, 733, 1}) {
            core::FrameRegistry registry;
            services::TrbMonitor::registerFrames(registry, true);
            services::PsuMonitor::registerFrames(registry, true);
            auto *tp = new FakeTransport;
            core::Link link("monitor", tp, &registry);
            link.start();
            model::DeviceStore trbs(3, 8);
            model::PsuStore psus(1, 5);
            services::TrbMonitor trbMon(&link, &trbs, nullptr);
            services::PsuMonitor psuMon(&link, &psus);
            QList<double> tv(proto::trbmon::kNumFields, 0.0);
            tv[proto::trbmon::kIdxPg] = 0x0F;
            QList<double> pv(psumon::kNumFields, 0.0);
            QByteArray burst;
            for (int i = 0; i < 20; ++i) burst += proto::trbmon::buildFrame(i / 8, i % 8, tv);
            for (int a = 1; a <= 5; ++a) burst += monitorFrame(a, pv);
            QCOMPARE(burst.size(), 7750);
            QCOMPARE(psumon::kLength, proto::trbmon::kLength);
            for (int i = 0; i < burst.size(); i += chunk) tp->inject(burst.mid(i, chunk));
            QCOMPARE(trbs.count(model::Status::Ok), 20);
            QCOMPARE(trbs.trb(2, 3).frames, quint64(1));                // TRB thứ 20 không mất
            QCOMPARE(trbs.trb(2, 4).frames, quint64(0));
            for (int a = 1; a <= 5; ++a) QCOMPARE(psus.psu(a).frames, quint64(1));
            QCOMPARE(trbMon.badAddressFrames(), quint64(0));
            QCOMPARE(psuMon.badAddressFrames(), quint64(0));
        }
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
