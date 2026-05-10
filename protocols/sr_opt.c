#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "datalink.h"

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

static unsigned char ack_expected = 0;
static unsigned char next_frame_to_send = 0;
static unsigned char frame_expected = 0;
static unsigned char too_far = WINDOW_SIZE;
static unsigned char nbuffered = 0;
static int phl_ready = 0;

static struct BUFFER out_buff[NR_BUFS];
static struct BUFFER in_buff[NR_BUFS];
static unsigned char arrived[NR_BUFS];

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

/* DATA frames keep CRC; ACK/NAK are short control frames like the reference optimized SR. */
static void send_frame_datalink(unsigned char kind, unsigned char frame_nr, unsigned char frame_expected) {
	struct FRAME s;
	int index;
	
	s.kind = kind;
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
			s.ack = frame_expected;
			dbg_frame("Send NAK  %d\n", s.ack);
		} else {
			dbg_frame("Send ACK  %d\n", s.ack);
		}
		s.seq = 0;
		send_frame((unsigned char *)&s, 3);
	}

	stop_ack_timer();
}

/* Cumulative ACK slides the send window and releases the matching timer slots. */
static void handle_ack(unsigned char curr_ack) {
	while (between(ack_expected, curr_ack, next_frame_to_send)) {
		nbuffered--;
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
					index = f.seq % NR_BUFS;
					arrived[index] = 1;
					memcpy(in_buff[index].data, f.data, len - 7);
					in_buff[index].len = len - 7; 

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
			if (f.kind == FRAME_NAK && between(ack_expected, f.ack, next_frame_to_send)) {
				dbg_frame("Recv NAK  %d\n", f.ack);
				send_frame_datalink(FRAME_DATA, f.ack, frame_expected);
				break;
			}
			handle_ack(f.ack);
			break;

		case DATA_TIMEOUT:
			dbg_event("---- DATA %d timeout\n", arg);
			/* Timer ids are modulo NR_BUFS; map the slot back into the current send window. */
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
