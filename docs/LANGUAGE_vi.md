# Tài liệu ngôn ngữ Chan

Chan là một ngôn ngữ thông dịch siêu gọn, viết thuần C99, dành cho các hệ
thống mà vài KB cũng là quá lớn. Chan được thiết kế như một **thư viện nhúng**
(kiểu V8): phần lõi (core) không có IO, mọi chức năng như in ấn, đọc file,
gọi C đều do host cung cấp thông qua API.

Các quyết định thiết kế lớn:

- `int` và `float` là hai kiểu riêng — lưu `float` khi chỉ cần `int` là lãng phí.
- **Move semantics**: mọi thứ mặc định là move, không cần GC, ARC hay
  borrow-checker. `copy` là từ khóa để nhân bản khi cần.
- Không có `for`, không có `switch` — mọi thứ làm bằng `if` và `while`.
- Không hỗ trợ đa luồng; ứng dụng nhúng chạy một instance `Chan` trên mỗi
  luồng (Chan không có biến toàn cục nên an toàn).

---

## 1. Từ vựng (Lexical)

### 1.1 Comment

`#` bắt đầu một comment kéo dài tới cuối dòng. Vì không dùng `//` làm comment
nên toán tử `/` (chia) không bao giờ phải nhìn trước 2 ký tự.

```
let x: int = 5   # đây là comment
```

### 1.2 Định danh

`[a-zA-Z_][a-zA-Z0-9_]*`. Keyword không được dùng làm tên biến.

### 1.3 Keyword (20 từ)

```
if elif else while let bool array map str nil int float obj
fn cont break copy return true false
```

- `cont` = continue (tiếp tục vòng lặp).
- `copy` = copy sâu (mặc định là move).
- `true` / `false` là literal của kiểu `bool`.
- `bool array map str nil int float obj` vừa là kiểu (ở khai báo), vừa có
  `nil`/`true`/`false` dùng được ở vị trí giá trị.

### 1.4 Literal

| Loại | Ví dụ | Ghi chú |
|------|-------|---------|
| int | `0`, `42`, `-7` | số nguyên 64-bit |
| float | `3.14`, `0.5`, `10.0` | số thực (bắt buộc có chữ số sau `.`) |
| str | `"chan"`, `"a\nb"` | escape: `\n \t \r \\ \"` |
| bool | `true`, `false` | |
| nil | `nil` | |

### 1.5 Toán tử và dấu phân cách

```
=  ==  !=  <  >  <=  >=  &&  ||  !
+  -  *  /  %
(  )  {  }  [  ]  ,  :  #
```

- `,` — ngăn cách phần tử trong danh sách (tham số, đối số, mảng, map).
- `#` — comment (xem 1.1).
- `:` — chú thích kiểu: `let x: int = 1`, `fn f(a: int): int`.
- EOL (`\n`) kết thúc câu lệnh. Không dùng `;`.

---

## 2. Kiểu dữ liệu

| Kiểu | Mô tả |
|------|-------|
| `nil` | không có giá trị |
| `bool` | `true` / `false` |
| `int` | số nguyên 64-bit |
| `float` | số thực double |
| `str` | chuỗi byte |
| `array` | mảng động, phần tử là move vào |
| `map` | bảng băm, key được copy vào khi insert |
| `obj` | giá trị liên kết C do host bọc lại (`mk_obj`) |

Quy tắc số học: `int op int = int`; nếu có một `float` tham gia thì kết quả
là `float`.

```
10 / 4    # 2    (int)
10.0 / 4  # 2.5  (float)
7 % 3     # 1    (chỉ áp dụng cho int)
```

So sánh số học so sánh được giữa `int` và `float` (`1 == 1.0` → `true`).
So sánh `str` là so sánh từ điển. So sánh `array`/`map` là **so sánh sâu**:
hai array/map bằng nhau khi cùng nội dung, so từng phần tử (giới hạn độ sâu
đệ quy để chống cấu trúc tự tham chiếu).

---

## 3. Biểu thức

Thứ tự ưu tiên (thấp → cao):

