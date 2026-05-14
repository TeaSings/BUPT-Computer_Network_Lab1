#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "datalink.h"

/**
 * @brief Stop-and-Wait / Alternating Bit protocol.
 *
 * 发送窗口大小固定为 1。发送端只有在当前 DATA 帧被确认后，才会从网络层取下一帧。
 * 序号和确认号只在 0、1 之间交替。
 */
#define DATA_TIMER 2000

struct FRAME {
	unsigned char kind;
	unsigned char ack;
	unsigned char seq;
	unsigned char data[PKT_LEN];
	unsigned int padding;
};

static unsigned char frame_nr = 0;
static unsigned char frame_expected = 0;
static unsigned char nbuffered = 0;
static unsigned char buffer[PKT_LEN];
static int phl_ready = 0;

/**
 * @brief 为帧追加 CRC 后交给物理层。
 * @param frame 指向帧头部的缓冲区。
 * @param len 不包含 CRC 的帧长度。
 */
static void put_frame(unsigned char *frame, int len)
{
	*(unsigned int *)(frame + len) = crc32(frame, len);
	send_frame(frame, len + 4);
	phl_ready = 0;
}

/**
 * @brief 发送或重传当前缓存中的 DATA 帧。
 */
static void send_data_frame(void)
{
	struct FRAME s;

	s.kind = FRAME_DATA;
	s.seq = frame_nr;
	/* ACK 确认的是 frame_expected 前一个已经按序收到的帧。 */
	s.ack = 1 - frame_expected;
	memcpy(s.data, buffer, PKT_LEN);

	dbg_frame("Send DATA %d %d, ID %d\n", s.seq, s.ack, *(short *)s.data);

	put_frame((unsigned char *)&s, 3 + PKT_LEN);
	start_timer(frame_nr, DATA_TIMER);
}

/**
 * @brief 单独确认当前已经按序收到的最后一个 DATA 帧。
 */
static void send_ack_frame(void)
{
	struct FRAME s;

	s.kind = FRAME_ACK;
	s.ack = 1 - frame_expected;

	dbg_frame("Send ACK  %d\n", s.ack);

	put_frame((unsigned char *)&s, 2);
}

int main(int argc, char **argv)
{
	int event, arg;
	struct FRAME f;
	int len = 0;

	protocol_init(argc, argv);
	lprintf("Stop-and-Wait Protocol, build: " __DATE__ "  " __TIME__ "\n");

	disable_network_layer();

	for (;;) {
		event = wait_for_event(&arg);

		switch (event) {
		case NETWORK_LAYER_READY:
			get_packet(buffer);
			nbuffered++;
			send_data_frame();
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
				dbg_frame("Recv ACK  %d\n", f.ack);
			}
			if (f.kind == FRAME_DATA) {
				dbg_frame("Recv DATA %d %d, ID %d\n", f.seq,
					  f.ack, *(short *)f.data);
				if (f.seq == frame_expected) {
					put_packet(f.data, len - 7);
					frame_expected = 1 - frame_expected;
				}
				send_ack_frame();
			}
			/* DATA 帧中的 ack 字段也可捎带确认，因此所有有效帧都检查 ACK。 */
			if (nbuffered > 0 && f.ack == frame_nr) {
				stop_timer(frame_nr);
				nbuffered--;
				frame_nr = 1 - frame_nr;
			}
			break;

		case DATA_TIMEOUT:
			dbg_event("---- DATA %d timeout\n", arg);
			send_data_frame();
			break;
		}

		if (nbuffered < 1 && phl_ready)
			enable_network_layer();
		else
			disable_network_layer();
	}
}
