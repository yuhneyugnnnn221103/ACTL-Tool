#!/usr/bin/env python3
"""Giả lập Gateway/TRB/PSU để thử ACTL Tool mà không cần phần cứng.

Các lệnh (python3 tools/mock_gateway.py <lệnh> -h để xem tùy chọn):

  list         Liệt kê các kịch bản.
  tcp          Giả Gateway: là TCP server (mặc định 0.0.0.0:5000), chờ app (TCP client) nối vào rồi bơm bản tin giám sát
               TRB (1111) và PSU (81) theo kịch bản. In ra lệnh điều khiển/beam mà app gửi xuống.
  hex          Ghi file hex để app phát lại: đặt monitor/replayFile=<file> trong actl_tool.ini.
  thresholds   Ghi thresholds.json và psu_thresholds.json mẫu để kịch bản "warning" có giá trị vượt ngưỡng.
  serial       Tạo cổng giả RS485 (pty, chỉ Linux/macOS): trả lời lệnh đọc cấu hình TRB (A3A3) và PSU (03),
               nhớ cấu hình đã ghi (A1A1, 04). Mở đường dẫn in ra trong ô cổng COM của app.
  selftest     Tự kiểm tra bộ dựng bản tin và cổng giả, không cần app.

Ví dụ:
  python3 tools/mock_gateway.py thresholds --dir build            # ngưỡng mẫu, chạy trước khi mở app
  python3 tools/mock_gateway.py tcp --scenario all --rate 2       # chạy trước, trong app nhập IP máy này (127.0.0.1) cổng 5000 rồi Kết nối
  python3 tools/mock_gateway.py serial --drop 0.2 --corrupt 0.1   # thử cấu hình khi RS485 mất gói / ghi sai

Mọi CMD phía PSU là 2 byte lặp (01 01, 03 03, 04 04, 81 81, 82 82, 90 90 .. 95 95), CRC phủ từ byte CMD.
Bố cục bản tin lấy theo proto/trb_monitor_proto.cpp, proto/psu_monitor_proto.cpp và các proto cấu hình;
nếu sửa bản tin bên C++ thì sửa hằng số tương ứng ở đây (selftest sẽ báo nếu độ dài không khớp).
"""
import argparse
import json
import os
import random
import select
import socket
import struct
import sys
import threading
import time

# ----------------------------------------------------------------------------- khung chung

H1, H2, T1, T2 = 0xAB, 0xCD, 0xE1, 0xE2


def crc16(data: bytes) -> int:
    """CRC16-CCITT-FALSE (poly 0x1021, init 0xFFFF), giống core/frame.cpp."""
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def seal(frame: bytearray, crc_start: int) -> bytes:
    """Điền header, CRC (big-endian tại len-4) và tailer."""
    n = len(frame)
    frame[0], frame[1] = H1, H2
    crc = crc16(bytes(frame[crc_start:n - 4]))
    frame[n - 4], frame[n - 3] = crc >> 8, crc & 0xFF
    frame[n - 2], frame[n - 1] = T1, T2
    return bytes(frame)


def put(frame: bytearray, off: int, value: int, size: int) -> None:
    value = max(0, min(int(value), (1 << (8 * size)) - 1))
    frame[off:off + size] = value.to_bytes(size, "big")


# ----------------------------------------------------------------------------- bản tin TRB

TRB_LEN, TRB_CRC_START = 310, 4
TRB_TRM_OFF, TRB_TRM_SIZE = 6, 50          # 4 khối TRM x 50 byte
TRB_V, TRB_I, TRB_TEMP = 206, 208, 210
TRB_TRIP0, TRB_STATE0 = 212, 228
TRB_ADAR, TRB_PA, TRB_PG, TRB_MCU, TRB_HUM = 232, 233, 234, 235, 237
# Sau độ ẩm: INIT DATA 1 byte (239), rồi 24 byte bản tin lỗi (240..263), PERIOD TXEN 3 byte (264), PULSE TXEN 2 byte (267),
# PERIOD BEAMSYNC 3 byte (269), PULSE BEAMSYNC 2 byte (272), 32 byte dự phòng, CRC, E1 E2.
TRB_INIT_DATA, TRB_ERR0, TRB_ERR_COUNT, TRB_PERIOD_TXEN, TRB_PULSE_TXEN, TRB_PERIOD_BEAM, TRB_PULSE_BEAM = 239, 240, 24, 264, 267, 269, 272
TRB_TRIP_COUNT = 16

# Trong khối TRM: I_SEN1..8 (+0), DET1..8 (+16), TEMP1..4 (+32), I_PA1..4 (+40), I_LNA (+48); mỗi trường 2 byte.


