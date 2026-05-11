#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "datalink.h"

/**
 * @brief 优化版 Selective Repeat。
 *
 * DATA 帧仍带 CRC，ACK/NAK 使用 3 字节短控制帧以降低确认开销。
 * 发送和接收缓存按 WINDOW_SIZE 做环形复用，定时器编号也映射到缓存槽位。
 */
#define MAX_SEQ 63
#define WINDOW_SIZE 32
#define NR_BUFS WINDOW_SIZE
#define DATA_TIMER 2500
#define ACK_TIMER 300

struct FRAME {
	unsigned char kind;
	unsigned char ack;
	unsigned char seq;
	unsigned char data[PKT_LEN];
	unsigned int padding;
};

struct BUFFER {
	unsigned char data[PKT_LEN];
	int len;
};

/* 发送窗口为 [ack_expected, next_frame_to_send)，接收窗口为 [frame_expected, too_far)。 */
static unsigned char ack_expected = 0;
static unsigned char next_frame_to_send = 0;
static unsigned char frame_expected = 0;
static unsigned char too_far = WINDOW_SIZE;
static unsigned char nbuffered = 0;
static int phl_ready = 0;

/* 缓存槽位按 seq % NR_BUFS 复用，arrived 记录接收槽是否已有乱序帧。 */
static struct BUFFER out_buff[NR_BUFS];
static struct BUFFER in_buff[NR_BUFS];
static unsigned char arrived[NR_BUFS];

/**
 * @brief 判断 b 是否落在环形序号区间 [a, c) 内。
 * @param a 区间左边界，包含。
 * @param b 待判断的序号。
 * @param c 区间右边界，不包含。
 */
static int between(unsigned char a, unsigned char b, unsigned char c) {
	return ((a <= b) && (b < c)) ||
        ((c < a) && (a <= b)) ||
        ((b < c) && (c < a));
}

/**
 * @brief 为 DATA 帧追加 CRC 后交给物理层。
 * @param frame 指向帧头部的缓冲区。
 * @param len 不包含 CRC 的帧长度。
 */
static void put_frame(unsigned char *frame, int len) {
	*(unsigned int *)(frame + len) = crc32(frame, len);
	send_frame(frame, len + 4);
	phl_ready = 0;
}

/**
 * @brief 统一发送 DATA、ACK 和 NAK。
 * @param kind 帧类型，取 FRAME_DATA、FRAME_ACK 或 FRAME_NAK。
 * @param frame_nr DATA 帧要发送的序号；ACK/NAK 调用时不使用。
 * @param frame_expected 本端当前缺失或期待的接收序号。
 */
static void send_frame_datalink(unsigned char kind, unsigned char frame_nr, unsigned char frame_expected) {
	struct FRAME s;
	int index;
	
	s.kind = kind;
	/* ACK 控制含义：确认 frame_expected 前一个已经按序收到的帧。 */
	s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);

	if (kind == FRAME_DATA) {
		index = frame_nr % NR_BUFS;
		s.seq = frame_nr;
		memcpy(s.data, out_buff[index].data, out_buff[index].len);

		dbg_frame("Send Data %d %d, ID %d \n", s.seq, s.ack, *(short *)s.data);
		put_frame((unsigned char *)& s, 3 + out_buff[index].len);
		start_timer(frame_nr % NR_BUFS, DATA_TIMER);
	} else {
		if (kind == FRAME_NAK) {
			/* NAK 控制含义：ack 字段改作当前缺失帧号。 */
			s.ack = frame_expected;
			dbg_frame("Send NAK  %d\n", s.ack);
		} else {
			dbg_frame("Send ACK  %d\n", s.ack);
		}
		s.seq = 0;
		/* 短 ACK/NAK 不带 CRC，长度固定为 kind、ack、seq 三个字节。 */
		send_frame((unsigned char *)&s, 3);
	}

	stop_ack_timer();
}

/**
 * @brief 根据累计确认号滑动发送窗口。
 * @param curr_ack 对端确认的最后一个连续收到的帧号。
 */
