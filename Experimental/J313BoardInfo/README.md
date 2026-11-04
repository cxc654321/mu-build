# J313 BoardInfo：host-only 资源契约

本目录为独立 MIT 纯 C 校验器。没有 MMIO 解引用、系统调用、回调、ADT/FDT parser、AppleDTLib、PIO 链接、UEFI INF/DSC 或 firmware glue；不会操作电源、时钟、pinmux、GPIO 或 IRQ。整数地址不转成指针。不部署、不 push。

## 分层与可信边界

1. **expected profile**：来自已阅读的[来源研究](../../Docs/J313-SPIHID-SOURCES.md)，基于其固定 Asahi Linux 414e55da7a0afaca2a97b29603be88ce0a6fd6de J313 DTS/T8103 DTSI 汇总，本轮不声称重新抓取上游。profile 类型没有 evidence；只提供期望值，不提供创建已验证请求的函数。
2. **caller 收集层（未实现）**：安全解析当前启动实例身份、资源与地址翻译，并独立确认实际状态。raw Apple ADT 不是 Linux FDT，不假定 compatible/phandle/clock-gates 可互换，不调用既有不安全 AppleDTLib parser。未来 parser 必须独立审查长度、范围、深度、整数溢出并模糊测试。
3. **本层**：caller 提供完整、有效且稳定的 bi_request；bi_validate 只验证结构、白名单、算术及证据状态。按值返回 bi_result，只有 BI_OK 可以读取 resource；失败 resource 各成员为零（不承诺 C padding 字节），missing_fact 仅在 BI_UNCONFIRMED 有效。返回首项错误，无部分成功资源。
4. **消费层（未实现）**：未来独立审查映射后端、超时/步数预算、所有权及 IRQ 策略。本层不生成 pio_config，不自动填写 PIO 的 resources_validated/bus_config_validated。C 结构不是不可伪造 capability；成功不是上机授权。

## 必须提交的证据

19个 evidence 槽必须严格为 BI_CONFIRMED 且 reference 非零；零初始化 UNKNOWN，EXPECTED、REJECTED、非法枚举、无引用均拒绝。reference 是 caller 审计记录标识，不是指针。本层不解析、不认证引用，也不能证明 caller 说了真话。测试中100+i仅为合成值，不能用于真实请求。

| 槽 | caller 的真实记录应证明 |
|---|---|
| IDENTITY | 当前启动实例的 J313/T8103 身份，不是编译时板名 |
| SPI/AP/NUB MAPPED | 整段物理到虚拟翻译、权限、实际 aperture 和映射生命周期 |
| SPI/AP/NUB DEVICE_MEMORY | 实际 Device memory 属性及访问排序，不能仅检查地址非零 |
| SPI/AP/NUB POWER_ACTUAL | 所有必要父域及本域实际已开启稳定，写 PMGR target 不等于 actual |
| CLOCK_ACTUAL | 当前时钟源、gate与 clock_hz 依据，不是 DTS 的120MHz |
| PINMUX_ACTUAL | AP46–49 function1 的实际状态与所有权，不猜四针信号次序 |
| CS/ENABLE/HID_IRQ/CONTROLLER_IRQ ROUTE | 控制器身份、路由、线号及极性，区分通知IRQ和SPI控制器IRQ |
| DIVISOR_ACTUAL | raw divider语义及SCK核实，不能忽略raw与raw+1资料分歧 |
| BUS_MODE_ACTUAL | mode0、8bit/MSB、CS时序，DTS默认推导不是实测 |
| EXCLUSIVE_STABLE_LEASE | 控制器/CS独占、无并发DMA/IRQ使用者，全部事实在使用期有效 |

本层无时间源，不能检测陈旧证据/TOCTOU。caller 必须将引用绑定同一启动实例、同组资源与有效lease；重映射、关电、改频或所有权改变立即丢弃结果并重新核实。不具备证据必须停止，不能批量填 CONFIRMED。本层也不证明 enable/reset 已执行、IRQ 已启用、HID 初始化成功或设备 ready。

## 数值契约

- 仅 J313/T8103，SPI3物理基址0x23510c000。SPI size 是实际已确认映射子区间，4字节对齐，0x16c..0x4000；0x16c覆盖当前[PIO接口](../J313SpiPio/j313_spi_pio.h)最高寄存器0x168加32位宽。不链接PIO头文件，PIO新增寄存器需人工同步审查。
- 三组 physical_base/mapped_base/size 均非零、4字节对齐；减法检查64位 base+size 的 exclusive end 不溢出；两两物理或映射区间重叠拒绝。mapped_base仅为64位地址描述，未来后端另验 uintptr_t 宽度、页面粒度及平台映射规则。
- AP GPIO base=0x23c100000，enable pin195 active-high；NUB base=0x23d1f0000，HID pin13 level-low。AP最少784字节、NUB最少56字节，仅覆盖 pin*4 的32位字。公开汇总未给完整GPIO aperture，因此不捏造最大值，真实范围由caller映射证据负责。这不是GPIO IRQ寄存器覆盖校验，也不授权GPIO/IRQ访问。
- CS0；SPI控制器 AIC IRQ617 level-high；HID通知NUB13，与AIC617不同。pinmux AP46/47/48/49 function1。
- clock_hz显式传入，本窄profile只接受120000000；0及旧200MHz拒绝，120MHz仍须独立CLOCK_ACTUAL证据。其他频率需审查新profile，不能静默兜底。
- sck_hz为请求上限，1..8000000；raw divider 1..0x7ff与PIO一致。caller显式选RAW或RAW_PLUS_ONE且提交证据，UNKNOWN拒绝。精确检查 clock_hz <= sck_hz * effective_divisor，避免截断频率误放行；乘积在已限界值内不溢出。不自动选择分频，不声称资料分歧已解决。
- 仅mode0、8bit、MSB-first、setup/hold/inactive=65/65/250微秒；这是白名单而非测量报告。段间200微秒属于transport事务，不在本层。

## 文件与复现

- [头文件](j313_boardinfo.h)：结构、19个证据槽、错误类型。
- [实现](j313_boardinfo.c)：无分配、无后端、固定次数检查。
- [测试](test_boardinfo.c)：独立合成请求，覆盖wrong board/SoC、bad MMIO、物理/虚拟overflow、未映射、Device属性未知、power unknown、clock0、divider边界/精确频率、GPIO控制器混淆、路由、总线、逐项证据缺失，以及profile完全匹配但未证实仍拒绝。

在本目录运行，不要定义NDEBUG：

```sh
clang -std=c11 -Wall -Wextra -Werror -pedantic -O1 j313_boardinfo.c test_boardinfo.c -o test_boardinfo_plain
./test_boardinfo_plain
clang -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all j313_boardinfo.c test_boardinfo.c -o test_boardinfo_sanitized
./test_boardinfo_sanitized
```

本轮普通严格警告构建及 ASan+UBSan 构建均运行 exit 0，各输出 `PASS 135 boardinfo host cases; no hardware/backend linked`，无 sanitizer diagnostics。编译产物已清理，仅保留四个源代码/文档文件。

host通过不证明真实映射、电源、时钟、波形、IRQ或固件可用。
