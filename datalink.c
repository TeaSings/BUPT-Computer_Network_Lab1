#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "datalink.h"

#define MAX_SEQ 63
#define WINDOW_SIZE 4
#define DATA_TIMER 2000

struct FRAME {
	unsigned char kind; /* FRAME_DATA */
	unsigned char ack;
	unsigned char seq;
	unsigned char data[PKT_LEN];
	unsigned int padding;
};

static unsigned char ack_expected = 0; // 最早还没被确认的帧，也就是发送窗口的左闭区间
static unsigned char next_frame_to_send = 0; // 下一个要发送的序号，也就是发送窗口的右开区间
static unsigned char frame_expected = 0; // 接收方期待的数据帧序号
static unsigned char nbuffered = 0;// 当前发送窗口中未确认的帧的数量
static int phl_ready = 0;// 物理层就绪标志

static unsigned char out_buff[MAX_SEQ + 1][PKT_LEN];

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

static void send_data_frame(unsigned char frame_nr) {
	struct FRAME s;

	s.kind = FRAME_DATA;
	s.seq = frame_nr;
	s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);
	memcpy(s.data, out_buff[frame_nr], PKT_LEN);

	dbg_frame("Send DATA %d %d, ID %d\n", s.seq, s.ack, *(short *)s.data);

	put_frame((unsigned char *)&s, 3 + PKT_LEN);
	start_timer(frame_nr, DATA_TIMER);
}

static void send_ack_frame(void) {
	struct FRAME s;

	s.kind = FRAME_ACK;
	s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);

	dbg_frame("Send ACK  %d\n", s.ack);

	put_frame((unsigned char *)&s, 2);
}

/*
@brief 这个函数用于处理 Cumulative ACK
*/
static void handle_ack(unsigned char curr_ack) {
	while (between(ack_expected, curr_ack, next_frame_to_send)) {
		nbuffered--;
		stop_timer(ack_expected);
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
				break;
			}
			if (f.kind == FRAME_ACK) {
				handle_ack(f.ack);
				dbg_frame("Recv ACK  %d\n", f.ack);
			}
			if (f.kind == FRAME_DATA) { // GoBackN 允许 Piggybacking 因此在正常的 FRAME_DATA 当中也要处理ACK
				handle_ack(f.ack);
				dbg_frame("Recv DATA %d %d, ID %d\n", f.seq,
					  f.ack, *(short *)f.data);
				if (f.seq == frame_expected) {
					put_packet(f.data, len - 7);
					frame_expected = (frame_expected + 1) % (MAX_SEQ + 1);
				}
				send_ack_frame();
			}
			break;

		case DATA_TIMEOUT: // 一次性重发窗口内所有帧
			dbg_event("---- DATA %d timeout\n", arg);
			unsigned char next = ack_expected;
			for (int i = 0; i < nbuffered; i++) {
				send_data_frame(next);
				next = (next + 1) % (MAX_SEQ + 1);
			}
			break;
		}

		if (nbuffered < WINDOW_SIZE && phl_ready) // 发送窗口还有空位
			enable_network_layer();
		else
			disable_network_layer();
	}
}