| Mức | Toán tử |
|-----|---------|
| 1 | `=` (gán) |
| 2 | `\|\|` |
| 3 | `&&` |
| 4 | `== !=` |
| 5 | `< > <= >=` |
| 6 | `+ -` |
| 7 | `* / %` |
| 8 | prefix `- ! copy` |
| 9 | gọi hàm `f(...)`, chỉ số `a[i]` |

Các dạng biểu thức:

```
42            # literal int
3.14          # literal float
"chan"        # literal str
true, nil     # literal bool / nil
x             # biến (mượn khi đọc, move khi chuyển)
-f            # phủ định
!flag         # phủ định logic
copy x        # copy sâu
a + b * 2     # toán tử
f(1, 2)       # gọi hàm
arr[0]        # phần tử mảng
m["k"]        # giá trị map
[1, 2.5, x]   # array literal (phần tử move vào)
{"a": 1, "b": 2}  # map literal
(a + b) * c   # nhóm bằng ngoặc
```

Gán là biểu thức: `x = expr`, `arr[i] = expr`, `m[k] = expr`. Vế trái phải là
biến hoặc chỉ số.

---

## 4. Câu lệnh

### 4.1 `let` — khai báo biến (kiểu bắt buộc)

```
let x: int = 5
let s: str = "hello"
let arr: array = [1, 2, 3]
```

Vế phải được **move** vào biến mới.

### 4.2 `fn` — định nghĩa hàm

```
fn add(a: int, b: int): int {
    return a + b
}
```

- Tên, tham số (có kiểu bắt buộc) và kiểu trả về (bắt buộc, sau `:`) — parser
  không phải đoán.
- Hàm không trả về giá trị thì khai báo `: nil`.
- Hàm có thể gọi đệ quy. `fn` là câu lệnh — hàm là giá trị nội bộ
  (`<fn:tên>`), gọi qua tên.

### 4.3 `return`

```
fn fib(n: int): int {
    if n < 2 {
        return n
    }
    return fib(n - 1) + fib(n - 2)
}
```

`return` dừng hàm và **move** giá trị ra khỏi khung hàm. Kiểu giá trị trả về
được kiểm tra với kiểu khai báo.

### 4.4 `if` / `elif` / `else`

```
if n < 2 {
    return n
} elif n < 10 {
    return fib(n - 1)
} else {
    return 0
}
```

`elif` được phân tích thành `else { if ... }` lồng nhau. `{` phải cùng dòng
với điều kiện. Điều kiện đọc biến bằng **mượn** (không move).

### 4.5 `while` / `cont` / `break`

```
let i: int = 0
while i < 10 {
    i = i + 1
    if i == 5 {
        cont     # bỏ qua phần còn lại, quay lại kiểm tra điều kiện
    }
    if i == 8 {
        break    # thoát vòng lặp
    }
}
```

`cont` / `break` ở ngoài vòng lặp là lỗi runtime.

### 4.6 Biểu thức đứng một mình

```
print(x)          # gọi hàm, bỏ kết quả
x + 1             # tính rồi bỏ — không có tác dụng, chỉ để test
```

---

## 5. Move semantics — quy tắc quan trọng nhất

Không có GC, ARC hay borrow-checker. Mỗi giá trị có **đúng một chủ sở hữu**
tại một thời điểm. Mọi thứ mặc định là **move** (chi phí 0), `copy` là cách
duy nhất để nhân bản.

**Các điểm move:**

| Tình huống | Ví dụ | Kết quả |
|-----------|-------|---------|
| Khai báo | `let b = a` | binding `a` bị **xóa khỏi bảng ký hiệu** |
| Gán | `x = y` | `y` bị xóa, giá trị chuyển sang `x` |
| Đối số hàm | `f(x)` | `x` bị xóa sau lời gọi |
| `return` | `return x` | `x` bị xóa khỏi khung hàm |
| Phần tử mảng/map | `[1, x]` | `x` bị xóa, mảng sở hữu giá trị |

**Đọc là mượn (borrow):** vế phải của phép toán, điều kiện `if`/`while`, chỉ
số, toán hạng — đều chỉ mượn, không xóa biến.

```
let a: int = 10
let b: int = a        # a bị xóa
print(b)              # 10
print(a)              # LỖI: undefined variable 'a'
```

**`copy`:** tạo bản sao sâu, cả hai biến còn sống.

