# AT32F435CMU7 内部 Flash Blackbox：可行性与最小实现

## 结论与适用范围

CMU7 可以把未用于固件的内部 Flash 接入现有 Blackbox。此次实现固定保留整个 Bank 2，提供 **1984 KiB（2,031,616 字节）** 的 NOR 日志卷，复用 FlashFS、Blackbox 编码、MSP 日志读取与已有 USB MSC 读取路径；无需增加新日志格式或 Configurator 协议。

这是可编译、可主机测试的实验实现。**尚未完成实机写入、下载、掉电和控制循环时序验证，不能据此认定已适合飞行。** 主机测试模拟控制器，无法证明硅片时序、编程粒度或 USB/ESC 的实际表现。

范围严格限定 AT32F435CMU7。CGU7 为 1024 KiB，不能使用此固定地址方案。没有尝试从当前固件末尾动态划出最大空闲容量：保留 Bank 1 的增长空间和清晰边界，可以少改代码，并避免固件升级后覆盖日志或在同 bank 内频繁擦写。

## 已核实的代码基线

2026-10-09，实验分支 `experimental/F435CMxx_FlashAsBlackbox`、FlyingRC fork 的 `master` 与官方 `iNavFlight/inav master` 均为：

```
940b9281bbefb06b85d442ac5b456c81ae8672c9
```

