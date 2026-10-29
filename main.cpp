#include "app/context.h"
#include "core/replay_transport.h"
#include "proto/psu_monitor_proto.h"
#include "proto/trb_monitor_proto.h"
#include "services/psu_monitor.h"
#include "services/trb_monitor.h"
#include "ui/auth.h"
#include "ui/main_window.h"
#include "ui/theme.h"
#include <QApplication>
#include <QFile>
#include <QThread>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    ui::theme::apply(app); // Fusion + font + style.qss (resources/style.qss)
    qRegisterMetaType<model::Status>();

    AppContext ctx;
    ctx.settings = Settings::load();
    const Settings &cfg = ctx.settings;

    // Mọi service đăng ký CMD nhận của mình tại đây, trước khi link chạy.
    core::FrameRegistry registry;
    services::TrbMonitor::registerFrames(registry, cfg.trbCheckCrc);
    services::TrbConfig::registerFrames(registry, cfg.trbCheckCrc);
    services::PsuMonitor::registerFrames(registry, cfg.psuCheckCrc);
    services::PsuConfig::registerFrames(registry, cfg.psuCheckCrc);
    services::FpgaOta::registerFrames(registry);
    services::StmOta::registerFrames(registry);

    // Một đường truyền duy nhất (Ethernet tới Gateway) chạy trên thread I/O riêng: giám sát, điều khiển, cấu hình, nạp code, Calib đều đi đường này.
    core::Transport *monitorTransport;
    if (cfg.monitorReplayFile.isEmpty())
        monitorTransport = ctx.tcp = new core::TcpClientTransport(cfg.monitorAddress, cfg.monitorPort);
    else
        monitorTransport = new core::ReplayTransport(cfg.monitorReplayFile, cfg.replayIntervalMs, true);
    ctx.monitorLink = new core::Link(QStringLiteral("monitor"), monitorTransport, &registry);

    QThread ioThread;
    for (core::Link *l : {ctx.monitorLink}) {
        l->moveToThread(&ioThread);
        QObject::connect(&ioThread, &QThread::finished, l, &QObject::deleteLater);
    }

    model::DeviceStore store(cfg.mbCount, cfg.trbPerMb);
    model::Thresholds thresholds(&proto::trbmon::table());
    QString thresholdError;
    if (!QFile::exists(cfg.thresholdsFile)) thresholds.save(cfg.thresholdsFile);
    else if (!thresholds.load(cfg.thresholdsFile, &thresholdError)) thresholdError = "Không đọc được file ngưỡng: " + thresholdError;
    ctx.store = &store;
    ctx.thresholds = &thresholds;

    // PSU: ngưỡng khóa theo (địa chỉ PSU, 0), cùng cơ chế với TRB nhưng file riêng.
    model::PsuStore psuStore(cfg.psuFirstAddr, cfg.psuCount);
    model::Thresholds psuThresholds(&proto::psumon::table());
    QString psuThresholdError;
    if (!QFile::exists(cfg.psuThresholdsFile)) psuThresholds.save(cfg.psuThresholdsFile);
    else if (!psuThresholds.load(cfg.psuThresholdsFile, &psuThresholdError))
        psuThresholdError = "Không đọc được file ngưỡng PSU: " + psuThresholdError;
    ctx.psuStore = &psuStore;
    ctx.psuThresholds = &psuThresholds;

    services::AlarmEngine alarms(&thresholds, &proto::trbmon::table());
    services::TrbMonitor trbMonitor(ctx.monitorLink, &store, &alarms);
    trbMonitor.setEmptyLimit(cfg.emptyFramesToLost);
    services::TrbControl trbControl(ctx.monitorLink);
    services::TrbConfig trbConfig(ctx.monitorLink, &thresholds, cfg.thresholdsFile, cfg.cfgReadTimeoutMs, cfg.cfgWriteGapMs);
    services::AlarmEngine psuAlarms(&psuThresholds, &proto::psumon::table());
    services::PsuMonitor psuMonitor(ctx.monitorLink, &psuStore, &psuAlarms);
    psuMonitor.setEmptyLimit(cfg.emptyFramesToLost);
    services::PsuControl psuControl(ctx.monitorLink);
    services::PsuConfig psuConfig(ctx.monitorLink, &psuThresholds, cfg.psuThresholdsFile, cfg.cfgReadTimeoutMs, cfg.cfgWriteGapMs);
    services::LogRecorder logger(cfg.logDir, cfg.logPeriodMs);
    services::TrbCalib trbCalib(ctx.monitorLink);
    ctx.trbCalib = &trbCalib;
    services::FpgaOta fpgaOta(ctx.monitorLink);
    services::StmOta stmOta(ctx.monitorLink);
    ctx.fpgaOta = &fpgaOta;
    ctx.stmOta = &stmOta;
    ui::Auth auth(cfg.passwordHash, cfg.lockMinutes);
    ctx.trbControl = &trbControl;
    ctx.psuControl = &psuControl;
    ctx.psuConfig = &psuConfig;
    ctx.trbConfig = &trbConfig;
    logger.setRecording(cfg.logAutoStart);
    ctx.logger = cfg.logEnabled ? &logger : nullptr;
    ctx.auth = &auth;

    if (cfg.logEnabled)
        QObject::connect(&psuMonitor, &services::PsuMonitor::psuUpdated, &logger, [&](int addr, bool changed) {
            logger.logPsu(addr, psuStore.psu(addr), changed);
        });
    if (cfg.logEnabled)
        QObject::connect(&trbMonitor, &services::TrbMonitor::trbUpdated, &logger, [&](int mb, int trb, bool changed) {
            logger.logTrb(mb, trb, store.trb(mb, trb), changed);
        });

    ui::MainWindow window(ctx);
    QObject::connect(&trbMonitor, &services::TrbMonitor::powerGoodChanged, &window, [&](int mb, int trb, int bits) {
        QStringList bad;
        for (int i = 0; i < 4; ++i) if (bits >> i & 1) bad << QStringLiteral("TRM%1").arg(i + 1);
        window.logEvent(ui::tagged(ui::EventKind::Pg, QStringLiteral("MB%1 / TRB%2: %3").arg(mb + 1).arg(trb + 1)
                            .arg(bits ? QStringLiteral("mất PG (Power Good) %1, tính là trip").arg(bad.join(", "))
                                      : QStringLiteral("PG đã tốt trở lại"))),
                        ui::LogTrbMonitor, mb, trb);
    });
    QObject::connect(&alarms, &services::AlarmEngine::alarmEvent, &window, [&](int mb, int trb, const QString &t, bool raised) {
        window.logEvent(ui::tagged(raised ? ui::EventKind::Warn : ui::EventKind::Ok, QStringLiteral("MB%1 / TRB%2: %3").arg(mb + 1).arg(trb + 1).arg(t)),
                        ui::LogTrbMonitor, mb, trb);
    });
    QObject::connect(&psuAlarms, &services::AlarmEngine::alarmEvent, &window, [&](int addr, int, const QString &t, bool raised) {
        window.logEvent(ui::tagged(raised ? ui::EventKind::Warn : ui::EventKind::Ok, QStringLiteral("PSU %1: %2").arg(addr).arg(t)),
                        ui::LogPsuMonitor);
    });
    QObject::connect(&logger, &services::LogRecorder::errorOccurred, &window, [&](const QString &m) { window.logEvent(ui::tagged(ui::EventKind::Err, m)); });
    if (!thresholdError.isEmpty()) window.logEvent(ui::tagged(ui::EventKind::Err, thresholdError));
    if (!psuThresholdError.isEmpty()) window.logEvent(ui::tagged(ui::EventKind::Err, psuThresholdError));
    window.showMaximized();

    ioThread.start();
    QMetaObject::invokeMethod(ctx.monitorLink, &core::Link::start); // TCP tự nối tới Gateway (và nối lại khi mất); cổng COM mở bằng nút
    const int rc = app.exec();
    ioThread.quit();
    ioThread.wait();
    return rc;
}
