# Lab1 数据链路层实验说明

这是本实验的 Linux/WSL 版本。程序会启动两个本地进程，分别模拟站点 `A` 和 `B`，它们通过 `protocol.c` 提供的仿真信道通信，真正的协议逻辑放在 `protocols/` 目录里，可以按需要编译基础 SR、优化版 SR 或 GBN 版本。

## 目录结构

和协议实现直接相关的文件如下：

```text
Lab1-linux/
├── protocols/
│   ├── sr.c        # Selective Repeat，当前默认编译版本
│   ├── sr_opt.c    # 优化版 Selective Repeat
│   └── gbn.c       # Go-Back-N
├── datalink.h      # 帧类型定义
├── protocol.h      # 实验库接口声明
├── protocol.c      # 实验库：物理层、网络层、事件和定时器
├── lprintf.c       # 日志输出
├── crc32.c         # CRC32
└── Makefile
```

## 编译

在 WSL/Linux 里进入目录后执行：

```bash
cd '/mnt/d/desktop/Lab1-2024(Win+Linux)/Lab1-linux'
make clean
make
```

`make` 默认编译 `protocols/sr.c`，生成可执行文件：

```bash
./datalink
```

### 选择协议

当前 Makefile 支持按协议编译：

| 命令 | 含义 |
| --- | --- |
| `make` | 默认编译基础 SR，即 `protocols/sr.c`。 |
| `make sr` | 编译基础 SR。 |
| `make sr_opt` | 编译优化版 SR，即 `protocols/sr_opt.c`。 |
| `make gbn` | 编译 GBN。 |
| `make PROTOCOL=sr datalink` | 显式选择 `protocols/sr.c`。 |
| `make PROTOCOL=sr_opt datalink` | 显式选择 `protocols/sr_opt.c`。 |
| `make PROTOCOL=gbn datalink` | 显式选择 `protocols/gbn.c`。 |
| `make list` | 查看当前 Makefile 中列出的协议名。 |
| `make clean` | 删除 `datalink`、中间 `.o` 文件和根目录日志文件。 |

当前可用协议可以用下面的命令查看：

```bash
make list
```

注意：`make clean` 会删除根目录下的 `*.log`，需要保留性能测试日志时请先备份或使用 `-l <file>` 指定单独文件名。

### 协议版本说明

| 协议名 | 源文件 | 用途 |
| --- | --- | --- |
| `sr` | `protocols/sr.c` | 基础 SR，保留当前已验证的 32 发送窗口实现，作为默认版本。 |
| `sr_opt` | `protocols/sr_opt.c` | 优化版 SR，参考学长实现思路，使用环形缓存、短 ACK/NAK 控制帧和独立 DATA 重传。 |
| `gbn` | `protocols/gbn.c` | Go-Back-N 协议实现。 |

优化版 SR 的主要逻辑：

| 机制 | 说明 |
| --- | --- |
| DATA 帧 | 携带 CRC，按 `seq % NR_BUFS` 使用发送和接收缓存。 |
| ACK/NAK 帧 | 使用 3 字节短控制帧，减少确认帧开销。 |
| DATA 定时器 | 定时器编号使用缓存下标，超时时再映射回当前发送窗口里的真实序号。 |
| NAK | 针对当前接收窗口缺失的 `frame_expected` 发送，触发对端单帧重传。 |

测试优化版 SR 时，先重新编译：

```bash
make clean
make sr_opt
```

之后仍然使用下面的启动和测试命令。

## 启动方式

要先开两个终端，先启动站点 `A`，再启动站点 `B`。

终端 1：

```bash
cd '/mnt/d/desktop/Lab1-2024(Win+Linux)/Lab1-linux'
./datalink a
```

终端 2：

```bash
cd '/mnt/d/desktop/Lab1-2024(Win+Linux)/Lab1-linux'
./datalink b
```

其中：

- `a` 表示站点 A
- `b` 表示站点 B

`A` 会先监听本地 TCP 端口，`B` 再去连接 `A`。这个 TCP 连接只是实验库内部用来模拟物理层信道，真正的链路层协议由本次编译选择的 `protocols/*.c` 文件实现。

## 参数写法要注意

`-u`、`-f`、`-t` 这些是选项，必须写在站点名 `a` 或 `b` 前面。

正确写法：

```bash
./datalink -u -t 120 a
./datalink -u -t 120 b
```

不要这样写：

```bash
./datalink au
./datalink bu
```

因为 `au` 只会被当成站点名字符串，程序只认第一个字符 `a`，后面的 `u` 不会被当成 `-u` 选项。

## 常用 flag 含义

| 参数 | 含义 |
| --- | --- |
| `a` | 启动站点 A，A 作为 TCP 服务端。 |
| `b` | 启动站点 B，B 作为 TCP 客户端。 |
| `-u` / `--utopia` | 无误码信道，把本站接收方向误码率设为 0。 |
| `-f` / `--flood` | flood 模式，网络层持续高速产生分组，用于测吞吐上限。 |
| `-b <rate>` / `--ber=<rate>` | 设置本站接收方向误码率，例如 `-b 1e-4`。 |
| `-t <seconds>` / `--ttl=<seconds>` | 运行指定秒数后自动退出，长测常用 `650`、`900`、`1200`。 |
| `-d <mask>` / `--debug=<mask>` | 打开调试输出，bit0 为事件、bit1 为帧、bit2 为警告；常用 `-d3`。 |
| `-p <port>` / `--port=<port>` | 指定 TCP 端口，端口冲突或多组并行测试时可改。 |
| `-l <file>` / `--log=<file>` | 指定日志文件名，建议性能测试时为每组测试单独命名。 |
| `-n` / `--nolog` | 不生成日志文件。 |
| `-i` / `--ibib` | 改变站点 B 的网络层发包节奏，使 B 的 busy/idle 周期与默认相反。 |
| `-?` / `--help` | 打印帮助信息。 |

