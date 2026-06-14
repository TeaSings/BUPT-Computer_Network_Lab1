# 2026 年春计算机网络 Lab1

本仓库是 2026 年春季计算机网络 Lab1 的 Linux 版本实现，内容围绕实验提供的数据链路层模拟环境，完成可靠传输协议的设计与实现。

## 内容

| 目标 | 源文件 | 说明 |
| --- | --- | --- |
| `stopwait` | `protocols/stopwait.c` | 停等协议 / 交替位协议。 |
| `gbn` | `protocols/gbn.c` | Go-Back-N，使用累计 ACK 和超时回退重传。 |
| `sr` | `protocols/sr.c` | Selective Repeat，支持单帧定时器和接收端乱序缓存。 |
| `sr_opt` | `protocols/sr_opt.c` | 优化版 Selective Repeat，使用短 ACK/NAK 帧和环形缓存。 |

实验框架文件包括 `protocol.c`、`protocol.h`、`datalink.h`、`lprintf.c` 和 `crc32.c`。`Makefile` 用于选择具体协议源码并编译生成 `datalink` 可执行文件。

## 编译

```bash
make sr
make sr_opt
make gbn
make stopwait
```

默认执行 `make` 会编译 `sr`。切换协议目标前可先运行 `make clean`，避免旧的 `datalink` 可执行文件影响测试。

## 运行

在仓库目录下打开两个终端，先启动站点 A，再启动站点 B：

```bash
./datalink -f -u -n -t 650 a
./datalink -f -u -n -t 650 b
```

常用参数：

| 参数 | 含义 |
| --- | --- |
| `a` / `b` | 启动站点 A 或 B。 |
| `-u` | 使用无误码信道。 |
| `-f` | 启用 flood 模式。 |
| `-b <rate>` | 设置误码率，例如 `-b 1e-4`。 |
| `-t <seconds>` | 设置运行时间。 |
| `-n` | 不生成日志文件。 |
| `-d <mask>` | 启用调试输出。 |
| `-p <port>` | 默认端口被占用时指定其他端口。 |

## 说明

本仓库仅保留实验实现相关的源码、构建文件和少量参考可执行文件。实验报告、评分表、生成日志和本地编译产物不纳入版本管理。