def trb_frame(mb, trb, rng, *, isen=None, v=150, i=500, power_temp=40, mcu=50, humidity=40,
              trips=None, state=None, adar=0xFF, pa=0x0F, pg=0x0F, trm_extra=None):
    """isen: {(trm 0..3, kênh 0..7): giá trị} ghi đè I_SEN; trips: list 16 byte; trm_extra: {(trm, offset): giá trị}."""
    f = bytearray(TRB_LEN)
    f[2] = f[3] = 0x11
    f[4], f[5] = mb, trb
    for trm in range(4):
        base = TRB_TRM_OFF + trm * TRB_TRM_SIZE
        for ch in range(8):
            put(f, base + 2 * ch, rng.randint(90, 110), 2)             # I_SEN
            put(f, base + 16 + 2 * ch, rng.randint(900, 1100), 2)      # DET
        for k in range(4):
            put(f, base + 32 + 2 * k, rng.randint(35, 45), 2)          # TEMP
            put(f, base + 40 + 2 * k, rng.randint(45, 55), 2)          # I_PA
        put(f, base + 48, rng.randint(15, 25), 2)                      # I_LNA
    for (trm, ch), val in (isen or {}).items():
        put(f, TRB_TRM_OFF + trm * TRB_TRM_SIZE + 2 * ch, val, 2)
    for (trm, off), val in (trm_extra or {}).items():
        put(f, TRB_TRM_OFF + trm * TRB_TRM_SIZE + off, val, 2)
    put(f, TRB_V, v + rng.randint(-5, 5), 2)
    put(f, TRB_I, i + rng.randint(-20, 20), 2)
    put(f, TRB_TEMP, power_temp, 2)
    for k, t in enumerate(trips or []):
        f[TRB_TRIP0 + k] = t & 0xFF
    for k, s in enumerate(state or []):
        f[TRB_STATE0 + k] = s & 0xFF
    f[TRB_ADAR], f[TRB_PA], f[TRB_PG] = adar, pa, pg
    put(f, TRB_MCU, mcu, 2)
    put(f, TRB_HUM, humidity, 2)
    put(f, TRB_PERIOD_TXEN, 1000, 3)
    put(f, TRB_PULSE_TXEN, 100, 2)
    put(f, TRB_PERIOD_BEAM, 2000, 3)
    put(f, TRB_PULSE_BEAM, 200, 2)
    return seal(f, TRB_CRC_START)


# ----------------------------------------------------------------------------- bản tin PSU

PSU_LEN, PSU_CRC_START, PSU_ADDR = 266, 2, 4      # CMD 2 byte (81 81), địa chỉ ở byte 4
PSU_CLUSTER_OFF, PSU_CLUSTER_SIZE = 5, 48
PSU_SUPPLY_OFF, PSU_RTC_OFF, PSU_TRIP_OFF, PSU_TRIP_COUNT = 197, 221, 228, 10

# Thứ tự trường trong một cụm: (tên, số byte), trùng psumon::ClusterField.
PSU_CLUSTER_FIELDS = [
    ("I_OUT_ADC1", 3), ("TEMP_ADC1", 3), ("I_OUT_ADC2", 3), ("TEMP_ADC2", 3), ("I_OUT_ADC3", 3), ("TEMP_ADC3", 3),
    ("I_IN_AMC", 3), ("V_IN_AMC", 3),
    ("V_IN_XDP", 2), ("V_IN_XDP_PEAK", 2), ("V_IN_XDP_VALLEY", 2), ("V_OUT_XDP", 2), ("V_OUT_XDP_PEAK", 2),
    ("V_OUT_XDP_VALLEY", 2), ("I_OUT_XDP", 2), ("I_OUT_XDP_PEAK", 2), ("I_OUT_XDP_VALLEY", 2), ("I_OUT_XDP_RMS", 2),
    ("TEMP_FET", 2), ("TEMP_XDP", 2),
]
PSU_SUPPLY_FIELDS = ["I_DCM", "V_DCM", "I_5V", "V_5V", "I_3V3_1", "V_3V3_1", "I_3V3_2", "V_3V3_2"]
# Giá trị thô "bình thường" (nằm trong ngưỡng mẫu do lệnh thresholds ghi).
PSU_NORMAL = {"TEMP_ADC1": 1000, "TEMP_ADC2": 1000, "TEMP_ADC3": 1000, "I_OUT_XDP": 2000, "V_IN_XDP": 3000,
              "V_OUT_XDP": 3000, "TEMP_FET": 1200, "TEMP_XDP": 1200}
PSU_SUPPLY_NORMAL = {"V_5V": 5000, "V_3V3_1": 3300, "V_3V3_2": 3300}


