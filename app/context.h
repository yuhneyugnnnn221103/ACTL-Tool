#pragma once
// Mọi thứ giao diện cần, gom lại để thêm service mới không phải sửa chữ ký hàm ở nhiều nơi.
#include "../app/settings.h"
#include "../core/tcp_client_transport.h"
#include "../services/log_recorder.h"
#include "../model/psu_store.h"
#include "../services/fpga_ota.h"
#include "../services/psu_config.h"
#include "../services/psu_control.h"
#include "../services/stm_ota.h"
#include "../services/trb_calib.h"
#include "../services/trb_config.h"
#include "../services/trb_control.h"

namespace ui { class Auth; }

struct AppContext {
    Settings settings;
    model::DeviceStore *store = nullptr;
    model::Thresholds *thresholds = nullptr;
    model::PsuStore *psuStore = nullptr;
    model::Thresholds *psuThresholds = nullptr;
    core::Link *monitorLink = nullptr;       // TCP: PC là client, Gateway là server; mọi bản tin (giám sát, điều khiển, cấu hình, nạp code, Calib) đi đường này
    core::TcpClientTransport *tcp = nullptr; // nullptr khi đang phát lại file hex
    services::TrbControl *trbControl = nullptr;
    services::TrbConfig *trbConfig = nullptr;
    services::TrbCalib *trbCalib = nullptr;
    services::PsuControl *psuControl = nullptr;
    services::PsuConfig *psuConfig = nullptr;
    services::FpgaOta *fpgaOta = nullptr;
    services::StmOta *stmOta = nullptr;
    services::LogRecorder *logger = nullptr;   // nullptr khi tắt log
    ui::Auth *auth = nullptr;

    // Các chức năng hỏi-đáp (cấu hình, nạp code) dùng chung một hàng đợi, mỗi lúc chỉ một chức năng được chạy.
    bool serviceBusy() const { return trbConfig->busy() || fpgaOta->busy() || stmOta->busy() || psuConfig->busy(); }
};
