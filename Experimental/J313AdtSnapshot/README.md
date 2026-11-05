# J313 ADT snapshot：仅 host 策略实验

**不接入固件，不修改启动链，不访问物理地址，不分配 pool，不 push。**
本目录只有独立 C11 数值范围策略和 host 测试。成功结果是“给定 descriptor 数据允许的读取范围”，**不是页表、地址有效性、内容完整性或所有权已验证**；不可把返回的 base 直接强转为指针。

## 已读取的现有实现与问题

- [PrePi/AdtParser.c](../../Silicon/Apple/AppleSiliconPkg/PrePi/AdtParser.c#L33-L55)：EarlySetup 在 36–38 行先从 BootArgs 读取 devtree/virt_base/phys_base 并做未检查的地址换算；50 行复制 bootargs，52 行按 devtree_size 复制 ADT，55 行才 dt_check。结构检查无法防止此前的源越界、目标越界、整数溢出或访问异常。
- [AppleDTLib.h](../../Silicon/Apple/AppleSiliconPkg/Include/Library/AppleDTLib.h#L47-L75)：devtree 是指针，devtree_size 是 UINT32；boot_args 有 revision/version 和三个 cmdline 版本。读取字段前必须验证所需 span；应由固件编译端用 offsetof/sizeof 和静态断言确认 ABI、指针宽度、对齐、字节序及支持版本，不能照搬 host struct 偏移。
- [T810XFamilyPkg.dsc.inc](../../Silicon/Apple/T810XFamilyPkg/T810XFamilyPkg.dsc.inc#L21-L22)：PcdBootArgsPointer=0x840000000，PcdAdtPointer=0x840004000。二者间隔 0x4000 **不等于 ADT 目标容量**，PCD 也不证明真实运行时内容或映射。
- [MemoryInitPeiLib.c](../../Silicon/Apple/T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c#L314-L448)：BuildVirtualMemoryMap 建立 identity DRAM、MMIO、framebuffer 的 ARM_MEMORY_REGION_DESCRIPTOR；InitMmu 调用 ArmConfigureMmu。DRAM 范围来自 PCD。HOB 代码处理 FD 及 CPU carveout；本文件未见专为这两个 snapshot 源区建立的保留 allocation HOB。因此不能断言 DXE 时原内容没有被 allocator 覆盖。
- [映射常量](../../Silicon/Apple/T810XFamilyPkg/Include/Library/T810XFamilyVirtualMemoryMapDefines.h)：MMIO 的 VA=PA 映射不使其适合当作 ADT 内存读取。ARM mapping table、resource HOB、DXE EFI memory map 和当前页表分别回答不同问题，不能互相替代。

host 的 early_setup_order_model 只用各 8 字节安全数组模拟“先 copy，后拒绝”的次序，确认拒绝前目标已改变。它不是调用真实 EarlySetup/dt_check，也不会故意触发真实地址访问或越界。现有启动链问题仍然存在，DXE 诊断不能追溯修复 PrePi 已发生的破坏。

## API 与拒绝规则

[snapshot_policy.h](snapshot_policy.h) / [snapshot_policy.c](snapshot_policy.c)：

1. caller 把 EFI descriptors 转成普通 SnapMemory：PhysicalStart → base，NumberOfPages × **4096** → length（EFI page 不是 Apple 16 KiB 页表粒度），Attribute → attributes，Type → type；额外已知保护区置 protected_region。转换前检查乘法、长度、count 和 descriptor 边界，不能直接 cast 原 buffer。
2. snap_check_range 用于最先验证 bootargs 读取范围；随后只有外部已独立确认 VA 映射/权限/寿命的代码才可安全读取并保存 devtree_size 标量。
3. snap_plan 输入 bootargs_span、独立可信的 adt_span 容量和已经安全提取的 length。仅返回 bootargs 与 ADT 实际 length 范围；未使用的容量尾部不要求覆盖，但整个 capacity 必须不溢出且不得与 bootargs span 重叠。不能用不可信 length 自己“证明”该容量。
4. 半开区间 [base,base+length)；零长度及加法溢出拒绝。即使最后一字节可表示，若 exclusive end 要达到 2^64 也保守拒绝。地址 0 本身是可接受的数值范围，不表示 NULL 可解引用。
5. blob cap 由 caller 给出，范围 1..16 MiB；length 不得超过 cap、capacity 或 SIZE_MAX。16 MiB 是实验硬上限，不是从平台证实的容量；未来需实测再评审。
6. map 至多 4096 项，要求 4 KiB 对齐、非零长度、无溢出；全 map 不允许任何 descriptor 重叠（包括重复/同类型），避免优先级歧义；允许乱序和相邻跨 descriptor 覆盖，拒绝空洞。复杂度 O(n²)，count 限制是 CPU 预算。
7. 读取涉及的 descriptor 仅允许 EFI types 1–7、9、10（Loader/BootServices/RuntimeServices code/data、Conventional、ACPI reclaim/NVS）。拒绝 Reserved=0、Unusable=8、MMIO=11、MMIOPort=12、PAL=13、Persistent=14、Unaccepted=15 及未知类型。无关的合法 MMIO descriptor 可以存在于 map 中。
8. 涉及范围的 RP/WP/RO 或 protected_region 均拒绝。WP/RO 实际并不一定禁止读，这里按实验要求保守拒绝“protected”；XP 只禁止执行，不当作读保护。未知属性位不自动证明安全，caller 必须核对平台的额外保护并设置 protected_region；本策略不是完整硬件权限解释器。
9. 失败输出清零，不返回部分可读范围。caller 必须提供有效 host 对象及真实 count，输出不可与输入别名。接口不能验证 caller 传进来的 C 指针对象是否有效。

允许 Conventional 或 BootServices memory 仅说明类型策略允许，**不证明源内容保留、可读或无人修改**。不得通过篡改 type、跳过保护或扩大 capacity 来令结果通过。

## 下一版 DXE 诊断的可行步骤（未实现）

1. 只在 Boot Services 有效时调用 gBS->GetMemoryMap。先按规范探测容量，检查 EFI_BUFFER_TOO_SMALL，预留 descriptor 增长空间，AllocatePool 后重试；限制总字节、次数和 descriptor count。分配会改变 map，最终使用成功返回的新快照。验证 DescriptorSize 至少容纳所需 EFI 字段、DescriptorVersion 受支持、MemoryMapSize 是 stride 的整数倍，按返回的 DescriptorSize 步进（不是 sizeof）；检查每个 NumberOfPages 的乘法/端点溢出再转换。失败就记录诊断并跳过 snapshot，不能退回硬编码 DRAM 范围。
2. 在读取 bootargs 之前先检查其 span；**另外**确认当前执行 EL、TTBR/TCR/MAIR、地址转换/identity-mapping 契约、PA 宽度、CPU 读取权限、安全状态及 Normal-memory 属性。必要时用受信任的平台映射接口建立只读 Normal 映射并管理寿命/TLB；本实验没有这样的接口。不做盲读或以捕获 data abort 代替验证。EFI descriptor PhysicalStart 不等于可直接访问的 VA；VirtualStart 也不是任意阶段可使用的通用映射证明。
3. 确认源区在 DXE 前已被恰当地保留且不会被 AllocatePool 等覆盖。若真实 map 显示 Conventional 或无法证明所有权/保留，默认跳过；这里即使 policy OK，也不够支持实际 copy。不能在可能已经被覆盖后才宣称“补保留”恢复原数据。今后任何启动链保留修复需要独立授权。
4. 在已确认可读且稳定的 bootargs span 内读取支持的 revision/version 和 devtree_size 到本地副本，拒绝未知 ABI。PCD ADT 是 EarlySetup 的目标副本，不必再次使用原 devtree VA 换算；若研究原始地址，必须另做减法下溢/加法溢出及映射验证。
5. 用独立证据确定 ADT source capacity，运行 snap_plan。通过后再取得大小受限的 pool，处理分配失败、UINTN 转换、目标可写容量与源/目标不重叠。分配之后重取 map 并复验源映射/所有权（不要让 map buffer 分配递归失控，使用有界重试/预分配），防止检查到拷贝之间状态变化。明确单次读取窗口及并发/DMA 写入约束；必要时报告无法取得一致快照。只复制已验证 length 到自己拥有的 pool，后续解析仅使用该 pool；错误路径释放资源。
6. 先有界结构验证，再输出摘要。raw parser 的 **256 nodes 预算可能不足真实 J313 ADT**；预算耗尽要明确报告 truncated/budget-exceeded，不能宣称完整解析成功、树无效或一定能提取所有设备。应有独立的总字节、node、property、depth、property-length/对齐溢出和运行时间预算，考虑迭代解析或严格限制递归深度；新增真实 blob fixture 后再决定预算。日志仅打印受限摘要，不把指针/任意 property 当作已验证字符串。

这些步骤仅给出下一版设计，未实现 EFI map adapter、实际映射检查、pool copy 或 raw parser。host sanitizer 结果不等于已上机验证。

## Host 测试

在本目录运行：

```sh
make test
```

[Makefile](Makefile) 使用 clang C11、严格 warnings 和 ASan + UBSan。macOS 此环境不支持 LeakSanitizer，所以 detect_leaks=0；并未关闭 ASan，UBSan 设置 halt_on_error=1。

本机结果：`PASS: 66 policy/order checks; no physical address dereferenced`，退出码 0，ASan/UBSan 无报错。覆盖跨 descriptor、乱序、端点、空洞、重叠、零长度、cap/capacity、类型与保护属性、PCD 高地址、近 UINT64_MAX 范围及溢出、无效 map/count、失败输出清零和安全的 copy-before-check 顺序模型。不是实际固件或真实 blob 测试。
