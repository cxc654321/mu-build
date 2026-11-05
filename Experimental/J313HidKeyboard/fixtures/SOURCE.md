# 本机内置 SPI HID Keyboard ReportDescriptor

## 结果

- 来源：本机 macOS IOService 注册表，只读查询。
- 二进制：`internal-spi-keyboard.report-descriptor.bin`
- 十六进制：`internal-spi-keyboard.report-descriptor.hex`（同一字节序列）
- 白名单来源证据：`source.json`
- Byte length: 182
- SHA-256: `5ad48fbaddbae4d5806c4dbc27c842e535e2954cd140e208494cf4f17fbc47c7`

## 实际执行命令与匹配边界

```sh
/usr/sbin/ioreg -h
/usr/sbin/ioreg -a -r -c AppleHIDTransportDeviceSPI -p IOService -l -w 0
```

`-h` 返回 usage，exit=1；这是参数说明查询，不是采集失败。第二条命令执行两次：第一次在内存中确认候选的白名单元数据及 descriptor 长度/hash，第二次严格匹配后提取。两次查询均 exit=0。未执行全树查询。

Python subprocess capture_output 在内存接收限定 SPI 子树 plist，plistlib 解析；原始 plist、任何完整树、序列号、LocationID 均未写盘或输出。只保存目标 descriptor、六个白名单属性以及父子 class 证据。查询会短暂在进程内含有 ioreg 返回的其他属性，解析后不保留。

匹配必须同时满足：
1. IOService 子树根 class 为 AppleHIDTransportDeviceSPI，祖先含 AppleHIDTransportProtocolHIDSPI。
2. 目标 class 为 AppleHIDTransportHIDDevice，Transport=SPI。
3. PrimaryUsagePage=1（Generic Desktop），PrimaryUsage=6（Keyboard）。
4. 子树含 AppleHIDKeyboardEventDriverV2；直接 IOHIDInterface 子级 descriptor 与目标完全相同。
5. 唯一候选才保存；不是仅凭 Product 字符串。

来源链：AppleHIDTransportDeviceSPI → AppleHIDTransportProtocolHIDSPI → AppleHIDTransportInterface → AppleHIDTransportHIDDevice → IOHIDInterface → AppleHIDKeyboardEventDriverV2。匹配到一个 SPI 根及一个 keyboard 功能；同根鼠标/触控板及厂商用途功能未作为键盘 descriptor 保存。Transport、SPI 祖先和 keyboard usage/driver 联合区分 Bluetooth/虚拟 HID。此为操作系统注册表归属证据，不是对固件真实性的独立认证。

## 隐私与安全范围

没有 IOHIDDeviceOpen、HID 输入回调、实际按键/输入报告采集、root/sudo、MMIO、ESP 访问或网络下载。IORegistry 中原已存在的 UserClient class 不是本任务打开的客户端。文件仅创建于本目录，目录必须新建成功；所有目标文件采用独占创建，不覆盖已有文件。未修改或运行现有 parser；本 descriptor 不代表受限 parser 已兼容，需离线另行验证。
