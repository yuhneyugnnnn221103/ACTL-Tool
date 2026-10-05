#pragma once
// Bảng màu, màu trạng thái và cách nạp style.qss. Mọi màu của giao diện khai báo ở đây:
// style.qss chỉ chứa token dạng @tên@, được thay bằng màu ở bảng này khi nạp.
#include "../model/device_store.h"
#include <QColor>
#include <QString>
#include <QWidget>

class QApplication;
class QWidget;

namespace ui::theme {

struct Palette {
    QColor bg{0xF3, 0xF4, 0xF6};          // nền cửa sổ
    QColor surface{0xFF, 0xFF, 0xFF};     // thẻ, ô nhập, bảng
    QColor surfaceAlt{0xF9, 0xFA, 0xFB};  // header bảng, vùng phụ
    QColor border{0xE2, 0xE5, 0xEA};      // viền mảnh 1px
    QColor grid{0xEE, 0xF0, 0xF3};        // lưới bảng, rất nhạt
    QColor text{0x1F, 0x29, 0x37};        // chữ chính
    QColor textMuted{0x6B, 0x72, 0x80};   // chữ phụ
    QColor accent{0x25, 0x63, 0xEB};      // màu nhấn
    QColor accentHover{0x1D, 0x4E, 0xD8};
    QColor accentPressed{0x1E, 0x40, 0xAF};
    QColor accentSoft{0xE8, 0xEF, 0xFE};  // nền mục đang chọn
    QColor danger{0xDC, 0x26, 0x26};      // nút nguy hiểm (nạp, boot, xóa flash)
    QColor dangerHover{0xB9, 0x1C, 0x1C};
    QColor dangerPressed{0x99, 0x1B, 0x1B};
    QColor disabledBg{0xD1, 0xD5, 0xDB};
    QColor disabledText{0x9C, 0xA3, 0xAF};
};

const Palette &palette();

// Màu trạng thái cố định, luôn đi kèm ký hiệu hoặc chữ.
QColor statusColor(model::Status s);
QString statusMark(model::Status s);
QColor statusTextColor(model::Status s); // chữ đọc được trên nền statusColor

QString styleSheet();                    // style.qss đã thay token
void apply(QApplication &app);           // Fusion + font + style.qss
inline void setRole(QWidget *w, const char *role) { w->setProperty("role", role); } // secondary | danger | title | muted | metric
void addShadow(QWidget *card);           // đổ bóng nhẹ, chỉ dùng cho vài thẻ lớn tĩnh

constexpr int kSpace = 8;                // khoảng cách layout, bội số của 4
constexpr int kMargin = 12;

} // namespace ui::theme