实验库默认信道参数：

| 参数 | 数值 |
| --- | --- |
| 带宽 | `8000 bps` |
| 单向传播时延 | `270 ms` |
| 默认误码率 | `1e-5` |
| 分组长度 | `256 bytes` |

## 推荐测试命令

下面每组命令都要先在终端 1 启动 `A`，再在终端 2 启动 `B`。

### 1. 无误码烟雾测试

先用这个检查程序最基本能否正常跑通。

终端 1：

```bash
./datalink -u -t 120 a
```

终端 2：

```bash
./datalink -u -t 120 b
```

日志开头应显示：

```text
bit error rate 0
```

### 2. 无误码调试输出

这个适合看 DATA、ACK、超时、捎带确认的行为。

终端 1：

```bash
./datalink -u -d3 -t 30 a
```

终端 2：

```bash
./datalink -u -d3 -t 30 b
```

你可以重点看这些输出：

```text
Send DATA ...
Recv DATA ...
Recv ACK ...
---- ACK timeout, send standalone ACK
Send ACK ...
```

### 3. 默认误码信道

不加 `-u` 时就是默认误码率 `1e-5`。

终端 1：

```bash
./datalink -t 120 a
```

终端 2：

```bash
./datalink -t 120 b
```

日志开头应显示：

```text
bit error rate 1.0E-05
```

### 4. 无误码 + flood

这个主要看协议在理想环境下的性能上限。

终端 1：

```bash
./datalink -f -u -t 120 a
```

终端 2：

```bash
./datalink -f -u -t 120 b
```

### 5. flood + 默认误码

这个是当前 GBN 很重要的压力测试。

终端 1：

```bash
./datalink -f -t 120 a
```

终端 2：

```bash
./datalink -f -t 120 b
```

### 6. flood + 高误码率

这个更难，通常在前面的测试稳定后再跑。

终端 1：

```bash
./datalink -f -b 1e-4 -t 120 a
```

终端 2：

```bash
./datalink -f -b 1e-4 -t 120 b
```

### 7. 验收式长时间运行

验收说明里要求能稳定运行一段较长时间，建议直接跑 15 分钟或 20 分钟。

15 分钟：

```bash
./datalink -f -t 900 a
./datalink -f -t 900 b
```

20 分钟：

```bash
./datalink -f -t 1200 a
./datalink -f -t 1200 b
```

两条命令要分别放在两个终端里执行。

## 端口冲突

如果默认端口被占用了，可以给两边换一个端口，但必须保持一致。

终端 1：

```bash
./datalink -p 60001 -u -t 120 a
```

终端 2：

```bash
./datalink -p 60001 -u -t 120 b
```

## 日志怎么看

默认会生成：

```text
datalink-A.log
datalink-B.log
```

日志里常见的统计行像这样：

```text
.... 688 packets received, 6092 bps, 76.15%, Err 17 (1.1e-05)
```

可以这样理解：

| 部分 | 含义 |
| --- | --- |
| `688 packets received` | 当前站点成功交付到网络层的分组数。 |
| `6092 bps` | 平均有效吞吐率。 |
| `76.15%` | 有效吞吐率占 8000 bps 信道带宽的比例。 |
| `Err 17` | 实验库插入的接收侧错误次数。 |
| `(1.1e-05)` | 当前观测到的误码率估计值。 |

判断协议是否还活着，最直观的方法就是看两边的 `packets received` 是否持续增长。

## 调试建议

一般先用：

```bash
./datalink -d3 -t 30 a
./datalink -d3 -t 30 b
```

如果还想看警告信息，可以试：

```bash
./datalink -d7 -t 30 a
./datalink -d7 -t 30 b
```

## 回归测试顺序

每次改完某个 `protocols/*.c`，建议按这个顺序回归测试：

1. `-u -d3 -t 30`，看帧级行为是否正确。
2. `-u -t 120`，看无误码稳定性。
3. `-t 120`，看默认误码下的恢复能力。
4. `-f -t 120`，看 flood 压力。
5. `-f -b 1e-4 -t 120`，看高误码恢复。
6. `-f -t 900`，看长时间稳定运行。

SR 版本应该满足的行为是：

| 场景 | 预期行为 |
| --- | --- |
| 按序收到 DATA | 交付分组，推进接收窗口，延迟发送 ACK。 |
| 收到窗口内乱序 DATA | 缓存到 `in_buff`，必要时发送 NAK 催缺失帧。 |
| 缺失帧补到后 | 连续交付已经缓存的帧，并检查是否出现新的缺口。 |
| DATA 超时 | 只重传对应超时的数据帧。 |
| ACK 超时 | 如果 ACK 没有被捎带出去，就单独发送 ACK。 |

GBN 版本应该满足的行为是：

| 场景 | 预期行为 |
| --- | --- |
| 按序收到 DATA | 交付分组，推进 `frame_expected`，延迟发送 ACK。 |
| 收到乱序或重复 DATA | 不交付，发送当前最后按序帧的 ACK。 |
| DATA 超时 | 从 `ack_expected` 开始重传整个窗口。 |
| ACK 超时 | 如果之前的 ACK 没有被捎带出去，就单独发 ACK。 |