def psu_frame(addr, rng, *, over=None, supply_over=None, trips=None, now=None):
    """over: {(cụm 0..3, tên trường): giá trị}; supply_over: {tên: giá trị}; trips: list 10 byte."""
    f = bytearray(PSU_LEN)
    f[2] = f[3] = 0x81
    f[PSU_ADDR] = addr
    for c in range(4):
        off = PSU_CLUSTER_OFF + c * PSU_CLUSTER_SIZE
        for name, size in PSU_CLUSTER_FIELDS:
            val = PSU_NORMAL.get(name, rng.randint(100, 900)) + rng.randint(-20, 20)
            if (c, name) in (over or {}):
                val = over[(c, name)]
            put(f, off, val, size)
            off += size
    off = PSU_SUPPLY_OFF
    for name in PSU_SUPPLY_FIELDS:
        val = PSU_SUPPLY_NORMAL.get(name, rng.randint(100, 900))
        if name in (supply_over or {}):
            val = supply_over[name]
        put(f, off, val, 3)
        off += 3
    t = time.localtime(now or time.time())
    f[PSU_RTC_OFF:PSU_RTC_OFF + 7] = bytes([t.tm_year % 100, t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, 0])
    for k, tr in enumerate(trips or []):
        f[PSU_TRIP_OFF + k] = tr & 0xFF
    return seal(f, PSU_CRC_START)


# ----------------------------------------------------------------------------- kịch bản


class Ctx:
    def __init__(self, mb_count, trb_per_mb, psu_first, psu_count, seed):
        self.mb_count, self.trb_per_mb = mb_count, trb_per_mb
        self.psu_first, self.psu_count = psu_first, psu_count
        self.rng = random.Random(seed)
        self.t = 0.0                      # giây kể từ lúc bắt đầu
        self.tick = 0

    def trbs(self):
        return [(m, t) for m in range(self.mb_count) for t in range(self.trb_per_mb)]

    def psus(self):
        return list(range(self.psu_first, self.psu_first + self.psu_count))


def healthy(ctx, skip_trb=(), skip_psu=(), trb_over=None, psu_over=None):
    """Bản tin khỏe mạnh cho mọi thiết bị, trừ các thiết bị bị bỏ qua; *_over: {khóa thiết bị: dict tham số ghi đè}."""
    out = []
    for mb, trb in ctx.trbs():
        if (mb, trb) not in skip_trb:
            out.append(trb_frame(mb, trb, ctx.rng, **(trb_over or {}).get((mb, trb), {})))
    for a in ctx.psus():
        if a not in skip_psu:
            out.append(psu_frame(a, ctx.rng, **(psu_over or {}).get(a, {})))
    return out


def sc_normal(ctx):
    return healthy(ctx)


def sc_warning(ctx):
    """Cần thresholds mẫu. TRB 0/0 I_SEN1 TRM1 vượt max, TRB 1/1 áp dưới min, TRB 2/2 nhiệt MCU quá cao;
    PSU 2 nhiệt ADC1 cụm 2 vượt max, PSU 4 dòng XDP cụm 4 dưới min, PSU 5 áp 5V quá cao."""
    last = ctx.psu_first + ctx.psu_count - 1
    return healthy(ctx,
                   trb_over={(0, 0): dict(isen={(0, 0): 250}), (1, 1): dict(v=50), (2, 2): dict(mcu=120)},
                   psu_over={ctx.psu_first + 1: dict(over={(1, "TEMP_ADC1"): 2500}),
                             ctx.psu_first + 3: dict(over={(3, "I_OUT_XDP"): 50}),
                             last: dict(supply_over={"V_5V": 7000})})


def sc_trip(ctx):
    """Trip code: 0/1 Trip1 bit0,2 (TRM1 I_SEN1, I_SEN3 vượt max); 3/2 Trip5 bit4,7 (TRM2 PA1, PA4 vượt max);
    5/5 Trip7 system (temp max, V min, I max); 7/0 Trip10 bit1 (TRM1 I_SEN2 dưới min); 6/3 Trip14 (PA dưới min);
    PSU 3 có trip, PSU 1 sạch."""
    def trips(**kv):
        t = [0] * TRB_TRIP_COUNT
        for k, v in kv.items():
            t[int(k[1:]) - 1] = v
        return dict(trips=t)
    mb = ctx.mb_count
    ov = {(0, 1): trips(t1=0b101), (min(3, mb - 1), 2): trips(t5=0x90), (min(5, mb - 1), 5): trips(t7=0b010101),
          (min(7, mb - 1), 0): trips(t10=0b10), (min(6, mb - 1), 3): trips(t14=0x81)}
    p = ctx.psu_first + 2
    return healthy(ctx, trb_over=ov, psu_over={p: dict(trips=[0b101, 0, 0, 0, 0x80, 0, 0, 0, 0, 1])})


