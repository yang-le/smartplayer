#ifndef __PKTQ_H___
#define __PKTQ_H___

#include <libavformat/avformat.h>
#include <SDL2/SDL_mutex.h>

typedef struct PacketQueue {
	AVPacketList *first, *last;
	int nb_packets;
	int size;
	SDL_mutex *mutex;
	SDL_cond *cond;
} PacketQueue;

#define PACKET_QUEUE_INITIALIZER {NULL, NULL, 0, 0, NULL, NULL}

void packet_queue_init(PacketQueue *q);
int packet_queue_put(PacketQueue *q, const AVPacket *_pkt);
int packet_queue_get(PacketQueue *q, AVPacket *pkt);

#endif