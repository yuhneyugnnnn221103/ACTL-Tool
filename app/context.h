#pragma once
// Mọi thứ giao diện cần, gom lại để thêm service mới không phải sửa chữ ký hàm ở nhiều nơi.
#include "../app/settings.h"
#include "../core/serial_transport.h"
#include "../core/tcp_server_transport.h"
#include "../services/csv_logger.h"
#include "../services/fpga_ota.h"
#include "../services/stm_ota.h"
#include "../services/trb_config.h"
#include "../services/trb_control.h"

namespace ui { class Auth; }

struct AppContext {
    Settings settings;
    model::DeviceStore *store = nullptr;
    model::Thresholds *thresholds = nullptr;
    core::Link *monitorLink = nullptr;       // TCP: giám sát + điều khiển
    core::Link *serviceLink = nullptr;       // RS485: cấu hình + nạp code
    core::TcpServerTransport *tcp = nullptr; // nullptr khi đang phát lại file hex
    core::SerialTransport *serial = nullptr;
    services::TrbControl *trbControl = nullptr;
    services::TrbConfig *trbConfig = nullptr;
    services::FpgaOta *fpgaOta = nullptr;
    services::StmOta *stmOta = nullptr;
    services::CsvLogger *logger = nullptr;   // nullptr khi tắt log
    ui::Auth *auth = nullptr;

    // Đường RS485 là hỏi-đáp, mỗi lúc chỉ một chức năng được dùng.
    bool serviceBusy() const { return trbConfig->busy() || fpgaOta->busy() || stmOta->busy(); }
};
