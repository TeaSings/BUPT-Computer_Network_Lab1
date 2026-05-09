# Lab1 数据链路层实验说明

这是本实验的 Linux/WSL 版本。程序会启动两个本地进程，分别模拟站点 `A` 和 `B`，它们通过 `protocol.c` 提供的仿真信道通信，真正的协议逻辑写在 `datalink.c` 里。

## 编译

在 WSL 里进入目录后执行：

```bash
cd '/mnt/d/desktop/Lab1-2024(Win+Linux)/Lab1-linux'
make clean
make
```

编译完成后生成可执行文件：

```bash
./datalink
```

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

`A` 会先监听本地 TCP 端口，`B` 再去连接 `A`。这个 TCP 连接只是实验库内部用来模拟物理层信道，真正的链路层协议还是由 `datalink.c` 实现。

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
| `a` | 启动站点 A。 |
| `b` | 启动站点 B。 |
| `-u` / `--utopia` | 无误码信道，误码率为 0。 |
| `-f` / `--flood` | 网络层以洪水式速度产生分组。 |
| `-b <rate>` / `--ber=<rate>` | 设置误码率，例如 `-b 1e-4`。 |
| `-t <seconds>` / `--ttl=<seconds>` | 运行指定秒数后自动退出。 |
| `-d <mask>` / `--debug=<mask>` | 打开调试输出。一般 `-d3` 最常用。 |
| `-p <port>` / `--port=<port>` | 指定 TCP 端口，端口冲突时可改。 |
| `-l <file>` / `--log=<file>` | 指定日志文件名。 |
| `-n` / `--nolog` | 不生成日志文件。 |
| `-i` / `--ibib` | 改变站点 B 的网络层发包节奏。一般前期不用。 |
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

## 当前 GBN 的检查顺序

每次改完 `datalink.c`，建议按这个顺序回归测试：

1. `-u -d3 -t 30`，看帧级行为是否正确。
2. `-u -t 120`，看无误码稳定性。
3. `-t 120`，看默认误码下的恢复能力。
4. `-f -t 120`，看 flood 压力。
5. `-f -b 1e-4 -t 120`，看高误码恢复。
6. `-f -t 900`，看长时间稳定运行。

当前基础 GBN 应该满足的行为是：

| 场景 | 预期行为 |
| --- | --- |
| 按序收到 DATA | 交付分组，推进 `frame_expected`，延迟发送 ACK。 |
| 收到乱序或重复 DATA | 不交付，发送当前最后按序帧的 ACK。 |
| DATA 超时 | 从 `ack_expected` 开始重传整个窗口。 |
| ACK 超时 | 如果之前的 ACK 没有被捎带出去，就单独发 ACK。 |

