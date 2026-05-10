#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "datalink.h"

#define MAX_SEQ 63
#define WINDOW_SIZE 32 // 由于是选择重传，故增大窗口大小
#define DATA_TIMER 2000
#define ACK_TIMER 300

struct FRAME {
	unsigned char kind; /* FRAME_DATA */
	unsigned char ack;
	unsigned char seq;
	unsigned char data[PKT_LEN];
	unsigned int padding;
};

static unsigned char ack_expected = 0; // 最早还没被确认的帧，也就是发送窗口的左闭区间
static unsigned char next_frame_to_send = 0; // 下一个要发送的序号，也就是发送窗口的右开区间
static unsigned char frame_expected = 0; // 接收方期待的数据帧序号，也就是接收窗口的左闭区间
static unsigned char too_far = WINDOW_SIZE; // 接收窗口的右开边界
static unsigned char nbuffered = 0; // 当前发送窗口中未确认的帧的数量
static unsigned char nak_sent = 0; // NAK 是否发送标志
static int phl_ready = 0; // 物理层就绪标志

static unsigned char out_buff[MAX_SEQ + 1][PKT_LEN];
static unsigned char arrived[MAX_SEQ + 1];
static unsigned char in_buff[MAX_SEQ + 1][PKT_LEN];

/*
@brief 此函数用于判断 b 是否在环形序号区间 [a, c) 当中
*/
static int between(unsigned char a, unsigned char b, unsigned char c) {
	return ((a <= b) && (b < c)) ||
        ((c < a) && (a <= b)) ||
        ((b < c) && (c < a));
}

static void put_frame(unsigned char *frame, int len) {
	*(unsigned int *)(frame + len) = crc32(frame, len);
	send_frame(frame, len + 4);
	phl_ready = 0;
}

/*
@brief 此函数用于发送 seq 为 frame_nr 的 frame
*/
static void send_data_frame(unsigned char frame_nr) {
	struct FRAME s;

	s.kind = FRAME_DATA;
	s.seq = frame_nr;
	s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);
	memcpy(s.data, out_buff[frame_nr], PKT_LEN);

	dbg_frame("Send DATA %d %d, ID %d\n", s.seq, s.ack, *(short *)s.data);
	stop_ack_timer();
	put_frame((unsigned char *)&s, 3 + PKT_LEN);
	start_timer(frame_nr, DATA_TIMER);
}

static void send_ack_frame(void) {
	struct FRAME s;

	s.kind = FRAME_ACK;
	s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);

	dbg_frame("Send ACK  %d\n", s.ack);
	stop_ack_timer();
	put_frame((unsigned char *)&s, 2);
}

/*
@brief 此函数仿照 send_ack_frame 编写，用于发送NAK
*/
static void send_nak_frame(void) {
	struct FRAME s;
	s.kind = FRAME_NAK;
	s.ack = frame_expected;

	dbg_frame("Send NAK  %d\n", s.ack);
	put_frame((unsigned char *)& s, 2);
}

/*
@brief 这个函数用于处理 Cumulative ACK
*/
static void handle_ack(unsigned char curr_ack) {
	while (between(ack_expected, curr_ack, next_frame_to_send)) { // 依据收到的 ACK 调整发送窗口的左边界
		nbuffered--;
		stop_timer(ack_expected);
		ack_expected = (ack_expected + 1) % (MAX_SEQ + 1);
	}
}

/*
@brief 这个函数用于处理收到 nak 时候的重传操作
*/
static void handle_nak(unsigned char curr_nak) {
	if (between(ack_expected, curr_nak, next_frame_to_send)) { // 判断当前 NAK 是否过期，如果没有过期则重传自 NAK 以来的所有帧
		send_data_frame(curr_nak);
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
		case NETWORK_LAYER_READY:
			get_packet(out_buff[next_frame_to_send]);
			nbuffered++;
			send_data_frame(next_frame_to_send);
			next_frame_to_send = (next_frame_to_send + 1) % (MAX_SEQ + 1); // 这里采用模除回滚帧序号
			break;

		case PHYSICAL_LAYER_READY:
			phl_ready = 1;
			break;

		case FRAME_RECEIVED:
			len = recv_frame((unsigned char *)&f, sizeof f);
			if (len < 5 || crc32((unsigned char *)&f, len) != 0) {
				dbg_event("**** Receiver Error, Bad CRC Checksum\n");
				if (nak_sent == 0) { // 依据有无发送过 NAK 来决定是否要发送，避免对于同一个序号的错帧反复刷屏 NAK
					send_nak_frame();
					nak_sent = 1;
				}
				break;
			}
			if (f.kind == FRAME_ACK) {
				handle_ack(f.ack);
				dbg_frame("Recv ACK  %d\n", f.ack);
			}
			if (f.kind == FRAME_DATA) { // GoBackN 允许 Piggybacking 因此在正常的 FRAME_DATA 当中也要处理ACK
				dbg_frame("Recv DATA %d %d, ID %d\n", f.seq,
					  f.ack, *(short *)f.data);
				handle_ack(f.ack);
				if (between(frame_expected, f.seq, too_far)) { // SR 需要缓存乱序到来的帧，这里使用 between 判断是不是在接收窗口内
					if (arrived[f.seq] == 0) {
						arrived[f.seq] = 1;
						memcpy(in_buff[f.seq], f.data, PKT_LEN);
					}
					if (f.seq == frame_expected) { // 如果接收到的是 frame_expected 指向的帧，那么就开始交付缓存的帧
						while (arrived[frame_expected]) {
							put_packet(in_buff[frame_expected], len - 7);
							arrived[frame_expected] = 0;
							frame_expected = (frame_expected + 1) % (MAX_SEQ + 1);
							too_far = (too_far + 1) % (MAX_SEQ + 1); // 接收窗口左右边界同时更新
							nak_sent = 0;
							start_ack_timer(ACK_TIMER); // Piggybacking 记得打开计时器
						}
						
						if (nak_sent == 0) {
							unsigned char next = (frame_expected + 1) % (MAX_SEQ + 1);
							while (next != too_far) {
								if (arrived[next]) {
									send_nak_frame();
									nak_sent = 1;
									break;
								}
								next = (next + 1) % (MAX_SEQ + 1);
							}
						}
					} else { // 如果来的不是 frame_expected 指向的帧，那么就看看是否需要发 NAK
						if (nak_sent == 0) {
							nak_sent = 1;
							send_nak_frame();
						}
					}
				} else { // 如果是旧的重复帧，发送 ACK
					send_ack_frame();
				}
			}
			if (f.kind == FRAME_NAK) {
				dbg_frame("Recv NAK  %d\n", f.ack);
				handle_nak(f.ack);
			}
			break;

		case DATA_TIMEOUT: // SR 只发送对应 TIMEOUT 的帧
			dbg_event("---- DATA %d timeout\n", arg);
			if (between(ack_expected, arg, next_frame_to_send)) {
				send_data_frame(arg);
			}
			break;

		case ACK_TIMEOUT: // 新增 ack_timer 事件用于进一步优化 Piggybacking
			dbg_event("---- ACK timeout, send standalone ACK\n");
			send_ack_frame();
			break;
		}

		if (nbuffered < WINDOW_SIZE && phl_ready) // 发送窗口还有空位
			enable_network_layer();
		else
			disable_network_layer();
	}
}
