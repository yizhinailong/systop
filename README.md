# systop

Windows 终端资源监控工具，用 C++23 模块编写，界面基于 [FTXUI](https://github.com/ArthurSonzogni/FTXUI)，
使用 [mcpp](https://github.com/mcpp-community/mcpp) 构建和管理依赖。

在一个全屏终端界面里实时显示 CPU、内存、网络、磁盘和进程，可以用键盘或鼠标滚轮浏览进程列表，
按 CPU、内存、名称或 PID 排序，并在运行中调整刷新间隔。

## 监控内容

| 面板     | 内容                                                                           | 数据来源                                       |
| -------- | ------------------------------------------------------------------------------ | ---------------------------------------------- |
| 标题栏   | 主机名、Windows 版本、开机时长、当前时间                                       | `GetComputerNameExW`、注册表、`GetTickCount64` |
| CPU      | 总占用及历史曲线、型号、当前频率、物理核心与逻辑处理器数、每个逻辑处理器的占用 | PDH `Processor Information` 计数器             |
| 内存     | 物理内存和提交内存的占用、可用内存、系统缓存、历史曲线                         | `GetPerformanceInfo`                           |
| 网络     | 接收和发送速率及历史曲线，只统计已连接的物理网卡                               | `GetIfTable2`                                  |
| 磁盘 I/O | 读取和写入速率及历史曲线、磁盘活动时间                                         | PDH `PhysicalDisk(_Total)` 计数器              |
| 磁盘空间 | 每个固定磁盘的占用和剩余空间                                                   | `GetDiskFreeSpaceExW`                          |
| 进程     | PID、名称、CPU%、内存、线程数、句柄数，以及系统进程、线程和句柄总数            | `NtQuerySystemInformation`                     |

CPU 占用优先读取 `% Processor Utility`，不可用时退回 `% Processor Time`。进程的 CPU% 是该进程在
所有逻辑处理器总时间中的占比，内存列为专用工作集。占用达到 60% 显示为黄色，达到 85% 显示为红色。

## 安装

从 [Releases](https://github.com/yizhinailong/systop/releases) 下载 `systop-<版本>-windows-x64.zip`，
解压后运行 `systop.exe`。压缩包已附带所需的 MSVC 运行时 DLL。

界面文字为简体中文，需要支持 Unicode 的终端，例如 Windows Terminal。

## 使用

```sh
systop                      # 默认每秒刷新，进程按 CPU 排序
systop --interval 2 --sort memory
systop -i 0.5 -s name
```

| 选项                     | 说明                                                          |
| ------------------------ | ------------------------------------------------------------- |
| `-i`、`--interval <SEC>` | 初始刷新间隔（秒），可选 `0.5`、`1`、`2`、`5`，默认 `1`       |
| `-s`、`--sort <KEY>`     | 初始排序方式，可选 `cpu`、`memory`、`name`、`pid`，默认 `cpu` |
| `-h`、`--help`           | 显示帮助并退出                                                |
| `--version`              | 显示版本号并退出                                              |

选项值不合法或出现多余参数时，systop 在标准错误输出原因并以退出码 2 结束。

运行时的按键：

| 按键                         | 操作                                                          |
| ---------------------------- | ------------------------------------------------------------- |
| `↑` `↓` 或 `k` `j`、鼠标滚轮 | 在进程列表中移动选中行                                        |
| `PgUp` `PgDn` `Home` `End`   | 翻页，跳到列表开头或末尾                                      |
| `c` `m` `n` `i`              | 按 CPU、内存、名称、PID 排序；CPU 和内存降序，名称和 PID 升序 |
| `空格`                       | 暂停或继续更新画面                                            |
| `+`（或 `=`）`-`             | 在 0.5、1、2、5 秒之间切换刷新间隔                            |
| `q` 或 `Esc`                 | 退出                                                          |

刷新后选中行会跟随同一个进程；切换排序方式时选中行回到列表顶部。

## 从源码构建

需要 Windows x64、mcpp，以及 Visual Studio 的 MSVC 工具和 Windows SDK。
mcpp 的安装方法见 [mcpp 安装说明](https://github.com/mcpp-community/mcpp#install)。
[Windows CI](.github/workflows/windows-ci.yml) 固定使用 mcpp `2026.11.9.1` 和 LLVM `23.1.3`，
本地可以安装同一工具链；下面的 `default` 命令会更改本机 mcpp 的默认工具链：

```sh
mcpp toolchain install llvm 23.1.3
mcpp toolchain default llvm@23.1.3
mcpp self doctor
```

在仓库根目录构建、运行和测试：

```sh
mcpp build
mcpp run -- --sort memory
mcpp test --timeout 90
```

`mcpp pack --release` 在 `target/dist/` 下生成附带 MSVC 运行时 DLL 的发布包，内容与 Release 中的压缩包相同。

依赖版本以 [mcpp.toml](mcpp.toml) 为准，由 mcpp 解析和管理：

| 依赖               | 版本    | 用途                          |
| ------------------ | ------- | ----------------------------- |
| `compat.ftxui`     | `7.0.3` | 终端界面                      |
| `mcpplibs.cmdline` | `0.0.2` | 命令行选项解析                |
| `mcpplibs.mtest`   | `0.5.1` | 单元测试，仅 `mcpp test` 使用 |

程序还链接 Windows 自带的 `advapi32`（注册表）、`pdh`（性能计数器）、`iphlpapi`（网卡统计）
和 `ntdll`（进程列表）。

## 代码组织

```text
systop/
├── src/
│   ├── systop.cppm               # 主模块接口，重新导出各分区
│   ├── main.cpp                  # 解析选项，启动采样线程和界面
│   ├── cli/command_line.cppm     # systop:command_line，命令行选项
│   ├── core/
│   │   ├── sampler.cppm          # systop:sampler，读取系统和进程数据
│   │   ├── history.cppm          # systop:history，历史曲线的采样值
│   │   ├── monitor_state.cppm    # systop:monitor_state，界面状态、排序和选中行
│   │   └── refresh_clock.cppm    # systop:refresh_clock，刷新间隔和采样节拍
│   └── ui/monitor_view.cppm      # systop:monitor_view，FTXUI 界面和按键处理
├── tests/test_*.cpp              # 每个分区一个 mtest 测试程序
├── build.mcpp                    # 从 [package] version 生成 systop:version 分区
├── mcpp.toml                     # 包元数据、依赖和构建配置
└── .github/workflows/            # Windows CI、版本标签和 Release
```

采样在独立线程上按刷新间隔运行，每次得到的快照交给界面线程更新状态并重绘。
`test_sampler` 直接读取本机的实时数据。

## 版本与发布

版本号只写在 `mcpp.toml` 的 `[package] version` 中，`systop --version` 的输出由 `build.mcpp` 在构建时生成。

在 `master` 上修改版本号并推送后，[版本标签工作流](.github/workflows/version-tag.yml) 会创建 `v<版本>` 标签，
[Release 工作流](.github/workflows/release.yml) 在 Windows CI 通过后把发布包上传到 GitHub Release。
也可以手动推送与版本号一致的 `v<版本>` 标签触发发布。

## 许可证

采用 [MIT 许可证](LICENSE)。