用户更正后的硬件分支是 [FLYINGRCF435WINGMINI](https://github.com/FlyingRC-Official/inav/tree/FLYINGRCF435WINGMINI)，读取时提交为 `6496bb4ec751d377212a40dfd34af51c6c6d8aa2`。只导入其 `target.h`、`target.c`、`CMakeLists.txt`，以及 CMU7/CGU7 配置擦除粒度支持，不合并整个硬件分支的其他变动。

新增构建 target：`FLYINGRCF435WINGMINI_CMU7_FLASH`。普通 `FLYINGRCF435WINGMINI` 和 `FLYINGRCF435WINGMINI_CMU7` 保留各自原有的存储选择。日志变体也不默认选择 FLASH，必须显式配置。

## 硬件依据及限制

依据官方 [AT32F435/437 参考手册 V2.07](https://www.arterychip.com/download/RM/RM_AT32F435_437_V2.07_EN.pdf)，第 1.3 节、第 5.1/5.2 节：CMU7 的 4032 KiB 主 Flash 包含 2048 KiB Bank 1 与 1984 KiB Bank 2；扇区为 4 KiB；支持 8/16/32 位编程。容量寄存器为 `0x1FFFF7E0`，单位 KiB。手册指出编程/擦除期间读取 Flash 可能暂停 CPU，并要求目标事先擦除。这份手册没有在所查章节明确保证跨 bank 的无停顿读写。

官方 [数据手册 V2.12](https://www.arterychip.com/download/DS/DS_AT32F435_437_V2.12_EN.pdf)，表 18/19：xM 的 sector 擦除典型 45 ms、最大 400 ms；编程典型 50 μs、最大 200 μs；耐久度最低 100k 擦写周期。这些参数属于手册的设计保证，不能代替实机测量。

因此，此实现不依赖“跨 bank 一定不会卡顿”的假设。最小同步写入方案可能增加控制循环延迟，擦除只允许在 disarmed 状态执行。

## 内存布局

| 用途 | 地址范围 | 容量 |
|---|---|---:|
| 启动/向量 | `0x08000000–0x080027FF` | 10 KiB |
| 自定义默认值 | `0x08002800–0x08003FFF` | 6 KiB |
| 配置 | `0x08004000–0x08007FFF` | 16 KiB |
| 固件及其 Flash 加载段 | `0x08008000–0x081FFFFF` | 2016 KiB |
| 内部日志卷 | `0x08200000–0x083EFFFF` | 1984 KiB |

如定义链接符号 `USE_CUSTOM_DEFAULTS_EXTENDED`，扩展默认值放在 `0x081FC000–0x081FFFFF`，固件区域缩减为 2000 KiB。所有初始化数据的加载镜像也受同一个 Bank 1 链接区域限制。

专用链接脚本 `at32_flash_f43xM_blackbox.ld` 预留日志区、导出 `__flashlog_start/end` 并检查边界；固件超出 Bank 1 时链接失败。日志卷没有加载段，不会填充进本次 HEX。刷写工具如果选择整片擦除，仍会删除历史日志。

运行时同时确认：容量寄存器为 4032、链接日志边界等于整个 Bank 2、`FLASH_USD.BTOPT` 已选择 Bank 1 启动。不满足则不注册日志介质，**不自动修改 USD/Option Bytes**。

## 最小实现与必要修正

1. `drivers/flash_at32_internal.c/.h`：独立 NOR 后端，挂到现有 `flashDriver_t`。
2. `drivers/flash.c`：增加一个受 `USE_FLASH_AT32_INTERNAL` 控制的驱动表项；允许无 `flush` 回调。
3. `target/FLYINGRCF435WINGMINI` 与专用链接脚本：按用户板子建立独立 CMU7 日志变体。
4. `config_streamer_at32f43x.c`：CMU7 使用 4 KiB、CGU7 使用 2 KiB 配置擦除粒度，防止导入真实封装宏后编译或擦除行为错误。
5. `io/flashfs.c`：修正短写时仍消费全部请求、同步 flush 无条件清缓冲、EOF 清缓冲后再次推进 tail 的问题；缓冲满时丢弃新字节而不是让 head 追上 tail；容量为零/越界读取安全返回。内部介质擦除被拒绝或失败时不归零指针。
6. `fc_msp.c`：armed 时拒绝日志擦除请求。`cli.c`：擦除失败明确显示失败，而不是输出 Done。

FlashFS 的小修正是必要的：SPI 驱动过去通常返回完整长度，内部控制器错误和短写需要真正按完成字节数推进。外部 NOR/NAND 全擦仍保持原有异步调用语义。

没有改 PG 结构、CLI 设置名、Blackbox 格式和 MSP 消息格式。固件升级暂存区、外部配置区、其他 Flash 驱动与本后端组合会在编译时报错，避免分区容量下溢或探测到错误介质。

### 编程

使用 4 字节逻辑页。对齐且长度足够时执行 word 编程；其他情况执行 halfword/byte。每次访问先检查相对地址、长度和页边界；写入前要求实际目标字节全部为 `0xFF`，写后比较读取结果。只解锁、清标志和锁定 Bank 2。

这避免把不足一个 word 的数据用 `0xFF` 填充后又重复编程同一 word。单次驱动调用最多需要两次编程操作，例如地址偏移 1、长度 3 时为 byte+halfword；FlashFS 跨多个输入缓冲分拆调用时，一个 4 字节片段最多可能产生四次操作。所有路径保留中断使能，但如果硬件读取 Flash 受阻，中断仍可能延迟。

**4 字节页不等于每个控制循环只写一次。** `flashfsWriteByte()` 超过缓冲阈值后会多次触发 flush，同一轮 Blackbox 可能累计多次同步停顿。最坏累计延迟必须实测。

### 擦除和故障

全擦从最后一个 sector 逆序到第一个 sector，496 个 sector，降低中途断电破坏 FlashFS“已用前缀/空闲后缀”扫描假设的概率。目标是否擦净仍以编程前校验兜底；这不是事务性文件系统，掉电可能损失尾部记录或部分正在擦除的日志。

全擦为同步操作：按手册估算典型约 22.3 秒，最坏约 198.4 秒，另有软件开销。期间主循环和 USB 服务可能暂停，Configurator 可能超时；应在拆桨台架、disarmed 时使用 CLI `flash_erase` 并等待完成，不要反复断电重试。没有在飞行中擦除或循环覆盖。

`1000 ms` 只限制进入 SDK 前等待控制器空闲的时间；实际 program/erase 由 SDK 自身的轮询计数超时控制，不是所有操作统一 1 秒超时。

编程、读回校验或擦除失败会锁存错误，将容量置零，使 FlashFS 到 EOF 停写；不会尝试在故障地址上反复编程。忙状态轮询不会因为已锁存故障永久卡死。需要重启后检查并重新擦除；本版本没有新增专门的错误统计/诊断命令。

## 容量和吞吐

按有效日志平均速率估算，忽略头部和每次重启扫描预留空间：

| 实际日志速率 | 1984 KiB 可记录时间 |
|---:|---:|
| 1 KiB/s | 33 分 04 秒 |
| 5 KiB/s | 6 分 37 秒 |
| 10 KiB/s | 3 分 18 秒 |
| 20 KiB/s | 1 分 39 秒 |

这不是实测吞吐量或推荐采样率。对齐 word 写入仅按编程耗时推算为典型 80 kB/s、最坏 20 kB/s；byte 写入对应 20 kB/s、5 kB/s，均未计 CPU、校验及控制任务开销。不能据此给出安全采样频率。

初次台架试验可用 `rate_num=1, rate_denom=32` 降低负载。实际记录频率约为控制循环频率的 1/32，仍需看输出文件速率与 loop jitter，不应把分母直接解释成固定 Hz。

## 构建和使用

使用项目要求的 Arm GNU 13.2.Rel1 / GCC 13.2.1：

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target FLYINGRCF435WINGMINI_CMU7_FLASH -j 8
python3 src/test/at32_internal_flash/run_tests.py
python3 src/test/at32_internal_flash/verify_layout.py --build build
```

构建产物：`build/inav_9.1.1_FLYINGRCF435WINGMINI_CMU7_FLASH.hex`。只用于 CMU7；不要刷入 CGU7 或 STM32F405 板。

首次在拆桨台架连接，备份配置，刷写并确认板子正常启动。启用 FLASH 后保存/重启，让初始化路径实际探测内部介质：

```
set blackbox_device = FLASH
set blackbox_rate_num = 1
set blackbox_rate_denom = 32
save
```

重连后执行 `flash_info`，应显示 NOR、496 sectors、sectorSize 4096、totalSize 2031616。执行 `flash_erase` 等待完成；退出 CLI 后再做解锁/上锁短记录。通过 Configurator 的现有 Blackbox 下载路径读取并验证日志。USB MSC 复用了既有实现，但本次没有实机验证。

如容量为零，应检查真实芯片容量、BTOPT、固件 target 和启动状态；不要为绕过检查直接写 USD 或禁用容量保护。

## 验证及审查记录

- 主机测试实际编译生产驱动、驱动注册/分区建立（`flash.c`）与 FlashFS，模拟擦除/编程控制器，覆盖容量/启动选项拒绝、范围/对齐、word/halfword/byte、已写目标拒绝、短写、环形缓冲跨界、同步大块写、读回错误、超时、EOF 最后 1/2/3 字节、重启扫描、armed 擦除拒绝、逆序全擦和擦除失败。
- 主机将 Bank 1 映射为只读，检查日志操作不会写固件。此模拟保护不代表芯片 MPU 配置。
- 外部 FlashFS 测试覆盖原同步写入与异步全擦行为；检查不兼容的升级、配置和驱动宏组合编译失败。
- 第 1 轮不同 subagent 审查架构：修正短写/擦除状态、配置粒度、保留扩展默认值、分区冲突、掉电擦除顺序，明确实时性缺口。
- 第 2 轮不同 subagent 审查实现：修正外部 NAND/NOR 擦除回归，澄清实际 SDK 超时语义。
- 第 3 轮不同 subagent 最终代码复审：未发现新增阻断缺陷，明确主机模拟和实机证据的边界。
- 实际固件 ELF/HEX 加载段均在 Bank 1；正常/扩展默认值链接布局与超大固件拒绝验证通过。
- `FLYINGRCF435WINGMINI_CMU7_FLASH`、普通 CMU7、CGU7、`NEUTRONRCF435MINI`、`BETAFPVF435` 五个 target 编译成功，无编译警告。
- 第 4 轮不同 subagent 交付复审：独立重跑真实驱动注册/分区集成测试与布局测试，未发现阻断交付的代码缺陷。实机验证仍待执行。

## 实机验收次序

1. 拆桨、限流供电，核对 CMU7 型号/容量/BTOPT，验证普通启动、USB、配置 save/reboot，备份固件与配置区域哈希。
2. 低速率记录、上锁、下载、用日志解析器确认头部和记录完整；重启后追加第二段并确认旧日志保留。
3. 比较启用前后的 loop 最大耗时、gyro/PID 调度、DShot/串口接收、USB 与日志实际字节率。逐步增加日志速率，给正常控制任务留足余量。
4. 用 GPIO/逻辑分析仪观测每次 program 的停顿与单轮累计延迟，实证 Bank 1 取指在 Bank 2 写入时的行为。
5. 测试接近写满、满后停写、擦除拒绝、配置保存与日志互不破坏，以及日志写入/逆序擦除过程断电后的恢复。配置 save 的原有 Flash 停顿不在此次改造范围。
6. 达不到目标控制时序时，下一步应评估更大的 RAM staging、按任务预算写入，或把所需执行链与中断搬到 RAM；这已超出“最小改动”的范围，也不能仅给一个函数加 RAM 属性就宣称消除停顿。
