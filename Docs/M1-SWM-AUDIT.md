# M1 / J313 SWM 复查与修复候选

## 结论

不存在需要另造的 M1 私有窗口管理协议。当前固定的 MU 子模块 `fa4cfd83ae6bb5c046c19e5971a64043d1cb0569` 已提供微软通用 `SimpleWindowManagerDxe`，真实窗口/绘制/输入接口均已实现。协议 GUID 为 `9d400d20-6f35-4268-904f-dc04b1877b62`。M1 专用工作是 framebuffer GOP 到 Mu SRE 的接入和控制台初始化时序，不是安装空 SWM 协议。

公开项目入口：[微软 mu_plus 图形包](https://github.com/microsoft/mu_plus/tree/release/202311/MsGraphicsPkg)、[当前使用的 Aurora fork](https://github.com/aurora-silicon/mu_plus)。本次具体行为依据本地固定版本源码；raw.githubusercontent.com 在线获取超时，没有把网页目录当成最新源码验证。

## 已确认的源码缺口

- `WindowManager.c:1591-1601` 仅在 ConsoleOutHandle 查询 GOP，找到就直接 Stage2；SRE 或输入未就绪会失败。
- `WindowManager.c:1604-1634` 注册 GOP 通知后不主动检查既有协议，晚注册可能漏掉已存在的 GOP/SRE。
- `WindowManager.c:1504` 用 mGop 非空代替初始化状态；异步 Stage2 失败会 Cleanup 并停止等待。
- `WindowManager.c:1289-1341` 显示 Stage2 还依赖 SRE、HII Font、字体包及当前 ConsoleInHandle 上的 TextInputEx。
- `ConSplitter.c:480-498` 先安装 TextInputEx，再设置系统表 ConsoleInHandle，因此仅增加协议安装通知不够。
- `MsUiThemeLib/Dxe/MsUiThemeLib.inf` 的主题 Depex 必须保留；constructor 缓存主题，不能声称计时器可以补救 constructor 前缺主题。
- SWM 安装成功后仍有 timer、toolkit、driver binding 初始化；协议安装日志不等于完整成功。

## 本工作树的修复候选

1. 现有 `J313SreHandoffDxe`（b27558e）在 console drivers 前显式启动 SRE，将 SimpleFb GOP 接入 SRE；失败尝试回滚。不使用全设备 ConnectAll。
2. `Tools/swm-readiness.py` 在干净固定版本的 SWM 源码上应用初始化就绪修复：保持 UEFI_DRIVER，检查同一 handle 上 GOP+SRE，先检查依赖，主动检查既有协议，以低频 timer 补偿控制台句柄后置更新，并避免重复进入有副作用的 Stage2。
3. J313 workflow 改用上述修复脚本，停止运行旧 ui-trace.py 的模块类型转换和不完整通知注入。诊断 v2 位于独立 diag 工作树，不与本脚本混用。

不要在已被旧 ui-trace 修改的子模块上叠加本脚本。先在独立构建树准备干净的固定子模块。脚本校验失败应停下检查版本，不能强制忽略。主构建树 main 和现有固件未在本次自动覆盖、推送或部署。

## 验证与范围

已复跑 `python3 -B Tests/test_j313_sre_handoff.py`：3 tests、15 C mock fault scenarios 全部通过。它不等于完整 DXE/硬件测试。SWM 新测试最终 5 tests / 16 个编译后 C 场景通过，覆盖晚到依赖、同句柄选择、重入、事件错误、等待期卸载、发布验证失败及补丁幂等/漂移拒绝。中间修订曾出现过输出指纹过期和废弃测试桩导致的失败，已修正并重跑通过；Stage2 本身仍为 mock，不代表整个 UI 渲染路径已通过。

已再次使用真实 UEFI/MsGraphics 头对 J313SreHandoffDxe 做 AARCH64 clang syntax-only 检查，通过（PCD 用语法检查占位宏，不是链接构建）。

本机存在 Apple clang，但常用 Homebrew LLVM/lld 路径缺失；隔离 handoff 工作树子模块未初始化。尚未完成新固件链接/GenFds，尚未进行 J313 实机启动。上游 Stage2 的部分失败清理仍有资源局限。本候选失败路径采用保守停止：尝试停止 watchlist timer、撤销 SWM 发布，但保留其他接口引用的对象及驱动代码直到重启，不释放客户端，不重复初始化；Stage2 开始后拒绝卸载。资源保留不是完整回滚，撤销失败必须看日志，不能称为完整热卸载/热插拔实现。

最新本地候选是 artifacts 中 e5e03e7-blockio（11月3日17:07），无对应在机 hash/本轮串口日志，无法确认用户现在实际启动了哪版。六个模块 LOADED 只能说明加载，不能证明协议已发布。

最终联合回归：SWM readiness + boot gate 共8项测试通过；另 handoff 3项测试通过。boot gate 生产 helper 实编译运行16场景 × 两种等待预算 =32组合通过，真实 UEFI 头 AARCH64 `-Wall -Wextra -Werror -fsyntax-only` 通过。`git diff --check` 通过。以上均为本机实跑或独立审阅实跑，尚未执行远端 CI。

## 启动时序保护与剩余限制

- 已补平台 BDS 的两处 UI 入口检查：设置启动路径、无可启动设备路径在选择菜单前检查 SWM，不存在时用 WaitForEvent 等待，100ms × 最多20次；超时/事件错误返回，不启动菜单，也不因等待失败重启。普通启动路径不增加等待。约2秒限制是计时等待预算，不是对卡死的固件回调的强制超时。
- 该检查确认 SWM 协议可找到，不是新的全局 Ready 协议；若初始化失败且协议撤销也失败，残留协议仍可能被找到，此时必须依据 readiness 错误日志停止验收。
- SWM 选择同句柄 GOP/SRE，不等于整个 UIToolKit 已支持多显示选择；上游 UIToolKit 仍有全局 LocateProtocol(GOP)。J313 handoff 的单 framebuffer 条件不能推广为所有 M1 外接多屏都正确。
- 独立审阅已将新 GLOBALS/CALLBACK/ENTRY 配真实 WindowManager.h 做 AARCH64 syntax-only，协议类型检查通过；最终追加完整 CLEANUP/Unload 后重新验证：`-Werror` 零警告、零错误。这不是完整固件链接。

## 本地复查命令

在本 handoff 工作树根目录：

```sh
python3 -B Tests/test_j313_sre_handoff.py
python3 -B Tests/test_swm_boot_gate.py
SWM_SOURCE_TREE=../mu-build-tree python3 -B Tests/test_swm_readiness.py
# 只检查，不修改主树
python3 -B Tools/swm-readiness.py --tree ../mu-build-tree
```

在已经初始化干净子模块的独立构建树内才应用：

```sh
python3 Tools/swm-readiness.py --tree "$PWD" --apply
# 然后执行该构建树的正常 J313 stuart 构建流程
```

不要将上面的只读检查命令随意追加 --apply 后指向正在维护的主树。CI 已显式选择自身 checkout，不默认写兄弟目录。

## 实机验收

- 从干净构建树应用补丁后生成固件，记录父仓库 commit、子模块 commit、修改摘要和固件 SHA-256。
- 优先 RAM payload 测试；本次不自动写 ESP/NVRAM。
- 串口必须看到 handoff 成功且 GOP/SRE 位于同一 handle，然后 SWM readiness 依赖满足、Stage2 返回成功；不能只搜安装前字符串。
- 确认之后未 Cleanup/卸载 SWM，再看 FrontPage/BootMenu 是否真正绘制。
- 缺依赖时按日志分别核对 SRE、HII/主题、聚合输入，不将未实现的物理键盘/触控板当成窗口协议缺失。
- 图形成功后单独验证鼠标/键盘输入、内部盘 Block I/O、Windows boot manager；本修复不证明后三者已工作。
