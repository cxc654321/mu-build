# J313 SPI-HID 移植计划与发布门槛

## 当前基线与纠错

- 当前ESP独立只读重挂已验证：分区UUID 7EF49869-E415-40DD-A6F4-EF9306438D2B，boot.bin SHA256 96a5555a9d6180b21ea2268d8443d0477a2cd33aac34e1be6cb602a28befdf7d，与a26de9a打包文件逐字节一致。写入已证实，实际选择启动链未证实。
- HOLDING 15s FOR THE CAMERA来自J313MemMapDxe，不是旧版本独有标记；现源码已移除其3秒/15秒摄影停顿。
- 原SPI探测不具备可靠CS、整体超时、FIFO边界与协议验证，已隔离，不再部署。安全占位不产生键盘输入。
- ReadKeyStroke不能证明FrontPage绘制；AbsPtr句柄不能证明物理设备；非恒定MISO和distinct计数不能证明SPI-HID帧有效。
- AppleFbDebugLib每实例清屏及逐行延迟可能遮蔽日志；必须新增可持续核验的版本/入口证据。

## Phase 0：可信部署与可见性（当前）

明确设备UUID、CI commit/run、FD哈希、打包输入哈希、备份哈希。发布时禁止复用旧构建物；写入先检查空间和源哈希，以新临时文件校验再替换；sync、卸载、只读重挂、读回逐字节比较。不能把同次挂载的缓存读取当唯一证据。

安全占位入口不读写MMIO、不阻塞、不发布虚假输入协议。删除所有非必要摄影暂停；保留硬件协议时序和有界超时。屏幕显示唯一构建标记及入口状态，后续清屏仍须能重现；不得为可见性引入无界等待或循环刷屏。

退出门槛：host/真实头语法测试通过，CI完整构建通过，产物内标记检查通过，磁盘重挂读回一致。实际入口/屏幕可见性需用户醒来后确认。

## Phase 1：公开资料与板级契约

以J313-SPIHID-SOURCES.md固定commit为准，区分Linux FDT、raw Apple ADT、当前UEFI映射。SPI3实际DTS参考120MHz，不沿用错误200MHz。记录CS0、GPIO enable/IRQ/pinmux、PMGR父域及MMIO访问前置条件。没有真实ADT/映射/电源证据不启动寄存器写入。

许可证门槛：Linux SPI/SPI-HID GPL来源不得标成BSD移植；将参考源码、协议事实、独立实现与许可声明分开记录。需要源代码移植时保留GPL与版权并审查链接发布策略。

## Phase 2：SPI传输与纯字节协议测试

先实现可离线验证的CRC16/256字节packet/message重组与请求编码，覆盖双层CRC、长度<=246、总长<=2048、连续offset、device/flags一致、超时/中止清理。对坏包不得发布输入。

SPI控制器重建为显式CS事务、正确模式/word bits、单调整体deadline、进度约束、仅非空FIFO读取、错误flags及所有出口cleanup；以host MMIO模型覆盖粘滞阈值、无进度、尾字迟到、短收与溢出。禁止全零随机试探代替已知协议请求。

## Phase 3：键盘最小可用

板级验证后：enable GPIO序列→合法boot包→设备/接口信息→HID descriptor→报告解码→EFI_SIMPLE_TEXT_INPUT及EX。只在真实初始化成功后安装接口，区分按下/松开/修饰键/Unicode/ScanCode，队列与事件须有边界。优先PIO轮询，不引入DMA/复杂IRQ。

退出门槛：host协议及输入测试通过，实际按键报告CRC合法且映射正确，UEFI ReadKeyStrokeEx可读，不伪造成功。

## Phase 4：触控板与UI

先相对移动/按钮，再考虑单指绝对坐标，最后多点触控策略；不能把LinuxMT事件直接当UEFI指针。验证SWM/FrontPage真实绘制、指针移动和菜单选择。SPI report02/feature开启/reset恢复依据固定公开源码。

## Phase 5：生命周期与交付

ExitBootServices/卸载停止timer/IRQ、CS回inactive、无dangling callback；共享电源域不得随意关闭。每个安全阶段都经过源码测试→CI→固件提取→部署读回；未通过安全门槛的开发提交不部署。不自动重启、不改NVRAM。

## 夜间执行范围

用户已授权继续研究、开发、CI和测试ESP部署；不把聊天密码存入文件/日志或发子代理。优先现有系统授权。用户睡觉期间进行离线实现/测试/构建，真实键盘/触控板/屏幕验收等醒来反馈，绝不伪称完成。
