# J313 SPI PIO：独立 host-only transaction engine

本目录仅为纯 C host 实验，不连接 firmware，没有真实 MMIO 后端、物理地址、PMGR/GPIO/AIC 访问或硬件测试。通过 host 模型不代表 J313 板级可用。本轮审查修复仅修改本目录测试和 README；引擎及头文件未改，未 push、未部署。

## 来源及许可证

代码采用 **GPL-2.0-only**。寄存器和初始化/传输顺序参考固定版本 [AsahiLinux spi-apple.c](https://raw.githubusercontent.com/AsahiLinux/linux/549e8b442b485c03e3e766635e2203c75362e312/drivers/spi/spi-apple.c)，提交 549e8b442b485c03e3e766635e2203c75362e312。本次通过 web_fetch 完整核对该固定 URL（HTTP 200），不是跟随移动分支。上游 SPDX 为 GPL-2.0，保留归属：Copyright The Asahi Linux Contributors；based on spi-sifive.c, Copyright 2018 SiFive, Inc. 上游 MODULE_AUTHOR 为 Hector Martin。新实现不包含 Linux API，独立增加跨全事务的截止时间、步数限额和严格失败条件；没有复制上游每次 wait 重置超时、剩余长度仅打印错误却仍可能成功的问题。

## 文件与契约

- [j313_spi_pio.h](j313_spi_pio.h)：回调、显式配置、分段请求、返回状态和计数。
- [j313_spi_pio.c](j313_spi_pio.c)：仅依赖标准 C 的 CPU polling 引擎。
- [test_pio.c](test_pio.c)：16 项 TX/RX FIFO、W1C latch + level flag、CS 边沿和时钟故障模型。

调用方必须独占控制器与 CS；保证时钟、电源、映射、pinmux、外设模式已验证且整个调用期间稳定；禁止并发 DMA/IRQ/其他控制器使用者。exclusive_owner/resources_validated/bus_config_validated 是调用方承诺，不是引擎完成了硬件验证。回调及配置/数组/缓冲区必须有效稳定，TX/RX 缓冲区不重叠。没有共享 power 的保存/恢复，也没有抢占未知控制器状态；cleanup 是 quiesce，不是 restore。

CPOL、CPHA 和 raw clock divider 由 caller 显式提供已验证值；divider 接受 1..0x7ff，不自行推导频率、不盲选 120/200MHz。Linux ceil(clk/speed) 与其他资料 divider+1 的差异仍需独立板级核验。本实现仅 8-bit、MSB-first，CFG MODE_IRQ=1<<5，但 IE_FIFO/IE_XFER 始终为 0，CPU 轮询。SHIFTCFG 的 bit24 override 必须清零，BITS[21:16]=8；PINCFG 清 CS_IDLE_VAL bit9、设 KEEP_CS bit1，保留无关位。CS 严格按 Linux PIN bit1：清零 assert，置位 deassert。

每段可 TX-only、RX-only 或同长度全双工，长度非零且不大于 UINT32_MAX。CS 跨所有段及 delay_after_us 保持有效。测试示例为 TX256、延迟200us、RX4，setup65/hold65/inactive250 均来自 config，不是引擎常数；另测 3/7/11us 与段间13us。为了不依赖上次调用时间，首段 assert 前和最终 deassert 后均等待 inactive_us。段间不重新施加 setup/hold。成功包含整个收尾 inactive 等待。

## 有界性、计数和失败

timeout_us 是从第一次 now_us 取样开始的单个事务 deadline，覆盖初始化后检查、首次 inactive、setup、全部段、迟到尾字、段间 delay、hold 和最终 inactive。不会因阈值 flag 粘滞、done 提前或少量慢流进度重置截止时间。使用差值而非 start+timeout，时间倒退（包括 uint64 wrap）直接失败。step_budget 限制检查点总数，时钟停住仍退出；错误不是成功。输入数组的预验证在 started 之前，依赖调用方提供有限、有效的数组，不受事务时限计数。

仅 FIFO occupancy 允许 RXDATA 读取；先验证 level<=16、不会超过剩余缓冲区；全部接收长度和对应 done 位都满足才成功。RX done 后缺尾字继续等待原有 deadline；最终短收为 TIMEOUT，返回已实际读取数量。TX done 提前且尚有未排队或 FIFO 未退役数据为 PREMATURE_TX。fifo_error_flags 保留 RXUNDERRUN bit16 / TXOVERFLOW bit17；超范围 occupancy / 多收独立报错。RXFULL 是容量/level 指示，不冒称上游不存在的 RX overflow error 位。

tx_queued 是写入 TXDATA 的数量；tx_completed 是根据 FIFO occupancy 已退役的数量，不等于独立观测到线路上已发送的字节。成功还要求 TX done 和 FIFO 空。错误时 TX 计数为最后采样可确认值，不保证与 cleanup 时硬件进度完全同步。rx_completed 是实际 RXDATA 读取数量；segments_completed 只计完成的数据段，即使后续 delay/hold 超时也不回退，最终 status 才决定事务成功。

每个 started 出口无条件写 RUN=0、PIN CS inactive、两个 IRQ enable=0。无效配置不访问 IO。失败立即释放 CS，不保证错误路径 setup/hold/inactive 最短时序；后续新调用重新建立 inactive 等待。绝不恢复或关闭共享电源。

软件无法中断卡死的 read32/write32/now_us/delay_us 回调、MMIO 总线访问或 cleanup；全事务时间保证以回调有限返回、时钟语义正确为前提。step budget 也不能解决阻塞回调。回调必须提供设备访问排序/可见性，未来真实后端需另行验证 barrier、总线错误、寄存器副作用和 IRQ 屏蔽。本模型仅模拟在 delay tick 发生硬件进度，并非周期精确模型；不证明异步 MMIO 采样、实际波形、板级资源配置或 SPI-HID 协议正确。没有 HID packet/CRC、device enable/reset、重试协议或固件集成。

## 复现 host 测试

在本目录运行（编译产物可自行放临时目录）：

```sh
clang -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g \
  -fsanitize=address,undefined -fno-sanitize-recover=all \
  j313_spi_pio.c test_pio.c -o /tmp/j313_spi_pio_test
/tmp/j313_spi_pio_test
```

本轮普通 clang 和 clang + ASan + UBSan 严格警告编译/运行均 exit 0，无 sanitizer diagnostics。测试输出：

    PASS 120 success cases: 10 lengths x TX/RX/full-duplex x 4 modes
    PASS TX256 + 200us + RX4, continuous CS, configurable timing and global delay bounds
    PASS 12 fault/tail scenarios + 2 matched NORMAL controls, error flags, bounded waits, guards and cleanup
    PASS 9 invalid configurations: zero IO
    ALL HOST TESTS PASSED

成功长度为 1/4/15/16/17/31/32/255/256/513；三种方向与四种 CPOL/CPHA 组合。12 场景为 sticky threshold 永无进度、premature TX done、RX done 后尾字迟到成功、永久短收、TX overflow、RX underrun、多收、非法 FIFO level、时钟倒退、RUN 中时钟停住 step budget 退出、慢流全局超时、长度已齐但没有 done 位。额外覆盖跨段 delay 超时、数据段均完成但收尾超时、guard 字节、FIFO 容量、零空读、CS 边沿、保留未知位和所有 started 出口清理。无效配置9项均零 IO。

## 审查修复与变异负控证据

旧 failures 默认500us，但正常32-byte全双工含首尾需要250+65+32+65+250=662us。只断言 TIMEOUT 会将缺少 done 检查后的收尾超时误认为正确失败。

- NO_DONE 改为2000us、step_budget=10000；同参数 NORMAL 对照断言成功、完成一段、TX queued/completed 与 RX completed 均为32、done齐全、数据及guard正确，耗时662us。故障断言 TIMEOUT、segments_completed=0、TX queued/completed 与 RX completed 均为32、FIFO空、两个done位均缺失及数据完整。
- SLOW 改为800us、step_budget=10000，同参数 NORMAL 在662us成功。慢流每20us进展一字节，数据段完成至少需250+65+640=955us；断言段未完成、TX/RX有进展但不足32、done缺失，排除仅收尾超时的假阳性。
- 两个故障均断言恰在原事务deadline退出、未耗尽step budget、无FIFO error，保留cleanup及guard检查。时间均为合成模型时间，不是实测硬件时间。

| 引擎 | 构建 | 实跑结果 |
| --- | --- | --- |
| 原引擎 | 普通严格警告 | exit 0，ALL HOST TESTS PASSED |
| 原引擎 | ASan + UBSan | exit 0，无sanitizer diagnostics |
| 删除done条件的临时副本 | 普通严格警告 | 编译成功；运行exit 134，NO_DONE断言失败 |
| 删除done条件的临时副本 | ASan + UBSan | 编译成功；运行exit 134，同一NO_DONE断言失败 |

两次负控均报告 `fault 11 got 0 expected 2`，随后在 `failures` 的 `assert(r.status==expect[i])` 失败（本轮test_pio.c第204行）；0=PIO_OK，2=PIO_TIMEOUT。不是编译错误或sanitizer报告导致失败。

下列命令在本目录复现，所有产物均放仓库外临时目录。变异仅删除 `(flags & needed) == needed &&`，并加 `(void)needed` 避免unused-variable编译失败；仓库引擎不变。不使用 `-DNDEBUG`。

```sh
tmp=$(mktemp -d /tmp/j313-pio-review.XXXXXX)
export PIO_REVIEW_TMP="$tmp"
python3 - <<'PY'
import os
from pathlib import Path
source = Path("j313_spi_pio.c").read_text()
condition = "(flags & needed) == needed &&"
assert source.count(condition) == 1
mutant = source.replace(condition, "").replace(
    " bool running = false;",
    " (void)needed; /* Mutation-only unused-variable suppression. */\n bool running = false;"
)
Path(os.environ["PIO_REVIEW_TMP"], "mutant.c").write_text(mutant)
PY
for mode in plain sanitized; do
  sanitizer=""
  if [ "$mode" = sanitized ]; then
    sanitizer="-fsanitize=address,undefined -fno-sanitize-recover=all"
  fi
  clang -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g $sanitizer \
    j313_spi_pio.c test_pio.c -o "$tmp/$mode" || exit 1
  "$tmp/$mode" || exit 1
  clang -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g $sanitizer -I . \
    "$tmp/mutant.c" test_pio.c -o "$tmp/mutant-$mode" || exit 1
  (ulimit -c 0; "$tmp/mutant-$mode")
  rc=$?
  printf "mutant-%s exit=%s (expected 134)\n" "$mode" "$rc"
  test "$rc" -eq 134 || exit 1
done
```
