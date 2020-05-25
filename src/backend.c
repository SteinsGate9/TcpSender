#include "backend.h"

/*
 * [0 ..confirmed... LAR-1] [LAR..sent not acked.. FAK-1] [FAK..both.. LFS-1] [LFS ..not sent.. LAR+SWS-1]*/
void append_to_sendqueue(cmu_socket_t * sock, char* data, int buf_len) {
  char* msg;
  char* data_offset = data;
  int sockfd, plen;
  size_t conn_len = sizeof(sock->conn);
  uint32_t seq;

  sockfd = sock->socket;
  if(buf_len > 0){
    while(buf_len != 0){ /* if have stuff to send */
      /* flow control */
      seq = sock->window.LFS;
      if (seq - sock->window.LAR >= WINDOW_SIZE ){
        return;
      }

      /* add maximum MAX_DLEN context */
      if(buf_len <= MAX_DLEN){ /* [  MAX_DLEN  MAX_DLEN MAX_DLEN ] buf_len */
        plen = DEFAULT_HEADER_LEN + buf_len; /* send headerlen + buf_len */

        msg = create_packet_buf(sock->my_port, sock->their_port, seq, -1,  /* create buf */
                                DEFAULT_HEADER_LEN, plen, NO_FLAG, 1, 0, NULL, data_offset, buf_len);

        buf_len = 0;
      }
      else{
        plen = DEFAULT_HEADER_LEN + MAX_DLEN;

        msg = create_packet_buf(sock->my_port, sock->their_port, seq, -1,
                                DEFAULT_HEADER_LEN, plen, NO_FLAG, 1, 0, NULL, data_offset, MAX_DLEN);

        buf_len -= MAX_DLEN;
      }

      /* save to buffer & todo: set timer */
      sock->window.sendQ[seq%SWS].msg = (char *) malloc(plen-DEFAULT_HEADER_LEN);
      memcpy(sock->window.sendQ[seq%SWS].msg, data_offset, plen-DEFAULT_HEADER_LEN);
//      set_alarm();

      /* try to send */
      sendto(sockfd, msg, plen, 0, (struct sockaddr*) &(sock->conn), conn_len);
#ifdef DEBUG_VERBOSE
      fprintf(stdout, "senddatato: %d:%d seq:%d,ack:%d plen:%d\n", sock->conn.sin_addr.s_addr, sock->conn.sin_port, seq, -1, plen);
        fflush(stdout);
#endif

      /* update */
      data_offset = data_offset + plen - DEFAULT_HEADER_LEN;
      sock->window.LFS ++;

    }
  }
}





/*
 * Param: sock - The socket to check for acknowledgements.
 * Param: seq - Sequence number to check
 *
 * Purpose: To tell if a packet (sequence number) has been acknowledged.
 *
 */
int check_ack(cmu_socket_t * sock, uint32_t seq){
  int result;

  while(pthread_mutex_lock(&(sock->window.ack_lock)) != 0);

  if(sock->window.last_ack_received > seq)
    result = TRUE;
  else
    result = FALSE;

  pthread_mutex_unlock(&(sock->window.ack_lock));
  return result;
}

/*
 * Param: sock - The socket used for handling packets received
 * Param: pkt - The packet data received by the socket
 *
 * Purpose: Updates the socket information to represent
 *  the newly received packet.
 *
 * Comment: This will need to be updated for checkpoints 1,2,3
 *
 */
/* handle recved data packet
 * 1) if it is ACK packet, then update ack_last_received
 * 2) else send seq, seq+1 back,
 *  2.1) if seq > last receive or first round, then update last_seq_received & copy back to buffer.
 *  2.2) else ignore.
 */
