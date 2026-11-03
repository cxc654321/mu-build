# J313 / M1 内置键盘触控板：SPI-HID 公开来源与移植边界

## 范围与结论

本文是公开源代码研究和板级描述交叉核对，不是驱动实现或硬件测试报告。研究没有执行硬件脚本、下载运行代码、读取凭据或改变硬件状态。仅新增本文档。

**J313（MacBook Air M1 2020）的公开实现路线是 Apple SPI-HID。开源不等于“无人做过”：Asahi Linux 已有 SPI 控制器、SPI-HID transport、键盘/HID 和触控板驱动；m1n1 已有相关逆向工具。** UEFI 移植需要替换操作系统接口，不应重新猜测 USB/MTP 总线。触控板代码复用名为 raw_event_mtp 的解析函数，不意味着 J313 使用 MTP 物理传输。

证据级别：
- **源码确认**：固定提交中的实现、数据结构和配置确实如此。
- **配置推导**：从 Linux DTS 默认值/驱动调用语义推导，不等于实测波形。
- **待板级验证**：实际启动链 raw ADT、运行时钟、寄存器遗留状态及 UEFI 行为。

## 固定提交与许可证

提交通过公开仓库 git ls-remote 读取，文件通过固定 commit 的 raw.githubusercontent.com URL 阅读。分支名会移动，引用以下 commit 而非仅引用分支名。

| 仓库 / 分支 | 固定 commit | 用途 |
|---|---|---|
| AsahiLinux/linux bits/090-spi-hid | 549e8b442b485c03e3e766635e2203c75362e312 | SPI-HID 核心、OF glue、SPI controller |
| AsahiLinux/linux asahi | 414e55da7a0afaca2a97b29603be88ce0a6fd6de | 集成 J313 DTS、GPIO、触控板、CRC |
| AsahiLinux/m1n1 main | c5609da67a021ae73d0c03caa109d2c3749e7f61 | SPI/PMGR 定义、GPIO 分析器 |

注意：上述 bits/090-spi-hid 提交的 J313 DTS 本身还没有集成 SPI-HID 节点；板级证据引用 asahi 集成提交，不能混为同一快照。

### Linux 原始来源

