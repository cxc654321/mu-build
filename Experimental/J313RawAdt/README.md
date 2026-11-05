# J313 raw ADT — host-only experiment

**未验证 UEFI 实际 blob。不是固件组件，不是驱动，不能据此认定 SPI/HID/GPIO 已可用。**

仅新增独立纯 C11 parser/header/test 和本文。未修改 AppleDTLib、INF/DSC/FDF、启动参数或固件；不推送部署、不执行 MMIO、不读取物理地址，不从 boot args 猜测映射。

## 格式核实与保守策略

已阅读仓库 AppleDTLib.h 的 dt_node_t/dt_prop_t 和 AppleDTLib.c 的 dt_check/dt_parse/dt_find。节点为 LE32 属性数、LE32 子节点数；属性为 32 字节 key、LE32 length、payload，payload 后补至 4 字节边界。树按前序串行存放。

参考公开 m1n1：
- [C layout/API](https://github.com/AsahiLinux/m1n1/blob/main/src/adt.h)，SPDX GPL-2.0-or-later OR BSD-2-Clause。
- [Python format/template semantics](https://github.com/AsahiLinux/m1n1/blob/main/proxyclient/m1n1/adt.py)，SPDX GPL-2.0-or-later：payload size 为 size & 0x7fffffff，bit31 为 is_template，template 走 CString 解码。

现有 AppleDTLib 使用 0xffffff mask，与 m1n1 的 31 位长度规则不同。本实验**不把整个高字节当已知 flags，也不悄悄截掉它**：bit31 template 不支持；bits24..30 超出本实验支持的长度范围，也统一 RADT_UNSUPPORTED。仅接受高字节为零的长度。未知模板/大长度不能造成错位后继续解析。没有复制两者的递归或指针转换实现，独立实现显式栈。

## 契约和安全边界

- caller 提供已映射、普通内存、整个调用和 view 生命周期内不可变的 buffer + 精确 size。必须自行保证这个 span 真实有效；纯 C 不能验证映射。禁止并发改写。
- caller 提供独立 radt_workspace；它、输出 view 与输入不得重叠。path/key 为 caller 的合法 NUL 终止字符串。workspace 是 scratch，不得当导出接口，其内容在错误后无效。
- 无分配、无递归。固定 8 层栈（root 为第一层），全树最多 256 节点、2048 属性，blob 最多 16 MiB。超预算 fail closed；真实机器树可能超过此实验预算。
- LE32 逐字节读取，允许 buffer 不对齐。用剩余长度减法检查 header/payload/padding，不执行 unchecked off+length 边界判断。
- key 必须在 32 字节内 NUL 终止、非空且可打印；NUL 后 key 字节忽略，但重名按 NUL 前名字拒绝。每节点必须有一个合法 name；所有同级重名拒绝，不择一命中。
- name 必须是单一非空可打印路径组件，最后且仅最后为 NUL，不允许 slash、点或双点。此严格子集可能拒绝合法但未支持的 ADT 变体。
- compatible 必须为完整非空、可打印的 NUL 分隔 token 列表。所有属性（包括非白名单）都进行结构检查；只有整个树消费恰好 size 后才输出 view。多余尾字节也拒绝。
- 显式拒绝 FDT magic d00dfeed。没有 FDT 解析器，也不借用 Linux DT compatible。
- O(P² + N²) 重名检测受到固定总预算限制。不会对输入 counts 做无界循环。

## 输出白名单

radt_validate 只返回状态。radt_lookup 每次重新全树验证，返回借用的原始 bytes view，失败输出清零；不打印、不自动遍历导出，不导出序列号。

精确绝对路径：root 用 /（不加 device-tree），只允许 /、/arm-io/spi3、/arm-io/spi3/ipd、/arm-io/gpio；没有 basename、prefix、alias 或模糊匹配。

- root 属性：model、compatible、board-id。
- 其余三条路径属性：name、compatible、reg、interrupts、interrupt-parent、AAPL,phandle、#gpio-cells、gpio-controller、function-spi_en、function-reset、function-gpio、function-irq、clock-gates、clock-frequency。
- spi3 必须实际包含完整 raw compatible token **spi-1,spimc**；ipd 还必须实际包含 **hid-transport,spi**，同时验证父 spi3。找不到、不匹配、结构错误各返回相应状态；路径存在并不硬编码为成功。
- GPIO 仅原始路径元数据，不宣称已解析 GPIO 控制器类型、引脚或 IRQ。

白名单是查询策略，不是实际 blob 中这些路径/属性均存在的证明。**ranges 地址翻译、reg cell 解码、phandle 解引用、GPIO function 执行均未实现/不支持**。reg 只是不对齐安全的原始 byte view，绝非可用 MMIO 地址。非 name/compatible 的 value 未进行设备特定语义校验，不要把它直接当 C 字符串或整数结构体。

## Host 测试

在本目录执行（临时二进制放 /tmp，不加入固件或源码树）：

~~~sh
tmp=$(mktemp -d /tmp/j313-rawadt.XXXXXX)
clang -std=c11 -Wall -Wextra -Werror -pedantic -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  raw_adt.c test_raw_adt.c -o "$tmp/test_raw_adt"
"$tmp/test_raw_adt"
~~~

合成测试包含正常嵌套/兄弟树、精确 lookup、空 payload、隐私白名单拒绝、缺失属性/路径、compatible 后缀伪匹配、每一个截断 prefix（独立 malloc 形成 ASan 末端红区）、1..7 字节不对齐、尾随字节、8 层成功/9 层拒绝、huge counts、全树节点/属性预算、bits24..31 高长度逐位、UINT32_MAX、无 NUL key、重复属性/同级名字、缺 name、坏 name/compatible、FDT magic，以及固定种子 30000 次随机字节/有效种子 mutation。测试不是完备性证明，不替代真实 blob 验证或持续 coverage-guided fuzzing。

## 许可与来源声明

本目录新增实现为独立代码，SPDX MIT。未复制 m1n1 或 pongoOS 的代码段；上述原始项目许可证保持原样，不声称把 GPL 源码改授 MIT。

为保留已参考 AppleDTLib 的原始许可声明，其头文件归属为 pongoOS / Copyright (C) 2019-2024 checkra1n team；C 文件为 Copyright (C) 2019-2023 checkra1n team，另含 Copyright (c) 2026 Aurora Silicon。其 MIT 文本如下，同时本目录独立新增代码采用相同许可条款：

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
