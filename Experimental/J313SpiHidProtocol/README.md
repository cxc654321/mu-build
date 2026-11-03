<!-- SPDX-License-Identifier: GPL-2.0-only -->
# J313 SPI HID — Phase 1: host-only byte protocol

**这是纯协议实验，不是可用键盘/触控板驱动。** 无硬件、UEFI、Linux API、MMIO、SPI I/O、动态分配或固件接入。没有猜测帧、全零 probe、部署或 push。

## 许可证与溯源

本目录全部新增源码、测试和本文采用 **GPL-2.0-only**，不是 BSD/MIT，也不声称 clean-room。实现参考 GPL SPI HID core 的结构与请求编码，保留上游归属：

- Copyright (C) The Asahi Linux Contributors
- 上游基于 drivers/input/applespi.c：Copyright (c) 2015-2018 Federico Lorenzi；Copyright (c) 2017-2018 Ronald Tschalär。
- [固定 Asahi core 原文](https://raw.githubusercontent.com/AsahiLinux/linux/549e8b442b485c03e3e766635e2203c75362e312/drivers/hid/spi-hid/spi-hid-apple-core.c)，commit 549e8b442b485c03e3e766635e2203c75362e312。实施前已 web_fetch 全文核对，HTTP 200、未截断。原文 SPDX GPL-2.0，本目录明确采用 GPL-2.0-only。
- [GNU GPL version 2 完整许可文本](https://www.gnu.org/licenses/old-licenses/gpl-2.0.txt)。再分发须遵守其条款；无担保。未来固件链接/发布必须另行审查许可证兼容性。
- [本地固定资料](../../Docs/J313-SPIHID-SOURCES.md) 已先行阅读；不修改该文档或已有固件文件。

## 已核对的协议事实

全部多字节字段显式 little-endian，禁止 packed struct 强制转换，允许未对齐输入。

| 对象 | 布局 |
|---|---|
| packet，固定 256 字节 | flags:u8 / device:u8 / offset:LE16 / remain:LE16 / length:LE16 / data[246] / CRC:LE16 |
| message | unknown0:u8 / unknown1:u8 / unknown2:u8 / id:u8 / rsplen:LE16 / length:LE16 / payload[length] / CRC:LE16 |

CRC16 reflected polynomial 0xa001（正向 0x8005），init=0，无末尾 XOR；packet 覆盖前254字节（包括 padding），message 覆盖8字节头+payload。已知向量 123456789 → 0xbb3d。
flags 0x20 输入、0x40 请求及其响应。device 不做猜测性白名单限制。未知字段保持 unknown 命名，rsplen 的完整语义上游也标注不确定。

实际调用核实：设备信息 target d0，20/01/d0，rsplen=0；接口信息 target d0，20/02/i，rsplen=512；描述符 target d0，20/10/i，rsplen=512。上游注释表把接口信息写成20/10，不能据此混同描述符。这里仅提供显式参数请求编码，不自动初始化设备。

## API 与安全策略

- [spihid_protocol.h](spihid_protocol.h)：返回码及接口；[spihid_protocol.c](spihid_protocol.c)：实现。
- sh_reset 初始化上下文；sh_feed 每次只接收精确256字节。SH_MORE=0，SH_MESSAGE=1，负值为错误。
- 总量包括 message header 和 CRC，限制10..2048；每片有效数据1..246。先减法界限校验，再加法/复制，拒绝16位字段形成的大总量。
- 分片 offset 必须连续，device/flags/总长必须恒定；总长=offset+length+remain。完成后要求 message 声明长度**精确**等于总长-10，并验证内层 CRC。
- 所有接收错误清除重组元数据、缓冲区及可用输出；错误片不会当新流重新接受。正在重组时又来 offset=0 也拒绝并复位，调用方须从之后的完整新序列恢复。
- 小于10字节的最后分片可以有效，最小长度约束作用于重组后消息，不机械照搬上游每包短报文过滤。
- 完成输出 payload 借用上下文内部存储，下一次 feed/reset 前有效；不得跨调用保留指针。上下文调用需外部串行化，各输入/输出不允许重叠。
- sh_request 只编码单包，payload<=236（支持零长）；显式接收 ID，不隐式全局递增。输出容量必须>=256，失败不改输出，padding归零但编码结果不是全零探测帧。
- sh_crc16 是底层原语：非零长度必须有相应可读内存；各指针缓冲区尺寸和不重叠是调用方契约。

这里比参考版本更严格：2048实际容量上限、恒定总量与连续性、所有异常清理、精确消息长度。允许零 payload 的合法消息以支持请求编码往返，不将其解释为 HID 输入事件。

## Host 测试

[test_protocol.c](test_protocol.c) 是实际执行的 C 测试，不是 Python 模拟实现。不使用 -DNDEBUG（测试依赖 assert）。在本目录运行，输出二进制放临时目录，不加入源码树：

~~~sh
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 \
  spihid_protocol.c test_protocol.c -o /tmp/j313-spihid-protocol-test
/tmp/j313-spihid-protocol-test
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -g -O1 \
  -fno-omit-frame-pointer -fsanitize=address,undefined \
  spihid_protocol.c test_protocol.c -o /tmp/j313-spihid-protocol-sanitized
/tmp/j313-spihid-protocol-sanitized
~~~

本次 Apple clang 21.0.0 / arm64 macOS：两种构建均成功、两次 ALL TESTS PASSED，退出码0；ASan/UBSan 无报告。覆盖：CRC已知向量；每个总长10..2048的单/多包与短尾片；packet/message bad CRC；0..255截断、257超长；2049超限、65535字段、长度247、零片；乱序/重复首片；device/flags/total mismatch；内部声明长度过长/过短；错误清空和丢弃孤立续片；NULL参数；请求每个payload长度0..236、237/SIZE_MAX拒绝、LE/padding/错误不改输出；显式取消清理。

## 限制与后续 Phase 边界

- 测试帧是根据已核实布局构造的合成离线数据，不是硬件抓包，不证明与实际设备互通；未进行硬件验证或固件构建。
- 不处理短 boot 通知 a0 80 00 00（当前 message API会拒绝它），也不处理4字节 TX status。应由未来经过独立授权的 transport 控制层分派，不能把它们当常规 message。
- 不实现计时器、上电/boot初始化、请求响应关联、ID分配/回绕策略、并发、HID描述符/报告解释、按键转换、UEFI输入协议。超时/总线错误/取消必须由上层调用 sh_reset，纯协议无时钟。
- 无多流交错或发送分片支持；不尝试推断未知 flags。不能用本目录替换现有固件驱动，也不能据此宣称 J313 内置键盘已工作。
