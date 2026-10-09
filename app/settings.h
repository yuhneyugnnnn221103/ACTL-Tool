#pragma once
#include <QCoreApplication>
#include <QSettings>

// Cấu hình đọc từ actl_tool.ini cạnh file chạy; thiếu khóa nào thì ghi giá trị mặc định ra file.
struct Settings {
    int mbCount, trbPerMb, staleMs;
    QString monitorAddress;    // địa chỉ IP (hoặc tên máy) của Gateway; PC là TCP client
    quint16 monitorPort;
    QString monitorReplayFile; // khác rỗng: phát lại file hex thay cho TCP (để thử)
    int replayIntervalMs;
    QString servicePort;       // cổng COM của đường RS485 (cấu hình, nạp code)
    int serviceBaud;
    bool trbCheckCrc, psuCheckCrc;
    int cfgReadTimeoutMs, cfgWriteGapMs;
    bool logEnabled;
    QString logDir, thresholdsFile, psuThresholdsFile, psuDefaultConfigFile;
    int logPeriodMs, logMaxFileMb;
    int fpgaEraseWaitSec, fpgaPacketGapMs, fpgaBootWaitMs;
    int psuCount, psuFirstAddr;
    QString passwordHash;      // rỗng = mật khẩu mặc định
    int lockMinutes;

    static QString iniPath() { return QCoreApplication::applicationDirPath() + "/actl_tool.ini"; }
    static void save(const QString &key, const QVariant &value)
    {
        QSettings(iniPath(), QSettings::IniFormat).setValue(key, value);
    }

    static Settings load()
    {
        QSettings ini(iniPath(), QSettings::IniFormat);
        auto get = [&](const QString &key, const QVariant &def) {
            if (!ini.contains(key)) ini.setValue(key, def);
            return ini.value(key);
        };
        const QString appDir = QCoreApplication::applicationDirPath();
        Settings s;
        s.mbCount = get("system/mbCount", 20).toInt();
        s.trbPerMb = get("system/trbPerMb", 8).toInt();
        s.staleMs = get("system/staleMs", 3000).toInt();
        s.monitorAddress = get("monitor/gatewayAddress", "192.168.1.10").toString();
        s.monitorPort = quint16(get("monitor/gatewayPort", 5000).toUInt());
        s.monitorReplayFile = get("monitor/replayFile", "").toString();
        s.replayIntervalMs = get("monitor/replayIntervalMs", 5).toInt();
        s.servicePort = get("service/port", "").toString();
        s.serviceBaud = get("service/baud", 1000000).toInt();
        s.trbCheckCrc = get("trb/checkCrc", true).toBool();
        s.psuCheckCrc = get("psu/checkCrc", true).toBool();
        s.cfgReadTimeoutMs = get("config/readTimeoutMs", 3000).toInt();
        s.cfgWriteGapMs = get("config/writeGapMs", 300).toInt();
        s.logEnabled = get("log/enabled", true).toBool();
        s.logDir = get("log/dir", appDir + "/logs").toString();
        s.logPeriodMs = get("log/periodMs", 10000).toInt(); // chu kỳ ghi mỗi TRB; đổi trạng thái thì ghi ngay
        s.logMaxFileMb = get("log/maxFileMb", 100).toInt();
        s.thresholdsFile = get("alarm/thresholdsFile", appDir + "/thresholds.json").toString();
        s.psuThresholdsFile = get("alarm/psuThresholdsFile", appDir + "/psu_thresholds.json").toString();
        s.psuDefaultConfigFile = get("psu/defaultConfigFile", appDir + "/psu_default_config.json").toString();
        s.fpgaEraseWaitSec = get("fpgaOta/eraseWaitSec", 420).toInt();
        s.fpgaPacketGapMs = get("fpgaOta/packetGapMs", 5).toInt();
        s.fpgaBootWaitMs = get("fpgaOta/bootWaitMs", 3000).toInt();
        s.psuCount = get("psu/count", 5).toInt();
        s.psuFirstAddr = get("psu/firstAddr", 1).toInt();
        s.passwordHash = get("auth/passwordHash", "").toString();
        s.lockMinutes = get("auth/lockMinutes", 10).toInt();
        return s;
    }
};
