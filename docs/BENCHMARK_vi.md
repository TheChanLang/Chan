# So sánh Chan với các ngôn ngữ nhúng khác

Bảng so sánh kích thước binary và tốc độ của Chan so với 6 ngôn ngữ nhúng phổ
biến: **Lua, Squirrel, Wren, AngelScript, ChaiScript, MicroPython**.

## Phương pháp

- **Máy:** Windows x64, CPU Duong (benchmark chạy nội bộ).
- **Trình biên dịch:** MSVC (cl) 14.51, `Release x64`, tối ưu hóa cao `-O2`
  (`/O2`), CRT động (`/MD`), toàn bộ đều build bằng cùng một bộ công cụ.
- **Benchmark:** `fib(30)` đệ quy (kết quả đúng = `832040`), đo 3 lần, lấy
  trung vị.
- **Nguồn:** bản mới nhất (2026) clone từ GitHub; Chan là `build/Release/chan.exe`.

## Kích thước binary (Release, x64)

| Ngôn ngữ | Binary | Kích thước | Ghi chú |
|----------|--------|-----------:|---------|
| **Chan** | `chan.exe` | **51,5 KiB** | gồm cả host demo |
| **Chan** (core) | `chan_core.exe` | **45,0 KiB** | chỉ core, không host |
| Wren | `wren.exe` | 123,0 KiB | host tối giản tự viết |
| Lua | `lua.exe` | 258,0 KiB | Lua 5.5.1 |
| Squirrel | `sq.exe` | 264,0 KiB | Squirrel 3.2 |
| MicroPython | `micropython.exe` | 562,0 KiB | port windows (thử nghiệm) |
| AngelScript | `as.exe` | 1 222,0 KiB | host tối giản tự viết |
| ChaiScript | `chaiscript.exe` | 1 441,0 KiB | header-only C++20 |

Chan nhỏ hơn **2,4×** Wren, **5×** Lua/Squirrel, và **24–28×** so với
AngelScript/ChaiScript.

## Tốc độ — `fib(30)` (giây, trung vị của 3 lần)

| Ngôn ngữ | Thời gian | Nhanh hơn Chan |
|----------|----------:|---------------:|
| Lua | 0,113 s | 11,5× |
| AngelScript | 0,151 s | 8,6× |
| Wren | 0,169 s | 7,7× |
| Squirrel | 0,204 s | 6,4× |
| MicroPython | 0,241 s | 5,4× |
| **Chan** | **1,303 s** | 1× |

## Nhận xét

- **Chan thắng áp đảo về kích thước** — đúng mục tiêu thiết kế: nhúng trên
  thiết bị IoT/vi điều khiển, nơi vài KB cũng là quá lớn.
- **Về tốc độ, Chan chậm hơn 5–11×** so với các ngôn ngữ còn lại. Nguyên nhân
  chính: Chan là **tree-walking interpreter** (diễn giải trực tiếp trên cây
  AST) — các ngôn ngữ kia đều biên dịch sang **bytecode** và chạy trên VM
  (Lua VM, Wren VM, Squirrel VM, MicroPython bytecode). Đây là đánh đổi đã
  ghi trong README: đạt mục tiêu "vài KB" đòi hỏi chuyển sang bytecode
  compiler + VM.
- **ChaiScript bị loại khỏi bảng tốc độ:** build thành công (1 441 KiB) nhưng
  chạy `fib(30)` mất **hơn 2 phút** — quá chậm để benchmark ở cùng workload;
  riêng `fib(20)` mất ~0,49 s.
- **MicroPython** (port windows) được ghi chú là "experimental" trong chính
  dự án; con số mang tính tham khảo.

## Tái lập

Toàn bộ mã nguồn benchmark đã tải về nằm ở `bench/` (đã thêm vào `.gitignore`),
script fib ở `bench/benchmarks/`. Lệnh build tham khảo:

```bash
# Lua (MSVC, -O2, /MD)
cl -O2 -MD -DLUA_USE_WINDOWS lua.c lapi.c ... linit.c -Fe:lua.exe
# Squirrel
cl -O2 -MD -EHsc -I include sq.c sqstdlib.cpp squirrel/*.cpp -Fe:sq.exe
# Wren + host
cl -O2 -MD -I wren/src/include wren_host/main.c wren/src/vm/*.c wren/src/optional/*.c
# AngelScript + host (cần ml64 cho as_callfunc_x64_msvc_asm.asm)
# MicroPython: msbuild mpy-cross.vcxproj && msbuild ports/windows/micropython.vcxproj
```