void handle_message(cmu_socket_t * sock, char* pkt){
  char* rsp;
  uint8_t flags = get_flags(pkt);
  uint32_t data_len, seq;
  socklen_t conn_len = sizeof(sock->conn);
  uint32_t ack;

  switch(flags){ /* [ recved ack ][LAR.(not recved ack) LFS-1][LFS ..not sent.. LAR+SWS-1] */
    case ACK_FLAG_MASK: /* if this pkt is only ACK */
#ifndef SLIDING_WINDOW
      if(get_ack(pkt) > sock->window.last_ack_received)
        sock->window.last_ack_received = get_ack(pkt);
#endif
#ifdef SLIDING_WINDOW
      ack = get_ack(pkt);
      sock->window.sendQ[ack%RWS].acked = 1;
      if (ack == sock->window.LAR) {
        uint32_t i;
        for (i=ack; i <= RWS+ack && sock->window.sendQ[i%RWS].acked == 1; i++){
          sock->window.sendQ[i%RWS].acked = 0;
          free(sock->window.sendQ[i%RWS].msg);
          /* todo:free timer  */
        }
        sock->window.NFE = i%RWS;

        /* congestion control */
        sock->window += MSS;
        if (sock->window.state == SS) {
          if (sock->window >= threshold) {
            sock->window.state = CA;
          }
        }
      }
      else {
        /* congestion control */
        if (++sock->window.dup == 3){
          sock->window.state = SS;
          thres = sock->window.congwin / 2;
          sock->window.congwin = thres;
        }
      }






#endif
      break;

    default:
#ifndef SLIDING_WINDOW
      /* get seq */
      seq = get_seq(pkt);

      /* send header: seq, seq+1 */
      rsp = create_packet_buf(sock->my_port, ntohs(sock->conn.sin_port), -1, seq + get_plen(pkt),
        DEFAULT_HEADER_LEN, DEFAULT_HEADER_LEN, ACK_FLAG_MASK, 1, 0, NULL, NULL, 0);

      sendto(sock->socket, rsp, DEFAULT_HEADER_LEN, 0, (struct sockaddr*)&(sock->conn), conn_len);
#ifdef DEBUG_VERBOSE
      fprintf(stdout, "sendackto: %d:%d seq:%d,ack:%d\n",sock->conn.sin_addr.s_addr, sock->conn.sin_port, -1, seq+1);
      fflush(stdout);
#endif
      free(rsp);

      /* if seq > seq or first one */
      if(seq > sock->window.last_seq_received || (seq == 0 && sock->window.last_seq_received == 0)){

        /* update window seq */
        sock->window.last_seq_received = seq;

        /* copy to recv buffer */
        data_len = get_plen(pkt) - DEFAULT_HEADER_LEN;
        if(sock->received_buf == NULL){
          sock->received_buf = malloc(data_len);
        }
        else{
          sock->received_buf = realloc(sock->received_buf, sock->received_len + data_len);
        }
        memcpy(sock->received_buf + sock->received_len, pkt + DEFAULT_HEADER_LEN, data_len);
        sock->received_len += data_len;
      }
#endif
#ifdef SLIDING_WINDOW
      seq = get_seq(pkt);
      /* flow control */
      if (seq - sock->window.NFE >= WINDOW_SIZE){
        return;
      }

      /* buffer */
//      if (seq <= sock->window.NFE-1 && seq >= sock->window.NFE-WINDOW_SIZE){ /*   */
//        ;
//      }
      if (seq >= sock->window.NFE && seq <= sock->window.NFE+WINDOW_SIZE-1){ /* save & send ack */
        if (sock->window.recvQ[seq%RWS].received != 1){
          sock->window.recvQ[seq%RWS].msg = (char*) malloc(get_plen(pkt));
          memcpy(sock->window.recvQ[seq%RWS].msg, pkt, get_plen(pkt));
          sock->window.recvQ[seq%RWS].received = 1;
        }
        if (seq == sock->window.NFE){
          uint8_t i;
          for (i=seq; i <= RWS+seq && sock->window.recvQ[i%RWS].received == 1; i++){
            sock->window.recvQ[i%RWS].received = 0;
            free(sock->window.recvQ[i%RWS].msg);
            /* todo : put in buffer */
          }
          sock->window.NFE = i%RWS;
        }
        if (seq == sock->window.RFS)
          sock->window.RFS++;
      }
      rsp = create_packet_buf(sock->my_port, ntohs(sock->conn.sin_port), -1, seq,
                              DEFAULT_HEADER_LEN, DEFAULT_HEADER_LEN, ACK_FLAG_MASK, RWS-, 0, NULL, NULL, 0);

      sendto(sock->socket, rsp, DEFAULT_HEADER_LEN, 0, (struct sockaddr*)&(sock->conn), conn_len);
#ifdef DEBUG_VERBOSE
    fprintf(stdout, "sendackto: %d:%d seq:%d,ack:%d\n",sock->conn.sin_addr.s_addr, sock->conn.sin_port, -1, seq+1);
      fflush(stdout);
#endif
      free(rsp);


#endif
      break;
  }
}

