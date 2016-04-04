#include "pktq.h"

void packet_queue_init(PacketQueue *q)
{
	memset(q, 0, sizeof(PacketQueue));
	q->mutex = SDL_CreateMutex();
	q->cond = SDL_CreateCond();
}

int packet_queue_put(PacketQueue *q, const AVPacket *pkt)
{
	AVPacketList *pkt1 = av_malloc(sizeof(AVPacketList));
	if (!pkt1) {
		return -1;
	}

	memcpy(&pkt1->pkt, pkt, sizeof(AVPacket));
	pkt1->next = NULL;

	SDL_LockMutex(q->mutex);

	if (!q->last)
		q->first = pkt1;
	else
		q->last->next = pkt1;

	q->last = pkt1;
	q->nb_packets++;
	q->size += pkt1->pkt.size;
	
	SDL_CondSignal(q->cond);

	SDL_UnlockMutex(q->mutex);

	return 0;
}

int packet_queue_get(PacketQueue *q, AVPacket *pkt)
{
	int ret = 0;
	AVPacketList *pkt1 = NULL;

	SDL_LockMutex(q->mutex);
	for(;;) {
		pkt1 = q->first;
		if (pkt1) {
			q->first = pkt1->next;

			/* if pkt is the last one, make last NULL so make last safe
			* because we will free it later
			*/
			if (!q->first)
				q->last = NULL;
			
			q->nb_packets--;
			q->size -= pkt1->pkt.size;

			memcpy(pkt, &pkt1->pkt, sizeof(AVPacket));
			
			av_free(pkt1);
			break;
		} else {
			SDL_CondWait(q->cond, q->mutex);
		}
	}
	SDL_UnlockMutex(q->mutex);

	return ret;
}