def sc_leds(ctx):
    """Đèn ADAR/PG/PA: 0/0 ADAR4 lỗi; 0/1 PG TRM3 lỗi; 0/2 PA chỉ TRM1, TRM3 bật; 0/3 tất cả bằng 0."""
    return healthy(ctx, trb_over={(0, 0): dict(adar=0xF7), (0, 1): dict(pg=0b1011), (0, 2): dict(pa=0b0101),
                                  (0, 3): dict(adar=0, pa=0, pg=0)})


def sc_lost(ctx):
    """Mất kết nối rồi hồi phục theo chu kỳ 24 s: MB0 và hai PSU đầu im lặng từ giây 6 đến giây 18 của chu kỳ."""
    phase = ctx.t % 24
    if 6 <= phase < 18:
        return healthy(ctx, skip_trb={(0, t) for t in range(ctx.trb_per_mb)}, skip_psu=set(ctx.psus()[:2]))
    return healthy(ctx)


def sc_flap(ctx):
    """Trạng thái nhảy liên tục: mỗi tick khoảng 10% thiết bị có trip, 10% trip bit ngẫu nhiên."""
    ov = {}
    for dev in ctx.trbs():
        if ctx.rng.random() < 0.10:
            t = [0] * TRB_TRIP_COUNT
            t[ctx.rng.randrange(TRB_TRIP_COUNT)] = 1 << ctx.rng.randrange(8)
            ov[dev] = dict(trips=t)
    pov = {a: dict(trips=[1] + [0] * 9) for a in ctx.psus() if ctx.rng.random() < 0.3}
    return healthy(ctx, trb_over=ov, psu_over=pov)


def sc_noise(ctx):
    """Bản tin tốt xen lẫn lỗi đường truyền: byte rác, sai CRC, sai tailer, khung cụt, CMD lạ, địa chỉ ngoài dải.
    Trả về cả (bytes, nghỉ giây) để gửi khung chia mảnh."""
    r = ctx.rng
    good = healthy(ctx)
    r.shuffle(good)
    out = good[:40]
    frame = trb_frame(1, 1, r)
    bad_crc = bytearray(frame); bad_crc[100] ^= 0x55
    bad_tail = bytearray(frame); bad_tail[-1] = 0x00
    out += [bytes(r.randrange(256) for _ in range(r.randint(1, 30))),             # byte rác
            bytes(bad_crc), bytes(bad_tail),
            bytes([H1, H2, 0x77, 0x77]) + bytes(20),                              # CMD lạ
            bytes([H1, H2]),                                                      # header cụt
            trb_frame(ctx.mb_count + 5, 0, r),                                    # MB ngoài dải
            trb_frame(0, ctx.trb_per_mb + 2, r),                                  # TRB ngoài dải
            psu_frame(ctx.psu_first + ctx.psu_count + 3, r)]                      # PSU ngoài dải
    whole = trb_frame(2, 2, r)                                                    # khung tốt bị chia 3 mảnh
    out += [(whole[:100], 0.05), (whole[100:200], 0.05), whole[200:]]
    cut = psu_frame(ctx.psu_first, r)                                             # khung cụt rồi khung tốt đè lên
    out += [cut[:120], psu_frame(ctx.psu_first, r)]
    return out


SCENARIOS = {
    "normal": (sc_normal, "mọi thiết bị khỏe, không trip"),
    "warning": (sc_warning, "giá trị vượt ngưỡng min/max (chạy lệnh thresholds trước)"),
    "trip": (sc_trip, "các trip code: I_SEN, PA, system, dưới min; PSU có trip"),
    "leds": (sc_leds, "đèn ADAR/PG/PA lỗi hoặc tắt"),
    "lost": (sc_lost, "một số thiết bị im lặng rồi hồi phục (thử trạng thái Mất kết nối)"),
    "flap": (sc_flap, "trạng thái đổi liên tục"),
    "noise": (sc_noise, "rác, sai CRC/tailer, khung cụt/chia mảnh, địa chỉ ngoài dải"),
    "stress": (sc_normal, "như normal, dùng với --rate cao (ví dụ 20) để thử tải"),
}
ALL_ORDER = ["normal", "warning", "trip", "leds", "noise", "lost", "flap"]
ALL_PHASE_SECONDS = 12