- [SPI-HID core，GPL-2.0](https://github.com/AsahiLinux/linux/blob/549e8b442b485c03e3e766635e2203c75362e312/drivers/hid/spi-hid/spi-hid-apple-core.c)
- [SPI-HID OF glue，GPL-2.0](https://github.com/AsahiLinux/linux/blob/549e8b442b485c03e3e766635e2203c75362e312/drivers/hid/spi-hid/spi-hid-apple-of.c)
- [Apple SPI controller，GPL-2.0](https://github.com/AsahiLinux/linux/blob/549e8b442b485c03e3e766635e2203c75362e312/drivers/spi/spi-apple.c)
- [J313 DTS，GPL-2.0+ OR MIT](https://github.com/AsahiLinux/linux/blob/414e55da7a0afaca2a97b29603be88ce0a6fd6de/arch/arm64/boot/dts/apple/t8103-j313.dts)
- [T8103 DTSI，GPL-2.0+ OR MIT](https://github.com/AsahiLinux/linux/blob/414e55da7a0afaca2a97b29603be88ce0a6fd6de/arch/arm64/boot/dts/apple/t8103.dtsi)
- [Apple GPIO/pinctrl，GPL-2.0-only](https://github.com/AsahiLinux/linux/blob/414e55da7a0afaca2a97b29603be88ce0a6fd6de/drivers/pinctrl/pinctrl-apple-gpio.c)
- [Magicmouse，含 SPI 专用分派，GPL-2.0-or-later](https://github.com/AsahiLinux/linux/blob/414e55da7a0afaca2a97b29603be88ce0a6fd6de/drivers/hid/hid-magicmouse.c)
- [CRC16，GPL-2.0-only](https://github.com/AsahiLinux/linux/blob/414e55da7a0afaca2a97b29603be88ce0a6fd6de/lib/crc/crc16.c)

真正的核心文件名是 spi-hid-apple-core.c；不是 spi-hid-apple.c 或 spi-hid-core.c，后两者在所查提交返回404。

### m1n1 原始来源

以下明确标记 GPL-2.0-or-later：
- [SPI 寄存器定义](https://github.com/AsahiLinux/m1n1/blob/c5609da67a021ae73d0c03caa109d2c3749e7f61/proxyclient/m1n1/hw/spi.py)
- [SPI tracer](https://github.com/AsahiLinux/m1n1/blob/c5609da67a021ae73d0c03caa109d2c3749e7f61/proxyclient/m1n1/trace/spi.py)
- [PMGR 寄存器定义](https://github.com/AsahiLinux/m1n1/blob/c5609da67a021ae73d0c03caa109d2c3749e7f61/proxyclient/m1n1/hw/pmgr.py)
- [PMGR 实现](https://github.com/AsahiLinux/m1n1/blob/c5609da67a021ae73d0c03caa109d2c3749e7f61/src/pmgr.c)
- [GPIOLogicAnalyzer](https://github.com/AsahiLinux/m1n1/blob/c5609da67a021ae73d0c03caa109d2c3749e7f61/proxyclient/m1n1/gpiola.py)

[实验 SPI 脚本](https://github.com/AsahiLinux/m1n1/blob/c5609da67a021ae73d0c03caa109d2c3749e7f61/proxyclient/experiments/spi.py)仅作反例：它使用 arm-io/spi2 加0x8000以及 arm-io/gpio0，包含实验性寄存器写入；不是 J313 键盘初始化配方，本文没有运行它。该文件未见单独 SPDX，不能自行宣称额外许可。[仓库 LICENSE](https://github.com/AsahiLinux/m1n1/blob/c5609da67a021ae73d0c03caa109d2c3749e7f61/LICENSE)为 GPLv2 文本。

未找到猜测的 src/spi.c、src/gpio.c、proxyclient/m1n1/hw/gpio.py，不能把它们列为已存在的参考实现。

## 1. 硬件映射与 raw ADT / Linux FDT 分离

公开集成 DTS 配置确认：

| 项目 | J313 / T8103 配置 |
|---|---|
| 控制器 | SPI3，MMIO 0x23510c000，长度0x4000 |
| 控制器 IRQ | AIC IRQ617，level-high |
| SPI 设备 | CS0，最高8MHz |
| SPI3 输入时钟 | 120MHz，引用 clk_120m |
| pinmux | AP46、47、48、49，function1；各引脚信号次序尚未由本文证据确定 |
| 设备使能 | AP GPIO195，active-high |
| HID 通知 | NUB GPIO13，level-low，与控制器 IRQ617不同 |
| GPIO MMIO | AP 0x23c100000；NUB 0x23d1f0000 |

**120MHz vs 旧本地200MHz：** 委派上下文提到旧本地200MHz值，必须视为旧快照/旧配置差异，不能未经核对选用。本文写入前实际读取的当前目标工作树 [T8103 DTSI:863–874](../Silicon/Apple/AppleSiliconPkg/DeviceTree/t8103.dtsi#L863-L874) 已经是 SPI3=120MHz；同文件 SPI0/1=200MHz，不可混淆。本次亦读过 mu-ref 的 SPI3=120MHz。因此“当前 clean-build SPI3 是200MHz”不符合本次读到的文件；旧200MHz的具体来源/版本仍需定位。公开120MHz配置与实际板级参考时钟/分频行为仍须在后续授权测试中确认；本文不修改时钟代码或配置。

本地文件仅作交叉证据，不是原始硬件规范。另已读取 [mu-ref J313:76–95](../../mu-ref/Silicon/Apple/AppleSiliconPkg/DeviceTree/t8103-j313.dts#L76-L95)、[mu-ref T8103:863–874](../../mu-ref/Silicon/Apple/AppleSiliconPkg/DeviceTree/t8103.dtsi#L863-L874)、[mu-ref PMGR:381–387](../../mu-ref/Silicon/Apple/AppleSiliconPkg/DeviceTree/t8103-pmgr.dtsi#L381-L387)。PMGR 描述中 SPI3 state offset0x258，父域 ps_sio/ps_spi_p；offset不是独立物理地址。

**Linux FDT 的 apple,spi-hid-transport、apple,t8103-spi、phandle、power-domains 不能直接假定存在于 raw Apple ADT。** BoardInfo 适配层应独立解析当前启动链的板型、SoC、reg/ranges 地址翻译、clock-gates和GPIO标识，验证后输出统一资源结构。本文没有获得目标板 raw ADT compatible或确切SPI节点路径的直接证据。

## 2. PMGR、GPIO、pinmux、IRQ

m1n1 PMGR 解析 raw ADT /arm-io/pmgr 的 devices、ps-regs，读取目标节点 clock-gates；递归开父域，再开子设备，等待 ACTUAL[7:4] 等于 TARGET[3:0]，使用有界超时。不要以 Linux phandle 替代 raw ADT 的 clock-gates ID，也不要仅写 target 后立即触碰断电外设。

GPIOLogicAnalyzer 证明按传入 ADT 节点 get_reg(0)取base，base+pin*4读bit0；它不是完整GPIO/pinmux驱动。Linux GPIO实现定义 DATA bit0、MODE[3:1]、PERIPH[6:5]、INPUT_ENABLE bit9；OUT模式1，低电平IRQ模式3，IRQ关闭模式7。移植应 read-modify-write，保留未知位、pull、drive、lock等设置，并管理GPIO域供电。

OF glue 的设备使能顺序：逻辑1等5ms → 0等5ms → 1等50ms。IRQ先保持关闭，transport准备好后启用。设备通知IRQ和SPI FIFO/完成IRQ必须分别管理。

可先设计有界PIO+GPIO电平轮询，后接AIC中断；轮询同样涉及硬件操作，必须在独立授权测试阶段才启用。UEFI高TPL回调不能阻塞等待1秒。

## 3. SPI 与 CS 时序

Linux控制器与m1n1可交叉定位：CTRL0x00，CFG0x04，PIN0x0c，TXDATA0x10，RXDATA0x20，CLKDIV0x30，RXCNT0x34，TXCNT0x4c，FIFOSTAT0x10c，IF_XFER0x134。RUN为CTRL bit0，FIFO reset bit2/3，CS为PIN bit1；IF_XFER完成位RX0/TX1。

存在不可忽略的资料差异：m1n1 CLKDIV注释写 CLK/(DIVIDER+1)，Linux控制器实际写 ceil(clk/speed)；m1n1旧STATUS.TX_COMPLETE是bit22，Linux旧STATUS宏为bit2，工作传输使用IF_XFER。不要机械拼接寄存器定义；最终分频语义和实际SCK须板级核验。

板级CS setup65µs、hold65µs、inactive250µs。DTS解释称ADT20µs与HID metadata45µs单独不可靠，相加可用；**65µs是实际采用配置，二者为何累加仍是源码作者的best guess**。

- RX：一个256字节读取事务。
- TX：同一个spi_message内写256字节，transfer后延迟200µs，再读4字节状态。
- transfer未设cs_change，按SPI框架语义跨这两段保持CS有效；最终再释放CS。
- 状态成功值 ac 27 68 d5。
- 200µs是阶段间延迟，不是250µs CS inactive的替代。
- Mode0/MSB-first来自DTS未声明CPOL/CPHA/LSB的默认解释，不是本文实测。

## 4. Packet、CRC、reassembly

256字节packet布局：flags:u8、device:u8、offset:LE16、remain:LE16、length:LE16、data[246]、CRC:LE16。

flags=0x20为输入，0x40为写及其响应；device0管理、1键盘、2触控板、0xd0信息。message为8字节header（unknown0/1/2、id、rsplen:LE16、length:LE16）+payload+CRC2。未知字段的语义不能包装成完整官方HID-over-SPI规范。

packet CRC覆盖前254字节；message CRC覆盖header+payload。Linux CRC16 polynomial0x8005，反射实现对应0xa001，初值0，无末尾xor，CRC小端存储。

重组必须保持device/flags一致、offset连续、offset+length+remain总长恒定，最终验证message CRC。建议独立纯字节协议模块，加入length<=246、总长<=实际buffer、message至少10字节、整数溢出、超时及中断序列清理。参考版本分配2048字节重组缓冲但仅有U16_MAX总长检查，不能机械复制这类边界处理。对GET_REPORT复制长度等也应重新审计，不能把公开源码视作已证明无内存安全问题。

## 5. 初始化与报告

1. 上电，启用通知接收。
2. CRC合法packet中data=a0 80 00 00表示booted，参考超时1秒。
3. 设备信息请求target0xd0，message20/01/d0。
4. 接口0..2信息请求20/02/i，响应空间512。
5. 接口1/2 HID report descriptor请求20/10/i，响应空间512。
6. 描述符验证后才对外发布输入设备。

core注释表把iface info写成20/10，但实际调用20/02；应以实际调用区分信息和descriptor，不照抄注释错误。消息id递增，移植需显式请求/响应关联与超时，避免仅依赖某字段变为非零。

键盘应按取得的HID report descriptor解析，不默认USB boot固定8字节布局。输入报告unknown0=0x10，payload交给HID层。

触控板公开SPI路径：report ID0x02；先跳8字节tp_mouse_report（ID/buttons/relx/rely/pad4），再解析38字节vendor header和N×30字节finger records，总prefix46字节。坐标为signed LE16，Linux将Y取负。feature {02,01}经SET_REPORT、transport message52/02/00启用MT；reset report0x60触发延迟10ms重新启用。不要将其他机型J314尺寸常量未经核验当J313尺寸。

## 6. 可移植分层方案（设计建议，非已实现事实）

1. **BoardInfo/ADT适配**：独立于Linux FDT；验证资源与机型，不以猜测compatible探测。
2. **平台HAL**：PMGR、GPIO、pinmux、AIC、MMIO屏障、计时、有界等待和错误回滚。
3. **SPI PIO事务**：控制FIFO、CS生命周期、速度、阶段延迟与错误恢复；最初不引入DMA。
4. **SPI-HID transport**：纯字节packet/CRC/reassembly、boot/init状态机、请求回复、限制与日志。
5. **HID语义层**：descriptor解析、键盘状态、鼠标、可选厂商MT格式；和transport解耦。
6. **UEFI适配**：键盘EFI_SIMPLE_TEXT_INPUT_PROTOCOL及EX，队列/WaitForKey、ScanCode/Unicode、修饰键、锁定键与LED；触控板EFI_SIMPLE_POINTER_PROTOCOL，必要时提供明确单指映射的EFI_ABSOLUTE_POINTER_PROTOCOL。UEFI标准输入协议不等于Linux多点触控slot接口。

先离线测试CRC、分片、畸形长度、描述符与按键转换；再在明确授权下做硬件分层验证。Boot或descriptor失败时不安装“看似存在但无输入”的协议。ExitBootServices取消计时器、关本驱动IRQ、停止事务并使CS inactive；电源是否恢复按启动链交接契约决定，不任意关共享父域。

## 7. 许可证与未确认项

GPL源码不能直接复制后改标BSD/MIT。DTS可选择MIT分支并保留通知；驱动、m1n1实现的GPL条款须分别评估。建议先保留协议事实和接口规范，决定GPL兼容模块或经审查的独立实现路径；仅声称“重写”不自动消除衍生作品问题。

已确认的是公开实现的SPI路线、板级配置、包结构和初始化/报告处理。尚未实测确认：本机raw ADT路径与compatible、运行时父时钟及divider语义、各SPI pin信号次序、CS波形、GPIO/PMGR遗留状态、UEFI交接和输入可用性。本文不宣称这些已经工作。
