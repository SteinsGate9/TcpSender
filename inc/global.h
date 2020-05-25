#include "grading.h"
#ifndef _GLOBAL_H_
#define _GLOBAL_H_
#include <semaphore.h>
#include <pthread.h>

#define EXIT_SUCCESS 0
#define EXIT_ERROR -1
#define EXIT_FAILURE 1

#define SIZE32 4
#define SIZE16 2
#define SIZE8  1

#define NO_FLAG 0
#define NO_WAIT 1
#define TIMEOUT 2

#define TRUE 1
#define FALSE 0

#define SWS 100
#define RWS 100

#define WINDOW_SIZE 10


typedef enum st{
  SS=0,
  CA
}STATE;



typedef struct {


	pthread_mutex_t ack_lock;

#ifdef SLIDING_WINDOW
  /* sender side state: [ recved ack ][LAR.(not recved ack) LFS-1][LFS ..not sent.. LAR+SWS-1] */
    uint32_t last_ack_received; /* really is LAR */
    uint32_t LAR;        /* update when acked */
    uint32_t LFS;        /* update when sent */
    uint8_t flag;
    sem_t sendWindowNotFull;
    struct sendQ_slot {
      time_t timeout; /* event associated with send-timeout */
      uint8_t acked; /* updated acked */
      char* msg;
    } sendQ[SWS];

    /* receiver side state: [0, NFE-1(recved in buffer )][NFE (not recved in buffer), RFS-1][RFS.  NFE+RWS-1]*/
    uint32_t last_seq_received; /* really is NFE */
    uint32_t NFE;       /* update when buffered */
    uint32_t RFS;       /* update when buffered */
    struct recvQ_slot {
      uint8_t received;  /* update when sent/buffered */
      char* msg;
    } recvQ[RWS];

    /* congestion control */
    uint32_t congwin;
    STATE state;

#endif
} window_t;


typedef struct {
	int socket;  /* fd */
	pthread_t thread_id;

	uint16_t my_port; /* this port  */
	uint16_t their_port; /* server port */

	struct sockaddr_in conn; /* send to */

	char* received_buf;
	int received_len;
	pthread_mutex_t recv_lock;
	pthread_cond_t wait_cond;
	char* sending_buf;
	int sending_len;
	int type;
	pthread_mutex_t send_lock;
	int dying;
	pthread_mutex_t death_lock;
	window_t window;
} cmu_socket_t;

#endif