/*
 * Param: sock - The socket used for receiving data on the connection.
 * Param: flags - Signify different checks for checking on received data.
 *  These checks involve no-wait, wait, and timeout.
 *
 * Purpose: To check for data received by the socket.
 * 1) recvfrom header from other end
 * 2) recv full packet from other end
 * 3) handle message
 *
 */
void check_for_data(cmu_socket_t * sock, int flags){
  char hdr[DEFAULT_HEADER_LEN];
  char* pkt;
  socklen_t conn_len = sizeof(sock->conn);
  ssize_t len = 0;
  uint32_t plen = 0, buf_size = 0, n = 0;
  fd_set ackFD;
  struct timeval time_out;
  time_out.tv_sec = 3;
  time_out.tv_usec = 0;

  /* recv a meesage */
  while(pthread_mutex_lock(&(sock->recv_lock)) != 0);
  switch(flags){
    case NO_FLAG: /* recv header */
      len = recvfrom(sock->socket, hdr, DEFAULT_HEADER_LEN, MSG_PEEK,
                (struct sockaddr *) &(sock->conn), &conn_len);
      break;

    case TIMEOUT:
      FD_ZERO(&ackFD);
      FD_SET(sock->socket, &ackFD);
      if(select(sock->socket+1, &ackFD, NULL, NULL, &time_out) <= 0){
        break;
      }

    case NO_WAIT:
      len = recvfrom(sock->socket, hdr, DEFAULT_HEADER_LEN, MSG_DONTWAIT | MSG_PEEK,
               (struct sockaddr *) &(sock->conn), &conn_len);
      break;

    default:
      perror("ERROR unknown flag");
  }
  /* the reason to us MSG_PEEK is that we need plen to fully receive the packet,
   * Therefore we recv header first then the full body..*/
  if(len >= DEFAULT_HEADER_LEN){
    /* get plen out of put */
    plen = get_plen(hdr);
    pkt = malloc(plen);

    while(buf_size < plen ){ /* receive full p_len length */
        n = recvfrom(sock->socket, pkt + buf_size, plen - buf_size, NO_FLAG, (struct sockaddr *) &(sock->conn), &conn_len);
      buf_size = buf_size + n;
    }
#ifdef DEBUG_VERBOSE
    int seq = get_seq(pkt);
    int ack = get_ack(pkt);
    int len_ = get_plen(pkt);
    fprintf(stdout, "recvfull: %d:%d seq:%d,ack:%d,len:%d\n",sock->conn.sin_addr.s_addr, sock->conn.sin_port, seq, ack, len_);
    fflush(stdout);
#endif

    /* handle received full length packet*/
    handle_message(sock, pkt);
    free(pkt);
  }
  pthread_mutex_unlock(&(sock->recv_lock));
}

/*
 * Param: sock - The socket to use for sending data
 * Param: data - The data to be sent
 * Param: buf_len - the length of the data being sent
 *
 * Purpose: Breaks up the data into packets and sends a single
 *  packet at a time.
 *
 * Comment: This will need to be updated for checkpoints 1,2,3
 *
 */
/* loop until buf_len runs -> 0
 *  1) create header+part of data
 *  2) repeatedly send data until recv a ACK > seq */
