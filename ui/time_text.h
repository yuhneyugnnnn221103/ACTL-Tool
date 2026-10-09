#pragma once
// "Đã cũ bao lâu" dạng giờ:phút:giây (không lấy phần lẻ giây), ví dụ 00:01:05.
#include <QString>
#include <QtGlobal>

namespace ui {

inline QString elapsedText(qint64 ms)
{
    const qint64 s = qMax<qint64>(0, ms) / 1000;
    return QStringLiteral("%1:%2:%3").arg(s / 3600, 2, 10, QLatin1Char('0'))
        .arg(s / 60 % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
}

} // namespace ui