def scenario_frames(name, ctx):
    if name == "all":
        name = ALL_ORDER[int(ctx.t // ALL_PHASE_SECONDS) % len(ALL_ORDER)]
    return name, SCENARIOS[name][0](ctx)


# ----------------------------------------------------------------------------- TCP (giả Gateway)


def decode_control(buf):
    """Tách các lệnh app gửi xuống Gateway; trả về (danh sách mô tả, phần dư)."""
    out = []
    while len(buf) >= 4:
        if buf[0] != H1 or buf[1] != H2:
            buf = buf[1:]
            continue
        cmd2 = bytes(buf[2:4])
        if cmd2 == b"\xA2\xA2" and len(buf) >= 14:
            f = buf[:14]
            ctl = f[7]
            out.append(f"điều khiển TRB MB{f[4]}/TRB{f[5]}: PA={f[6]:04b} start={ctl >> 1 & 1} clear={ctl & 1} "
                       f"beamsync={ctl >> 2 & 1} debug={ctl >> 4 & 1}" + (" (broadcast)" if f[4] == 0xFF else ""))
            buf = buf[14:]
        elif cmd2 == b"\x14\x14" and len(buf) >= 17:
            f = buf[:17]
            out.append(f"beam MB{f[4]}/TRB{f[5]}: phaseTX={f[6]} phaseRX={f[7]} ampTX={f[8]} ampRX={f[9]} CH={f[10]:04b} ADAR={f[11]:08b}")
            buf = buf[17:]
        elif cmd2 == b"\x01\x01" and len(buf) >= 13:
            f = buf[:13]
            out.append(f"điều khiển PSU {f[4]}: bật cụm={f[5]:04b} clearTrip={f[6]}")
            buf = buf[13:]
        elif cmd2 in (b"\xA2\xA2", b"\x14\x14", b"\x01\x01"):
            break                                      # chưa đủ byte
        else:
            buf = buf[1:]
    return out, buf


def cmd_tcp(args):
    """Giả Gateway: là TCP server, chờ app (TCP client) nối vào rồi bơm bản tin giám sát."""
    ctx = Ctx(args.mb, args.trb, args.psu_first, args.psu_count, args.seed)
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((args.host, args.port))
    srv.listen(1)
    print(f"giả Gateway đang chờ app nối vào {args.host}:{args.port} (trong app nhập IP máy này và cổng {args.port}, bấm Kết nối). Ctrl+C để dừng.")
    try:
        while True:
            sock, peer = srv.accept()
            print(f"app {peer[0]}:{peer[1]} đã nối vào")
            if tcp_session(args, ctx, sock) is False:
                break
            print("chờ app nối lại...")
    except KeyboardInterrupt:
        pass
    finally:
        srv.close()


def burst_frames(frames):
    """Gom các khung giám sát của 8 TRB cùng một MB thành một lần gửi, như Gateway thật."""
    out, buf, cur = [], bytearray(), None
    for item in frames:
        data, pause = item if isinstance(item, tuple) else (item, 0)
        mb = data[4] if len(data) > 4 and data[2:4] == b"\x11\x11" and not pause else None
        if mb is not None and (cur is None or mb == cur) and len(buf) // TRB_LEN < 8:
            buf += data
            cur = mb
            continue
        if buf:
            out.append((bytes(buf), 0))
            buf, cur = bytearray(), None
        if mb is not None:
            buf += data
            cur = mb
        else:
            out.append((data, pause))
    if buf:
        out.append((bytes(buf), 0))
    return out


def tcp_session(args, ctx, sock):
    sock.settimeout(0.01)
    print(f"kịch bản '{args.scenario}', {args.rate} lượt/giây, mỗi lần gửi dồn 8 bản tin TRB của một MB.")

    stop = threading.Event()
    rx = bytearray()

    def reader():
        nonlocal rx
        while not stop.is_set():
            try:
                data = sock.recv(4096)
                if not data:
                    print("app đã đóng kết nối")
                    stop.set()
                    return
                rx += data
                lines, rest = decode_control(bytes(rx))
                rx = bytearray(rest)
                for l in lines:
                    print("  <- app:", l)
            except socket.timeout:
                continue
            except OSError:
                stop.set()
                return

    threading.Thread(target=reader, daemon=True).start()
    start, last_name = time.time(), None
    try:
        while not stop.is_set() and (args.duration <= 0 or time.time() - start < args.duration):
            ctx.t, ctx.tick = time.time() - start, ctx.tick + 1
            name, frames = scenario_frames(args.scenario, ctx)
            if name != last_name:
                print(f"[{ctx.t:6.1f}s] kịch bản: {name} - {SCENARIOS[name][1]}")
                last_name = name
            for item in burst_frames(frames):
                data, pause = item if isinstance(item, tuple) else (item, 0)
                sock.setblocking(True)
                sock.sendall(data)
                sock.settimeout(0.01)
                if pause:
                    time.sleep(pause)
            time.sleep(max(0.0, 1.0 / args.rate))
    except KeyboardInterrupt:
        return False
    except (BrokenPipeError, ConnectionResetError):
        print("app đã ngắt kết nối")
    finally:
        stop.set()
        sock.close()
    return True


# ----------------------------------------------------------------------------- file hex / ngưỡng


def cmd_hex(args):
    ctx = Ctx(args.mb, args.trb, args.psu_first, args.psu_count, args.seed)
    lines = 0
    with open(args.out, "w") as fh:
        for tick in range(args.ticks):
            ctx.t, ctx.tick = tick * args.tick_seconds, tick
            _, frames = scenario_frames(args.scenario, ctx)
            for item in frames:
                data = item[0] if isinstance(item, tuple) else item
                fh.write(" ".join(f"{b:02X}" for b in data) + "\n")
                lines += 1
    print(f"đã ghi {lines} dòng vào {args.out}. Đặt [monitor] replayFile={os.path.abspath(args.out)} trong actl_tool.ini.")


def cmd_thresholds(args):
    os.makedirs(args.dir, exist_ok=True)
    trb = {"TRB.V": [100, 200], "TRB.TEMP POWER": [10, 80], "TEMP MCU": [10, 90]}
    for trm in range(1, 5):
        for ch in range(1, 9):
            trb[f"TRM{trm}.I SEN{ch}"] = [20, 200]
    psu = {}
    for c in range(1, 5):
        for k in (1, 2, 3):
            psu[f"C{c}.TEMP_ADC{k}"] = [100, 2000]
        psu[f"C{c}.I_OUT_XDP"] = [100, 5000]
    psu["SUPPLY.V_5V"] = [4500, 5500]
    for name, doc in (("thresholds.json", {"default": trb}), ("psu_thresholds.json", {"default": psu})):
        path = os.path.join(args.dir, name)
        with open(path, "w") as fh:
            json.dump(doc, fh, indent=2)
        print("đã ghi", path)
    print("Đặt hai file này cạnh file chạy của app (hoặc đổi alarm/thresholdsFile, alarm/psuThresholdsFile trong actl_tool.ini).")


# ----------------------------------------------------------------------------- RS485 giả (pty)

TRB_CFG_LEN, TRB_CFG_READ_LEN = 520, 10
PSU_CFG_LEN, PSU_CFG_READ_LEN = 915, 13


class SerialResponder:
    """Trả lời cấu hình qua một luồng byte; nhớ cấu hình đã ghi theo thiết bị."""

    def __init__(self, drop=0.0, corrupt=0.0, delay=0.0, rng=None, log=print):
        self.drop, self.corrupt, self.delay = drop, corrupt, delay
        self.rng, self.log = rng or random.Random(), log
        self.trb, self.psu = {}, {}
        self.buf = bytearray()

    @staticmethod
    def _crc_ok(frame, start):
        n = len(frame)
        return frame[n - 2] == T1 and frame[n - 1] == T2 and crc16(bytes(frame[start:n - 4])) == (frame[n - 4] << 8 | frame[n - 3])

    def feed(self, data):
        """Nhận byte, trả về danh sách bytes cần gửi lại."""
        self.buf += data
        replies = []
        while len(self.buf) >= 4:
            b = self.buf
            if b[0] != H1 or b[1] != H2:
                del b[0]
                continue
            cmd2 = bytes(b[2:4])
            if cmd2 == b"\xA1\xA1":
                n, kind = TRB_CFG_LEN, "ghi TRB"
            elif cmd2 == b"\xA3\xA3":
                n, kind = TRB_CFG_READ_LEN, "đọc TRB"
            elif cmd2 == b"\x04\x04":
                n, kind = PSU_CFG_LEN, "ghi PSU"
            elif cmd2 == b"\x03\x03":
                n, kind = PSU_CFG_READ_LEN, "đọc PSU"
            else:
                del b[0]
                continue
            if len(b) < n:
                break
            frame = bytes(b[:n])
            start = 4 if kind.endswith("TRB") else 2
            if not self._crc_ok(frame, start):
                self.log(f"  khung {kind} sai CRC/tailer, bỏ qua 1 byte")
                del b[0]
                continue
            del b[:n]
            replies += self._handle(kind, frame)
        return replies

    def _handle(self, kind, f):
        is_trb = kind.endswith("TRB")
        key = (f[4], f[5]) if is_trb else f[PSU_ADDR]
        name = f"MB{key[0]}/TRB{key[1]}" if is_trb else f"PSU {key}"
        store = self.trb if is_trb else self.psu
        if kind.startswith("ghi"):
            body = bytearray(f)
            if self.rng.random() < self.corrupt:
                pos = self.rng.randrange(8, len(f) - 8)
                body[pos] ^= 0xFF
                self.log(f"  {kind} {name}: lưu SAI một byte (offset {pos}) để thử đọc lại không khớp")
            store[key] = bytes(body)
            self.log(f"  {kind} {name}: đã lưu {len(f)} byte")
            return []
        if self.rng.random() < self.drop:
            self.log(f"  {kind} {name}: bỏ không trả lời")
            return []
        if is_trb:
            reply = bytearray(store.get(key, bytes(TRB_CFG_LEN)))
            reply[2] = reply[3] = 0xA4
            reply[4], reply[5] = key
            crc_start, = (4,)
        else:
            reply = bytearray(store.get(key, bytes(PSU_CFG_LEN)))
            reply[2] = reply[3] = 0x82
            reply[PSU_ADDR] = key
            reply[5] = reply[6] = 0                      # Config Mask: không dùng khi trả lời
            crc_start = 2
        self.log(f"  {kind} {name}: trả lời {len(reply)} byte")
        if self.delay:
            time.sleep(self.delay)
        return [seal(reply, crc_start)]


def cmd_serial(args):
    import pty
    import tty
    master, slave = pty.openpty()
    tty.setraw(slave)
    tty.setraw(master)
    print(f"Cổng RS485 giả: {os.ttyname(slave)}\nMở đường dẫn này trong ô cổng COM của app (baud tùy ý). Ctrl+C để dừng.")
    resp = SerialResponder(args.drop, args.corrupt, args.delay / 1000.0, random.Random(args.seed))
    try:
        while True:
            if select.select([master], [], [], 0.5)[0]:
                data = os.read(master, 4096)
                for r in resp.feed(data):
                    os.write(master, r)
    except KeyboardInterrupt:
        pass


# ----------------------------------------------------------------------------- tự kiểm tra


def check(cond, msg):
    print(("  ok   " if cond else "  LỖI  ") + msg)
    if not cond:
        check.failed += 1


check.failed = 0


def cmd_selftest(_args):
    rng = random.Random(1)
    print("bản tin giám sát:")
    t = trb_frame(3, 5, rng, trips=[1] * 16)
    check(len(t) == TRB_LEN and t[:4] == bytes([H1, H2, 0x11, 0x11]), "TRB 310 byte, CMD 1111")
    check(crc16(t[4:TRB_LEN - 4]) == (t[TRB_LEN - 4] << 8 | t[TRB_LEN - 3]) and t[TRB_LEN - 2:] == bytes([T1, T2]), "TRB CRC từ byte 4, tailer E1 E2")
    check(t[4] == 3 and t[5] == 5 and t[TRB_TRIP0:TRB_TRIP0 + 16] == bytes([1] * 16), "địa chỉ và trip code đúng vị trí")
    p = psu_frame(2, rng, trips=[0xAA] * 10)
    check(len(p) == PSU_LEN and p[2:4] == b"\x81\x81" and p[PSU_ADDR] == 2, "PSU 266 byte, CMD 81 81, địa chỉ ở byte 4")
    check(crc16(p[2:262]) == (p[262] << 8 | p[263]), "PSU CRC từ byte 2 (CMD)")
    check(p[PSU_TRIP_OFF:PSU_TRIP_OFF + 10] == bytes([0xAA] * 10), "PSU trip code đúng vị trí")
    check(PSU_CLUSTER_OFF + 4 * PSU_CLUSTER_SIZE == PSU_SUPPLY_OFF and sum(s for _, s in PSU_CLUSTER_FIELDS) == PSU_CLUSTER_SIZE,
          "cụm PSU 48 byte, Supply ngay sau 4 cụm")

    print("kịch bản:")
    ctx = Ctx(20, 8, 1, 5, 1)
    for name in list(SCENARIOS) + ["all"]:
        ctx.t = 0
        _, frames = scenario_frames(name, ctx)
        check(len(frames) > 0, f"'{name}' sinh {len(frames)} bản tin")
    ctx.t = 10
    check(len(sc_lost(ctx)) == len(sc_normal(ctx)) - 8 - 2, "'lost' bỏ 8 TRB của MB0 và 2 PSU trong giai đoạn mất kết nối")

    print("giải mã lệnh app gửi xuống:")
    ctl = bytearray(14); ctl[2] = ctl[3] = 0xA2; ctl[4], ctl[5], ctl[6], ctl[7] = 7, 3, 5, 0x03
    lines, rest = decode_control(seal(ctl, 4))
    check(len(lines) == 1 and "MB7/TRB3" in lines[0] and rest == b"", "điều khiển TRB")
    pc = bytearray(13); pc[2] = pc[3] = 0x01; pc[4], pc[5], pc[6] = 4, 0b1010, 1
    lines, _ = decode_control(seal(pc, 2))
    check(len(lines) == 1 and "PSU 4" in lines[0] and "1010" in lines[0], "điều khiển PSU")

    print("RS485 giả (cấu hình):")
    r = SerialResponder(rng=random.Random(2), log=lambda *_: None)
    w = bytearray(TRB_CFG_LEN); w[2] = w[3] = 0xA1; w[4], w[5] = 6, 2; w[10:14] = b"\x01\x02\x03\x04"
    check(r.feed(seal(w, 4)) == [], "ghi TRB không có trả lời")
    rd = bytearray(TRB_CFG_READ_LEN); rd[2] = rd[3] = 0xA3; rd[4], rd[5] = 6, 2
    out = r.feed(seal(rd, 4))
    check(len(out) == 1 and len(out[0]) == TRB_CFG_LEN and out[0][2:4] == b"\xA4\xA4" and out[0][10:14] == b"\x01\x02\x03\x04"
          and crc16(out[0][4:516]) == (out[0][516] << 8 | out[0][517]), "đọc TRB trả đúng cấu hình đã ghi (A4A4)")
    w = bytearray(PSU_CFG_LEN); w[2] = w[3] = 0x04; w[PSU_ADDR] = 3; w[5] = w[6] = 0xFF; w[500:504] = b"\xDE\xAD\xBE\xEF"
    r.feed(seal(w, 2))
    rd = bytearray(PSU_CFG_READ_LEN); rd[2] = rd[3] = 0x03; rd[PSU_ADDR] = 3
    out = r.feed(seal(rd, 2))
    check(len(out) == 1 and len(out[0]) == PSU_CFG_LEN and out[0][2:4] == b"\x82\x82" and out[0][500:504] == b"\xDE\xAD\xBE\xEF"
          and crc16(out[0][2:911]) == (out[0][911] << 8 | out[0][912]), "đọc PSU trả đúng cấu hình đã ghi (82 82)")
    noisy = SerialResponder(drop=1.0, rng=random.Random(3), log=lambda *_: None)
    check(noisy.feed(seal(bytearray(rd), 2)) == [], "--drop 1.0: không trả lời")
    chunks = seal(bytearray(rd), 2)
    split = [r.feed(chunks[:5]), r.feed(chunks[5:])]
    check(split[0] == [] and len(split[1]) == 1, "nhận khung bị chia mảnh")
    junk = r.feed(b"\x00\xFF\xAB" + seal(bytearray(rd), 2))
    check(len(junk) == 1, "bỏ qua byte rác phía trước")

    print("không có lỗi" if not check.failed else f"{check.failed} lỗi")
    return 1 if check.failed else 0


# ----------------------------------------------------------------------------- dòng lệnh


def cmd_list(_args):
    for name, (_, doc) in SCENARIOS.items():
        print(f"  {name:8s} {doc}")
    print(f"  {'all':8s} lần lượt {', '.join(ALL_ORDER)}, mỗi giai đoạn {ALL_PHASE_SECONDS} s")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    def common(p):
        p.add_argument("--scenario", default="normal", choices=list(SCENARIOS) + ["all"])
        p.add_argument("--mb", type=int, default=20, help="số mother board (system/mbCount)")
        p.add_argument("--trb", type=int, default=8, help="số TRB mỗi MB (system/trbPerMb)")
        p.add_argument("--psu-first", type=int, default=1, help="địa chỉ PSU đầu (psu/firstAddr)")
        p.add_argument("--psu-count", type=int, default=5, help="số PSU (psu/count)")
        p.add_argument("--seed", type=int, default=1)

    p = sub.add_parser("list"); p.set_defaults(fn=cmd_list)
    p = sub.add_parser("tcp"); common(p); p.set_defaults(fn=cmd_tcp)
    p.add_argument("--host", default="0.0.0.0", help="địa chỉ lắng nghe"); p.add_argument("--port", type=int, default=5000)
    p.add_argument("--rate", type=float, default=2.0, help="số lượt gửi đủ mọi thiết bị mỗi giây")
    p.add_argument("--duration", type=float, default=0, help="giây; 0 = chạy mãi")
    p = sub.add_parser("hex"); common(p); p.set_defaults(fn=cmd_hex)
    p.add_argument("out"); p.add_argument("--ticks", type=int, default=10, help="số lượt")
    p.add_argument("--tick-seconds", type=float, default=1.0, help="thời gian mô phỏng mỗi lượt (cho 'lost', 'all')")
    p = sub.add_parser("thresholds"); p.set_defaults(fn=cmd_thresholds)
    p.add_argument("--dir", default=".", help="thư mục ghi (cạnh file chạy của app)")
    p = sub.add_parser("serial"); p.set_defaults(fn=cmd_serial)
    p.add_argument("--drop", type=float, default=0.0, help="xác suất bỏ không trả lời lệnh đọc")
    p.add_argument("--corrupt", type=float, default=0.0, help="xác suất lưu sai một byte khi ghi (thử đọc lại không khớp)")
    p.add_argument("--delay", type=float, default=0.0, help="ms trễ trước khi trả lời")
    p.add_argument("--seed", type=int, default=1)
    p = sub.add_parser("selftest"); p.set_defaults(fn=cmd_selftest)

    args = ap.parse_args()
    sys.exit(args.fn(args) or 0)


if __name__ == "__main__":
    main()