void single_send(cmu_socket_t * sock, char* data, int buf_len){
    char* msg;
    char* data_offset = data;
    int sockfd, plen;
    size_t conn_len = sizeof(sock->conn);
    uint32_t seq;

    sockfd = sock->socket;
    if(buf_len > 0){
      while(buf_len != 0){ /* if have stuff to send */
        seq = sock->window.last_ack_received;

        if(buf_len <= MAX_DLEN){ /* [  MAX_DLEN  MAX_DLEN MAX_DLEN ] buf_len */
            plen = DEFAULT_HEADER_LEN + buf_len; /* send headerlen + buf_len */

            msg = create_packet_buf(sock->my_port, sock->their_port, seq, -1,  /* create buf */
              DEFAULT_HEADER_LEN, plen, NO_FLAG, 1, 0, NULL, data_offset, buf_len);

            buf_len = 0;
          }
        else{
            plen = DEFAULT_HEADER_LEN + MAX_DLEN;

            msg = create_packet_buf(sock->my_port, sock->their_port, seq, -1,
              DEFAULT_HEADER_LEN, plen, NO_FLAG, 1, 0, NULL, data_offset, MAX_DLEN);

            buf_len -= MAX_DLEN;
          }
        while(TRUE){ /* continuously send data until receive ACK > seq which means received */
          /* send all data */
          sendto(sockfd, msg, plen, 0, (struct sockaddr*) &(sock->conn), conn_len);

#ifdef DEBUG_VERBOSE
          fprintf(stdout, "senddatato: %d:%d seq:%d,ack:%d plen:%d\n", sock->conn.sin_addr.s_addr, sock->conn.sin_port, seq, -1, plen);
          fflush(stdout);
#endif

          /* check until receive feed back*/
          check_for_data(sock, TIMEOUT); /* we need to up date last_ack_received in this function */
          if (check_ack(sock, seq)) /* check if last_ack_recv > seq */
            break;

        }
        data_offset = data_offset + plen - DEFAULT_HEADER_LEN;
      }
    }
}

/*
 * Param: in - the socket that is used for backend processing
 *
 * Purpose: To poll in the background for sending and receiving data to
 *  the other side.
 *
 */
void* begin_backend(void * in){
  cmu_socket_t * dst = (cmu_socket_t *) in;
  int death, buf_len, send_signal;
  char* data;

  while(TRUE){
    /* check terminal status */
    while(pthread_mutex_lock(&(dst->death_lock)) !=  0); /* wait on death_lock */
    death = dst->dying;
    pthread_mutex_unlock(&(dst->death_lock));
    while(pthread_mutex_lock(&(dst->send_lock)) != 0); /* wait on send lock */
    buf_len = dst->sending_len;
    if(death && buf_len == 0)
      break;

    /* check send buffer: append to send queue */
#ifndef SLIDING_WINDOW
    if(buf_len > 0){
      data = malloc(buf_len);
      memcpy(data, dst->sending_buf, buf_len);
      dst->sending_len = 0;
      free(dst->sending_buf);
      dst->sending_buf = NULL;
      pthread_mutex_unlock(&(dst->send_lock));
      single_send(dst, data, buf_len);
      free(data);
    }
#endif
#ifdef SLIDING_WINDOW
    if(buf_len > 0){
      data = malloc(buf_len);
      memcpy(data, dst->sending_buf, buf_len);
      dst->sending_len = 0;
      free(dst->sending_buf);
      dst->sending_buf = NULL;
      append_to_sendqueue(dst, data, buf_len);
      free(data);
    }
#endif
    pthread_mutex_unlock(&(dst->send_lock));

    /* handle recved message one at a time */
    check_for_data(dst, TIMEOUT);

    /* check recved buffer: check recved queue & add to buffer */
#ifdef SLIDING_WINDOW

#endif
    while(pthread_mutex_lock(&(dst->recv_lock)) != 0);
    if(dst->received_len > 0)
      send_signal = TRUE;
    else
      send_signal = FALSE;
    pthread_mutex_unlock(&(dst->recv_lock));
    if(send_signal){
      pthread_cond_signal(&(dst->wait_cond));
    }
  }

  /* register every RTT signal state */
  if (dst->window.state == SS){
    /*todo: congwinx2*/
  }
  else {
    /*todo: congwin += MSS*/
  }
  /* set estimate RTT & reset timer */



  /* register exceed timer */
  if (){
    thres = congwin/2;
    congwin = thres;
    dst->window.state = SS;
  }



  pthread_exit(NULL);
  return NULL;
}