```
let a: int = 10
let b: int = copy a   # cả a và b đều tồn tại
print(a)              # 10
print(b)              # 10
```

Quy ước thực dụng: muốn dùng lại một biến sau khi truyền vào hàm, hãy truyền
`copy x`:

```
print(copy x)   # x vẫn còn
print(x)        # x bị move, từ đây x không tồn tại
```

---

## 6. Mảng và map

### 6.1 Mảng

```
let arr: array = [1, 2.5, "x"]
print(arr[1])         # 2.5
arr[0] = 99           # gán phần tử
print(len(copy arr))  # 3  (len do host cung cấp)
```

- Phần tử được move vào mảng: `[1, 2.5, x]` xóa `x`.
- Chỉ số ngoài phạm vi là lỗi runtime.
- Trong ngữ cảnh move (đối số, `let`), `arr[i]` **detach** phần tử ra khỏi
  mảng; ngược lại chỉ mượn.

### 6.2 Map

```
let m: map = {"a": 1, "b": 2}
m["c"] = 3
print(m["b"])         # 2
```

- Key được deep-copy vào bảng băm; value được move vào.
- `print(m["b"])` trong ngữ cảnh move sẽ **detach** key `"b"` khỏi map.

---

## 7. Lỗi runtime

Các lỗi được báo dạng `line N: ...`:

```
undefined variable 'x'
'x': type mismatch, expected int, got str
index 5 out of range (len 2)
division by zero
break outside of a loop
cont outside of a loop
cannot call int
cannot index str
cannot apply '+' to int and str
```

`chan_run` trả về `-1` khi có lỗi; chi tiết lỗi nằm trong `chan_error_msg(c)`.

---

## 8. Nhúng C (API thư viện)

API chính (`interp.h`, `value.h`):

| Hàm | Mô tả |
|-----|-------|
| `chan_new()` / `chan_free(c)` | tạo / hủy một instance |
| `chan_parse(c, src)` | phân tích chương trình → `Program*` (NULL nếu lỗi) |
| `chan_run(c, p, out)` | chạy; `out` nhận bản sao giá trị cuối (0 = ok, -1 = lỗi) |
| `chan_register(c, name, fn, ud)` | đăng ký hàm C cho script |
| `chan_get(c, name, out)` | đọc biến toàn cục (bản sao owned) |
| `chan_error_msg(c)` | thông báo lỗi |
| `mk_obj(ptr, "tên", free_fn)` | bọc giá trị C thành `obj` |
| `chan_take(&slot)` / `chan_drop(&slot)` | chuyển quyền sở hữu / dọn đối số trong hàm C |

Ví dụ host đăng ký `print`:

```c
static Value my_print(Chan* c, Value* args, int n, void* ud) {
    for (int i = 0; i < n; i++) {
        char* s = value_to_string(&args[i]);
        fputs(s, stdout);
        free(s);
        chan_drop(&args[i]);   // đối số move vào — phải dọn
    }
    return mk_nil();
}
```

Quy tắc hàm C:

- Đối số đến **đã move vào** (owned) — dùng `chan_drop` hoặc `chan_take`.
- Hàm trả về một `Value` owned (`mk_*`).
- Không gọi hàm script đệ quy từ trong hàm C (không có reentrancy).

---

## 9. Giới hạn hiện tại

- Key của map được deep-copy vào bảng băm — có chủ ý: map phải sở hữu key
  để key tồn tại lâu hơn biến script đã tạo ra nó.
- Không có comment khối — hạn chế có chủ ý để giữ lexer gọn nhẹ và tiết kiệm tài nguyên trên thiết bị hạn chế.
- Không có chuỗi đa dòng — ngôn ngữ dùng xuống dòng để phân biệt các câu lệnh, nên chuỗi phải nằm trên một dòng; việc duyệt chuỗi đa dòng tốn tài nguyên.
- Một `Chan*` chỉ dùng một luồng — nhưng chạy nhiều instance song song là an toàn. Vì Chan là ngôn ngữ nhúng trên thiết bị IoT nên không hỗ trợ đa luồng; giống như isolate của V8, người dùng có thể khởi tạo các instance Chan độc lập trên nhiều luồng nếu muốn.
