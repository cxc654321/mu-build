<!-- SPDX-License-Identifier: GPL-2.0-only -->
# J313 SPI-HID：离线初始化状态机 Phase

仅新增 [sh_init.h](sh_init.h)、[sh_init.c](sh_init.c)、[test_init.c](test_init.c) 与本文。既有协议/README不变；无 MMIO、SPI 调用、电源操作、固件构建接入、输入安装、push 或部署。

## 公开固定来源与许可证

实际阅读 [Asahi core 固定提交 549e8b442b485c03e3e766635e2203c75362e312](https://github.com/AsahiLinux/linux/blob/549e8b442b485c03e3e766635e2203c75362e312/drivers/hid/spi-hid/spi-hid-apple-core.c)，并与 [已有来源记录](../../Docs/J313-SPIHID-SOURCES.md) 及本目录既有协议实现核对。不是由运行时硬件抓包推导。来源函数/结构：

- spihid_process_read / spi_hid_apple_booted：外层 CRC 合法且 length=4、data=a0 80 00 00 为 boot 短通知；没有完整 message header/inner CRC，不能送入要求至少10字节的 sh_feed。
- spihid_apple_core_probe：device info 请求 target=d0、20/01/d0、rsplen=0；interface info 为20/02/i、rsplen=512；descriptor 为20/10/i、rsplen=512，均空请求payload。上游注释表的 interface info 20/10 与调用矛盾，以调用为准。本阶段按授权仅接口1、2，不请求管理接口0。
- spihid_device_info：24字节前缀；LE16 num_devices@4、vendor_id@6、product_id@8、version@10；三个字符串(offset,length) LE16 对分别@12/16/20。前4字节未知，不当作状态码。
- spihid_iface_info：16字节前缀；interface_num@1、country@5；LE16 input/output/control最大长度@6/8/10；name offset/length@12/14。其他未知字节不猜。
- spihid_process_response：u0=20，u1=01/02/10分派，u2指定设备/接口；descriptor payload原样复制、上限512。不存在在descriptor前添加自创status/length结构的依据。
- spihid_msg_hdr 注释：ID递增，回复rsplen同请求；rsplen准确语义仍被上游标记为推测。

新增模块为 **GPL-2.0-only**，不改标 BSD/MIT。保留上游归属：Copyright (C) The Asahi Linux Contributors；上游基于 applespi.c，Copyright (c) 2015-2018 Federico Lorenzi，Copyright (c) 2017-2018 Ronald Tschalär。上游SPDX旧写法 GPL-2.0；本模块使用明确的 GPL-2.0-only。此实验不代表已完成未来固件许可证兼容性审查。

## API / 信任边界

首次 sh_init_reset，然后 sh_init_restart(now_ms)。流程：BOOT → DEVICE → IFACE1 → IFACE2 → DESC1 → DESC2 → DONE。每次只输出一个 sh_request 编码的256字节请求动作；上层负责何时实际发送、传输状态和取消。动作不是已发送的确认，发出动作即开始该阶段期限。发送失败时上层应停止使用动作并显式restart/reset，同时清理transport。

- now_ms由调用者提供单调uint64毫秒；相等允许，回退进入FAILED。使用差值避免绝对deadline溢出；时钟回绕当作回退。
- 每个等待阶段1000ms；now-start>=1000超时，晚到的正确reply也拒绝。错误回复不推进、不刷新期限。每轮最多64个输入事件和5个请求，无隐式retry；第65个输入进入FAILED。poll不算输入事件，不延长期限。
- boot入口要求完整256字节packet并自行重新验外层CRC、长度4和精确magic；保守要求offset/remain均0。上游未限定boot flags/device，故不编造它们的值；测试的零值不是硬件抓包证明。
- message入口只接受外层已通过 sh_feed 返回SH_MESSAGE的完整消息，调用者保证两个CRC、重组及payload有效期。sh_message没有CRC原始字节，状态机无法再次验证，不能传裸构造的未验证消息。测试负例会主动篡改解析后的元数据以测试防御边界，这不改变生产调用契约。
- 请求关联检查flags=40、device=d0、u0/u1/u2、ID及rsplen。**device=d0回显是保守接受策略，不是上游已验证的接收端规律**：上游response分派并不检查外层device。ID回显也未由上游分派强制；这里故意更严格。不兼容回复会停留等待直至超时，而非放宽猜测。
- DEVICE要求24..512字节、num_devices>=3、vendor非零、字符串区间不越界；不复制字符串。IFACE要求16..512、interface_num等于待定接口、max_input非零、name区间合法。全部用逐字节LE解码，无未对齐struct cast。未知字段不赋予状态语义；上述额外限制是fail-closed策略。
- DESC要求1..512字节，复制到context固定缓冲；必须全部完成才通过sh_init_descriptor对外返回。FAILED/restart/reset清除全部descriptor。输出是**传输CRC、请求关联及长度验证后的bytes，不是HID语义验证后的描述符**，测试故意使用不保证HID合法的内容来维持层次分离。
- 未知类型、错误关联、空/超长/短结构/坏范围不推进。公开core没有通用错误reply结构，因此不编造status字段。若设备以完全相同的成功header及结构承载不可辨别错误，本阶段不能识别；尤其descriptor payload需要后续HID语义校验，不能据DONE安装输入协议。

## Restart / reset / 生命周期

context必须由本API初始化，字段对调用者不透明，外部串行化；输入、context和action缓冲不得重叠。非REQUEST结果的action.packet_length始终0。描述符借用指针只在DONE有效，直到restart/reset或失败。

restart保留已消费ID，不复用旧ID；不足剩余5个ID时拒绝新一轮，避免8位ID回绕使旧reply被接受。reset清空ID和状态，**必须先停用旧transport并drain旧RX/重组数据**，否则相同ID无法区分旧会话；状态机没有线上的epoch，不能假装能防御这种跨reset重放。boot通知同样没有epoch，上层必须隔离旧队列。restart不自行清理sh_rx、取消I/O或复位硬件。

## 实际 host tests

在本目录执行（产物只写临时目录，不进固件/仓库）：

```sh
out=$(mktemp -d /tmp/j313-init.XXXXXX)
clang -std=c11 -Wall -Wextra -Werror -pedantic -O2 spihid_protocol.c sh_init.c test_init.c -o "$out/test_init"
"$out/test_init"
clang -std=c11 -Wall -Wextra -Werror -pedantic -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer spihid_protocol.c sh_init.c test_init.c -o "$out/test_init_san"
"$out/test_init_san"
```

测试覆盖：完整5请求正常链、请求编码反解验证、512字节多packet descriptor；错序接口/类型、旧ID、错device/flags/u0/rsplen；零长、短device结构、字符串越界、interface_num不符；513字节CRC合法多packet超长reply及SIZE_MAX长度防御；boot坏CRC/错magic/短长/非零fragment/重复；阶段边界timeout、boot timeout、晚到正确reply、时间回退、UINT64_MAX附近；部分descriptor失败清除、DONE前不可访问；restart后旧ID、事件计数耗尽、ID预算耗尽。

正常及 AddressSanitizer + UndefinedBehaviorSanitizer 两次实跑均输出 sh_init: all host tests passed，exit code 0，无sanitizer报告。只证明这些合成host测试，不是J313硬件初始化成功；无运行时错误布局、响应外层device或完整ID回显抓包证据。后续phase仍需HID语义、安全策略及另行授权的transport/硬件验证。
