# Git và quy trình làm việc nhóm (mục 12 của báo cáo)

Phần này ghi lại quy trình Git áp dụng cho chính dự án này, đúng với mô tả ở
mục 12 của báo cáo: nhánh tính năng → commit có ý nghĩa → Merge Request → CI.

## 1. Khởi tạo và quy ước nhánh

```bash
git init
git add .
git commit -m "khoi tao du an minh hoa bao cao thuc tap"
git remote add origin git@gitlab.company.vn:intern/ap6-embedded-linux.git
```

| Loại nhánh | Đặt tên | Ví dụ |
|---|---|---|
| Tính năng | `feature/<mô-tả>` | `feature/telemetry-shm` |
| Sửa lỗi | `fix/<mô-tả>` | `fix/zombie-after-apply` |
| Thử nghiệm | `exp/<mô-tả>` | `exp/epoll-edge-trigger` |

Không commit trực tiếp lên `main` — mọi thay đổi đi qua Merge Request để có
review, đúng như báo cáo nêu về việc giảm rủi ro làm hỏng codebase chung.

## 2. Vòng làm việc hằng ngày

```bash
git checkout -b feature/ap6sim-poll
# ... sửa mã ...
make && make test              # bắt buộc xanh trước khi commit
git add kernel/ap6sim/ap6sim.c
git commit -m "ap6sim: them .poll de user space dung epoll doc event"
git push -u origin feature/ap6sim-poll
```

Quy ước message (theo kiểu kernel):
`<thành phần>: <việc đã làm ở thể chủ động>`, dòng đầu ≤ 72 ký tự, thân
commit giải thích **tại sao** chứ không lặp lại **cái gì**.

```
apd: xu ly SIGCHLD bang vong lap waitpid(WNOHANG)

Moi lan APPLY, daemon fork mot process con chay script cau hinh. Kernel
chi gui MOT SIGCHLD du nhieu con cung ket thuc, nen handler cu chi
waitpid mot lan de sot zombie sau vai gio chay. Chuyen sang vong lap
WNOHANG de thu hoi het.
```

## 3. Những gì đưa vào Git trong dự án nhúng

Ngoài mã nguồn ứng dụng, báo cáo (mục 12) nêu rõ còn có: script build, cấu hình
board, file Device Tree, tài liệu kỹ thuật và patch cho kernel/bootloader.
Trong dự án này tương ứng là:

```
src/, demos/, kernel/           mã nguồn
Makefile, scripts/              hệ thống build
config/ap6.dat                  cấu hình mặc định
openwrt/package/ap6ctl/         package recipe, init script, UCI
tests/                          kiểm thử
docs/                           tài liệu
```

**Không** đưa vào Git: `build/`, `*.o`, `*.ko`, `demos/02-basic-c/out/`,
`docs/ket-qua-chay.txt` (sinh ra được) — xem `.gitignore`.

## 4. Merge Request và CI

Nội dung MR nên có: mục tiêu, cách kiểm chứng, ảnh chụp/log kết quả, và phần
đã test trên board nào. CI tối thiểu cho dự án này:

```yaml
# .gitlab-ci.yml
stages: [build, test]

build:
  stage: build
  script:
    - make
    - make CROSS_COMPILE=arm-linux-gnueabihf-   # kiểm tra cross-compile
  artifacts:
    paths: [build/]

test:
  stage: test
  script:
    - make test
```

## 5. Lệnh hay dùng khi xử lý sự cố

| Tình huống | Lệnh |
|---|---|
| Xem nhánh đã đi trước/sau remote bao nhiêu | `git status -sb` |
| Cất tạm việc đang dở để sửa lỗi gấp | `git stash` → `git stash pop` |
| Lấy đúng một commit từ nhánh khác | `git cherry-pick <sha>` |
| Hoàn tác một commit đã push | `git revert <sha>` |
| Xem ai sửa dòng này, vì sao | `git log -p -L 120,140:src/apd/server.c` |
| Tìm commit gây lỗi | `git bisect start` → `good`/`bad` |
| Đồng bộ nhánh với main trước khi MR | `git fetch && git rebase origin/main` |

Cảnh báo đã nêu trong báo cáo: `git reset --hard` và `git push --force` làm
thay đổi lịch sử. Chỉ dùng trên nhánh cá nhân chưa chia sẻ; với nhánh chung
hãy dùng `--force-with-lease` để không ghi đè commit của người khác.
