# Chan

> English: [README.md](README.md)

Chan là ngôn ngữ thông dịch siêu gọn, viết thuần C99, hướng tới các hệ thống
mà vài KB cũng là quá lớn. Chan được thiết kế như một **thư viện nhúng**
(kiểu V8), không phải ngôn ngữ hoàn chỉnh: core không có IO — mọi thứ như
`print` đều do host C đăng ký vào qua API.

## Tài liệu ngôn ngữ

- [docs/LANGUAGE_vi.md](docs/LANGUAGE_vi.md) — tài liệu tiếng Việt
- [docs/LANGUAGE_en.md](docs/LANGUAGE_en.md) — English version

Tài liệu đầy đủ về từ vựng, kiểu, biểu thức, câu lệnh, move semantics và
API nhúng C.

## Xây dựng

```bash
cmake -S . -B build
cmake --build build --config Release
```

Build tối ưu kích thước binary (kèm target `chan_core` để đo lõi không kèm
host demo):

```bash
cmake -S . -B build-size -DCHAN_SIZE_OPT=ON
cmake --build build-size --config Release
```

## Kích thước (Windows x64, MSVC)

| Build | `chan.exe` (kèm host demo) | `chan_core.exe` (lõi) |
|-------|---------------------------|----------------------|
| Release `/O2` (mặc định) | 48.5 KiB | — |
| Release `/O1` + `/MD` (`CHAN_SIZE_OPT=ON`) | 46.0 KiB | 41.0 KiB |

Con số trên là binary Windows PE (đã bao gồm phần đầu PE và CRT động). Trên
vi điều khiển với `gcc -Os` không có PE/CRT sẽ nhỏ hơn nữa. Lưu ý: hiện thực
tree-walking + parser dựng AST đầy đủ nằm ở mức ~41 KiB — để chạm mục tiêu
"vài KB" cần chuyển sang bytecode compiler + VM gọn hơn (xem phần Ghi chú
hiện thực).

## Chạy

```bash
./build/Release/chan            # chạy demo nhúng
./build/Release/chan file.chan  # chạy một file
./build/Release/chan --ast      # in AST của demo
CHAN=./build/Release/chan bash tests/run_tests.sh
```

## Kiểu dữ liệu

`nil`, `bool` (`true`/`false`), `int`, `float`, `str`, `array`, `map`, `obj`.
`int` và `float` là **hai kiểu riêng biệt** — lưu float khi chỉ cần int trên
hệ thống nhỏ là lãng phí. Số học: `int + int = int`; có float tham gia thì
kết quả là `float` (`10 / 4 = 2`, `10.0 / 4 = 2.5`).

## Cú pháp

```
fn fib(n: int): int {
    if n < 2 {
        return n
    } else {
        return fib(n - 1) + fib(n - 2)
    }
}

let x: int = fib(10)
let y: float = 3.14
let flag: bool = true
let s: str = "chan # tiny"
let arr: array = [1, 2.5, x, s]
let m: map = {"a": 1, "b": 2}
m["c"] = 3

while i < 10 {
    if i == 5 {
        cont
    }
    if i == 8 {
        break
    }
    i = i + 1
}
```

- Danh sách (tham số, đối số, phần tử mảng) ngăn cách bằng `,`.
- `#` là **comment** tới cuối dòng — vì không dùng `//` nên `/` (chia) không
  bao giờ phải nhìn trước 2 ký tự, đỡ tốn tài nguyên cho lexer.
- Không có `for`, không có `switch` — mọi thứ bằng `if` và `while`.
- Kiểu trả về của hàm bắt buộc có `: type` sau danh sách tham số
  (`fn f(a: int, b: int): int`), parser khỏi phải đoán.

Keyword (20): `if elif else while let bool array map str nil int float obj
fn cont break copy return true false`.

## Move semantics (không GC, không ARC)

Trong Chan mọi thứ **mặc định là move**, chi phí bằng 0. Không cần GC/ARC/
borrow-checker — mỗi giá trị có đúng một chủ sở hữu:

- `let b = a` **di chuyển** `a` vào `b`: binding `a` bị **xóa hẳn khỏi bảng
  ký hiệu** (không để lại `nil`) để tiết kiệm tài nguyên.
- Truyền đối số cũng move: `print(x)` xóa `x`. Muốn giữ lại thì dùng
  `print(copy x)`.
- `copy` tạo bản sao sâu: `let b = copy a` — cả `a` và `b` còn sống.
- Đọc một biến (vế phải của phép toán, điều kiện `if`/`while`, chỉ số) là
  **mượn** (borrow), không tốn chi phí và không xóa biến.
- `return x` move giá trị ra khỏi khung hàm.
- Mảng/map sở hữu phần tử của chúng: `[1, 2.5, x, s]` move `x`, `s` vào mảng.

Ví dụ (`examples/move.chan`):

```
let a: int = 10
let b: int = a     # a bị move vào b — a không còn tồn tại
print(b)           # 10
print(a)           # lỗi: undefined variable 'a'
```

## Nhúng C (thư viện, kiểu V8)

Core không in ấn, không đọc file. Host đăng ký hàm C và đọc kết quả:

```c
#include "interp.h"

static Value my_print(Chan* c, Value* args, int n, void* ud) {
    for (int i = 0; i < n; i++) {
        char* s = value_to_string(&args[i]);
        fputs(s, stdout);
        free(s);
        chan_drop(&args[i]);   // đối số được move vào — host phải dọn
    }
    return mk_nil();
}

int main(void) {
    Chan* c = chan_new();
    chan_register(c, "print", my_print, NULL);   // IO do host cung cấp

    Program* p = chan_parse(c, "let x: int = 40 + 2\nprint(x)\n");
    if (!p) { fprintf(stderr, "%s\n", chan_error_msg(c)); return 1; }
    if (chan_run(c, p, NULL) != 0) {
        fprintf(stderr, "%s\n", chan_error_msg(c));
        return 1;
    }
    Value x;
    if (chan_get(c, "x", &x) == 0) { /* đọc biến toàn cục */ free_value(&x); }
    chan_free(c);
    return 0;
}
```

- Hàm C nhận đối số **đã move vào** (owned). Dùng `chan_drop(&args[i])` để
  giải phóng, `chan_take(&args[i])` để chuyển quyền sở hữu sang kết quả trả
  về (xem `c_push` trong `src/main.c`).
- `obj` là kiểu liên kết C: `mk_obj(ptr, "ten", free_fn)` bọc con trỏ C để
  script dùng; bản sao `copy` là tham chiếu dùng chung, không sở hữu dữ liệu.
- **Không hỗ trợ đa luồng**: mỗi luồng chạy một `Chan*` riêng. Chan không có
  biến toàn cục, nên chạy nhiều instance trên nhiều luồng là an toàn.

## Ghi chú hiện thực

- Bộ nhớ kiểu arena đơn giản: AST do `chan_parse` cấp và được giải phóng cùng
  `free_program`; giá trị tự giải phóng khi scope ra khỏi phạm vi
  (`scope_free`), không có GC.
- So sánh `array`/`map` là so sánh sâu (đệ quy, có giới hạn độ sâu để
  giá trị tự tham chiếu không lặp vô hạn).
- Phần tử mảng/map là move vào; key của map được copy vào bảng băm
  (open addressing, tombstone).
