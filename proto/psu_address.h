#pragma once
// Địa chỉ PSU trong bản tin là 0..4 (byte địa chỉ), còn giao diện, nhật ký và thông báo hiển thị 1..5 (giống MB/TRB: hiển thị = địa chỉ + 1).
namespace proto::psuaddr {
constexpr int displayNo(int addr) { return addr + 1; }
constexpr int addrOf(int displayNo) { return displayNo - 1; }
}