static void handle_ack(unsigned char curr_ack) {
	while (between(ack_expected, curr_ack, next_frame_to_send)) {
		nbuffered--;
		/* 定时器编号使用缓存槽位，而不是原始序号。 */
		stop_timer(ack_expected % NR_BUFS);
		ack_expected = (ack_expected + 1) % (MAX_SEQ + 1);
	}
}

int main(int argc, char **argv) {
	int event, arg;
	struct FRAME f;
	int len = 0;

	protocol_init(argc, argv);
	lprintf("Designed by Jiang Yanjun, build: " __DATE__ "  "__TIME__"\n");

	disable_network_layer();

	for (;;) {
		event = wait_for_event(&arg);

		switch (event) {
		case NETWORK_LAYER_READY: {
			int index = next_frame_to_send % NR_BUFS;

			/* 新分组写入发送窗口右端对应的环形缓存槽。 */
			out_buff[index].len = get_packet(out_buff[index].data);
			nbuffered++;
			send_frame_datalink(FRAME_DATA, next_frame_to_send, frame_expected);
			next_frame_to_send = (next_frame_to_send + 1) % (MAX_SEQ + 1);
			break;
		}

		case PHYSICAL_LAYER_READY:
			phl_ready = 1;
			break;

		case FRAME_RECEIVED:
			len = recv_frame((unsigned char *)&f, sizeof f);
			if (len < 5 && len != 3) {
				break;
			}
			if (len >= 5 && crc32((unsigned char *)&f, len) != 0) {
				dbg_event("**** Receiver Error, Bad CRC Checksum\n");
				/* DATA 帧损坏时，直接请求当前窗口左端缺失帧。 */
				send_frame_datalink(FRAME_NAK, 0, frame_expected);
				break;
			}
			if (f.kind == FRAME_ACK) {
				dbg_frame("Recv ACK  %d\n", f.ack);
			}
			if (f.kind == FRAME_DATA) {
				int index;
				dbg_frame("Recv DATA %d %d, ID %d\n", f.seq,
					  f.ack, *(short *)f.data);
				start_ack_timer(ACK_TIMER);
				if (between(frame_expected, f.seq, too_far) && arrived[f.seq % NR_BUFS] == 0) {
					/* 同一个缓存槽复用前必须确认旧内容已经交付并清空。 */
					index = f.seq % NR_BUFS;
					arrived[index] = 1;
					memcpy(in_buff[index].data, f.data, len - 7);
					in_buff[index].len = len - 7; 

					/* 窗口左端连续到达后，立即按序交付并向前滑动接收窗口。 */
					while (arrived[frame_expected % NR_BUFS]) {
						index = frame_expected % NR_BUFS;
						put_packet(in_buff[index].data, in_buff[index].len);
						arrived[index] = 0;

						frame_expected = (frame_expected + 1) % (MAX_SEQ + 1);
						too_far = (too_far + 1) % (MAX_SEQ + 1);
						start_ack_timer(ACK_TIMER);
					}
				}
			}
			/* NAK 的 ack 字段不是累计确认号，必须先重传并跳过 handle_ack。 */
			if (f.kind == FRAME_NAK && between(ack_expected, f.ack, next_frame_to_send)) {
				dbg_frame("Recv NAK  %d\n", f.ack);
				send_frame_datalink(FRAME_DATA, f.ack, frame_expected);
				break;
			}
			handle_ack(f.ack);
			break;

		case DATA_TIMEOUT:
			dbg_event("---- DATA %d timeout\n", arg);
			/* 定时器编号是缓存槽位，超时时需要映射回当前发送窗口中的真实序号。 */
			if (!between(ack_expected, arg, next_frame_to_send))
				arg += NR_BUFS;
			if (between(ack_expected, arg, next_frame_to_send))
				send_frame_datalink(FRAME_DATA, arg, frame_expected);
			break;

		case ACK_TIMEOUT:
			dbg_event("---- ACK timeout, send standalone ACK\n");
			send_frame_datalink(FRAME_ACK, 0, frame_expected);
			break;
		}

		if (nbuffered < WINDOW_SIZE && phl_ready)
			enable_network_layer();
		else
			disable_network_layer();
	}
}
