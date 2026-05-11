#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "datalink.h"

/**
 * @brief Go-Back-N。
 *
 * 接收端只接受按序到达的 DATA 帧；一旦丢失或乱序，发送 NAK/ACK 提示发送端。
 * 发送端超时时从发送窗口左端开始重传整个窗口。
 */
#define MAX_SEQ 63
#define WINDOW_SIZE 32
#define DATA_TIMER 2000
#define ACK_TIMER 300

struct FRAME {
	unsigned char kind;
	unsigned char ack;
	unsigned char seq;
	unsigned char data[PKT_LEN];
	unsigned int padding;
};

/* 发送窗口为 [ack_expected, next_frame_to_send)，接收端只维护下一个期望序号。 */
static unsigned char ack_expected = 0;
static unsigned char next_frame_to_send = 0;
static unsigned char frame_expected = 0;
static unsigned char nbuffered = 0;

/* 同一个缺口只发一次 NAK，避免在连续乱序帧上反复发送控制帧。 */
static unsigned char nak_sent = 0;
static int phl_ready = 0;

static unsigned char out_buff[MAX_SEQ + 1][PKT_LEN];

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
 * @brief 为帧追加 CRC 后交给物理层。
 * @param frame 指向帧头部的缓冲区。
 * @param len 不包含 CRC 的帧长度。
 */
static void put_frame(unsigned char *frame, int len) {
	*(unsigned int *)(frame + len) = crc32(frame, len);
	send_frame(frame, len + 4);
	phl_ready = 0;
}

/**
 * @brief 发送或重传一个 DATA 帧。
 * @param frame_nr 要发送的数据帧序号。
 */
static void send_data_frame(unsigned char frame_nr) {
	struct FRAME s;

	s.kind = FRAME_DATA;
	s.seq = frame_nr;
	/* ACK 确认的是 frame_expected 前一个已经按序收到的帧。 */
	s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);
	memcpy(s.data, out_buff[frame_nr], PKT_LEN);

	dbg_frame("Send DATA %d %d, ID %d\n", s.seq, s.ack, *(short *)s.data);
	stop_ack_timer();
	put_frame((unsigned char *)&s, 3 + PKT_LEN);
	start_timer(frame_nr, DATA_TIMER);
}

/**
 * @brief ACK 定时器到期后，单独发送当前累计确认号。
 */
static void send_ack_frame(void) {
	struct FRAME s;

	s.kind = FRAME_ACK;
	s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);

	dbg_frame("Send ACK  %d\n", s.ack);
	stop_ack_timer();
	put_frame((unsigned char *)&s, 2);
}

/**
 * @brief 请求对端重传当前缺失的 frame_expected。
 */
static void send_nak_frame(void) {
	struct FRAME s;
	s.kind = FRAME_NAK;
	s.ack = frame_expected;

	dbg_frame("Send NAK  %d\n", s.ack);
	put_frame((unsigned char *)& s, 2);
}

/**
 * @brief 根据累计确认号滑动发送窗口。
 * @param curr_ack 对端确认的最后一个连续收到的帧号。
 */
static void handle_ack(unsigned char curr_ack) {
	while (between(ack_expected, curr_ack, next_frame_to_send)) {
		nbuffered--;
		stop_timer(ack_expected);
		ack_expected = (ack_expected + 1) % (MAX_SEQ + 1);
	}
}

/**
 * @brief 处理 NAK，从缺失帧开始回退重传。
 * @param curr_nak 对端当前缺失的帧号。
 */
static void handle_nak(unsigned char curr_nak) {
	if (between(ack_expected, curr_nak, next_frame_to_send)) {
		unsigned char next = curr_nak;
		while (next != next_frame_to_send) {
			send_data_frame(next);
			next = (next + 1) % (MAX_SEQ + 1);
		}
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
			next_frame_to_send = (next_frame_to_send + 1) % (MAX_SEQ + 1);
			break;

		case PHYSICAL_LAYER_READY:
			phl_ready = 1;
			break;

		case FRAME_RECEIVED:
			len = recv_frame((unsigned char *)&f, sizeof f);
			if (len < 5 || crc32((unsigned char *)&f, len) != 0) {
				dbg_event("**** Receiver Error, Bad CRC Checksum\n");
				if (nak_sent == 0) {
					send_nak_frame();
					nak_sent = 1;
				}
				break;
			}
			if (f.kind == FRAME_ACK) {
				handle_ack(f.ack);
				dbg_frame("Recv ACK  %d\n", f.ack);
			}
			if (f.kind == FRAME_DATA) {
				/* DATA 帧也可能捎带 ACK，需要先处理发送窗口。 */
				handle_ack(f.ack);
				dbg_frame("Recv DATA %d %d, ID %d\n", f.seq,
					  f.ack, *(short *)f.data);
				if (f.seq == frame_expected) {
					put_packet(f.data, len - 7);
					frame_expected = (frame_expected + 1) % (MAX_SEQ + 1);
					nak_sent = 0;
					start_ack_timer(ACK_TIMER);
				} else {
					if (nak_sent == 0) {
						send_nak_frame();
						nak_sent = 1;
					} else {
						send_ack_frame();
					}
				}
			}
			if (f.kind == FRAME_NAK) {
				dbg_frame("Recv NAK  %d\n", f.ack);
				handle_nak(f.ack);
			}
			break;

		case DATA_TIMEOUT:
			dbg_event("---- DATA %d timeout\n", arg);
			unsigned char next = ack_expected;
			/* GBN 超时后回退到窗口左端，重传所有未确认帧。 */
			for (int i = 0; i < nbuffered; i++) {
				send_data_frame(next);
				next = (next + 1) % (MAX_SEQ + 1);
			}
			break;

		case ACK_TIMEOUT:
			dbg_event("---- ACK timeout, send standalone ACK\n");
			send_ack_frame();
			break;
		}

		if (nbuffered < WINDOW_SIZE && phl_ready)
			enable_network_layer();
		else
			disable_network_layer();
	}
}
