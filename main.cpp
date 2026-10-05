#include "app/context.h"
#include "core/replay_transport.h"
#include "proto/trb_monitor_proto.h"
#include "services/trb_monitor.h"
#include "ui/auth.h"
#include "ui/main_window.h"
#include <QApplication>
#include <QFile>
#include <QThread>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setStyle(QStringLiteral("Fusion"));
    qRegisterMetaType<model::Status>();

    AppContext ctx;
    ctx.settings = Settings::load();
    const Settings &cfg = ctx.settings;

    // Mọi service đăng ký CMD nhận của mình tại đây, trước khi link chạy.
    core::FrameRegistry registry;
    services::TrbMonitor::registerFrames(registry, cfg.trbCheckCrc);
    services::TrbConfig::registerFrames(registry, cfg.trbCheckCrc);
    services::FpgaOta::registerFrames(registry);
    services::StmOta::registerFrames(registry);

    // Hai đường truyền, chung một thread I/O. Thêm đường mới = thêm một transport + một Link ở đây.
    core::Transport *monitorTransport;
    if (cfg.monitorReplayFile.isEmpty())
        monitorTransport = ctx.tcp = new core::TcpServerTransport(QHostAddress(cfg.monitorAddress), cfg.monitorPort);
    else
        monitorTransport = new core::ReplayTransport(cfg.monitorReplayFile, cfg.replayIntervalMs, true);
    ctx.monitorLink = new core::Link(QStringLiteral("monitor"), monitorTransport, &registry);
    ctx.serial = new core::SerialTransport(cfg.servicePort, cfg.serviceBaud);
    ctx.serviceLink = new core::Link(QStringLiteral("service"), ctx.serial, &registry);
    ctx.serviceLink->setIdleResetMs(20); // RS485 hỏi-đáp: bỏ khung cụt để không chặn câu trả lời kế tiếp

    QThread ioThread;
    for (core::Link *l : {ctx.monitorLink, ctx.serviceLink}) {
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

    services::AlarmEngine alarms(&thresholds, &proto::trbmon::table());
    services::TrbMonitor trbMonitor(ctx.monitorLink, &store, &alarms);
    services::TrbControl trbControl(ctx.monitorLink);
    services::TrbConfig trbConfig(ctx.serviceLink, &thresholds, cfg.thresholdsFile, cfg.cfgReadTimeoutMs, cfg.cfgWriteGapMs);
    services::CsvLogger logger(cfg.logDir, cfg.logPeriodMs, qint64(cfg.logMaxFileMb) * 1024 * 1024, &proto::trbmon::table());
    services::FpgaOta fpgaOta(ctx.serviceLink);
    services::StmOta stmOta(ctx.serviceLink);
    ctx.fpgaOta = &fpgaOta;
    ctx.stmOta = &stmOta;
    ui::Auth auth(cfg.passwordHash, cfg.lockMinutes);
    ctx.trbControl = &trbControl;
    ctx.trbConfig = &trbConfig;
    ctx.logger = cfg.logEnabled ? &logger : nullptr;
    ctx.auth = &auth;

    if (cfg.logEnabled)
        QObject::connect(&trbMonitor, &services::TrbMonitor::trbUpdated, &logger, [&](int mb, int trb, bool changed) {
            logger.logTrb(mb, trb, store.trb(mb, trb), changed);
        });

    ui::MainWindow window(ctx);
    QObject::connect(&alarms, &services::AlarmEngine::alarmEvent, &window, [&](int mb, int trb, const QString &t, bool) {
        window.logEvent(QStringLiteral("MB%1 / TRB%2: %3").arg(mb).arg(trb).arg(t));
    });
    QObject::connect(&logger, &services::CsvLogger::errorOccurred, &window, &ui::MainWindow::logEvent);
    if (!thresholdError.isEmpty()) window.logEvent(thresholdError);
    window.showMaximized();

    ioThread.start();
    QMetaObject::invokeMethod(ctx.monitorLink, &core::Link::start); // TCP tự lắng nghe; cổng COM mở bằng nút
    const int rc = app.exec();
    ioThread.quit();
    ioThread.wait();
    return rc;
}
