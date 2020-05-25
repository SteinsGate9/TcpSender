#include "cmu_tcp.h"

/*
 * Param: dst - The structure where socket information will be stored
 * Param: flag - A flag indicating the type of socket(Listener / Initiator)
 * Param: port - The port to either connect to, or bind to. (Based on flag)
 * Param: ServerIP - The server IP to connect to if the socket is an initiator.
 *
 * Purpose: To construct a socket that will be used in various connections.
 *  The initiator socket can be used to connect to a listener socket.
 *
 * Return: The newly created socket will be stored in the dst parameter,
 *  and the value returned will provide error information. 
 *
 */
int cmu_socket(cmu_socket_t * dst, int flag, int port, char * serverIP){
  int sockfd, optval;
  socklen_t len;
  struct sockaddr_in conn, my_addr;
  len = sizeof(my_addr);

  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0){
    perror("ERROR opening socket");
    return EXIT_ERROR;
  }
  dst->their_port = port; /* server port */
  dst->socket = sockfd; /* my sockfd */
  dst->received_buf = NULL;
  dst->received_len = 0;

  pthread_mutex_init(&(dst->recv_lock), NULL);
  dst->sending_buf = NULL;
  dst->sending_len = 0;
  pthread_mutex_init(&(dst->send_lock), NULL);

  dst->type = flag;
  dst->dying = FALSE;
  pthread_mutex_init(&(dst->death_lock), NULL);

  dst->window.last_ack_received = flag == TCP_LISTENER? 100 % INT32_MAX: 200 ;
  dst->window.last_seq_received = 0;
  pthread_mutex_init(&(dst->window.ack_lock), NULL);
#ifdef SLIDING_WINDOW
  dst->window.LAR = 0;
  dst->window.LFS = 0;
  sem_init(&dst->window.sendWindowNotFull, 0, 1);
  dst->window.NFE = 0;
#endif

  if(pthread_cond_init(&dst->wait_cond, NULL) != 0){
    perror("ERROR condition variable not set\n");
    return EXIT_ERROR;
  }

  switch(flag){
    case(TCP_INITATOR):
      if(serverIP == NULL){
        perror("ERROR serverIP NULL");
        return EXIT_ERROR;
      }
      memset(&conn, 0, sizeof(conn));          
      conn.sin_family = AF_INET;          
      conn.sin_addr.s_addr = inet_addr(serverIP);  
      conn.sin_port = htons(port); 
      dst->conn = conn; /* conn is server ip & port */
      fprintf(stdout, "my_conn addr[%d]:port[%d]\n", conn.sin_addr.s_addr, conn.sin_port);
      fflush(stdout);

      my_addr.sin_family = AF_INET;
      my_addr.sin_addr.s_addr = htonl(INADDR_ANY);
      my_addr.sin_port = 0;                          /* their_port = 15441 */
      fprintf(stdout, "bind addr[%d]:port[%d]\n", htonl(INADDR_ANY), 0);
      fflush(stdout);
      if (bind(sockfd, (struct sockaddr *) &my_addr, /* my_port = random */
        sizeof(my_addr)) < 0){                       /* my_conn = (16777226)ip, 20796(15441) */ /* fd = /(all), /(all) */

        perror("ERROR on binding");
        return EXIT_ERROR;
      }



      break;
    
    case(TCP_LISTENER):
      bzero((char *) &conn, sizeof(conn));
      conn.sin_family = AF_INET;
      conn.sin_addr.s_addr = htonl(INADDR_ANY);
      conn.sin_port = htons((unsigned short)port);

      optval = 1;
      setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,/* their_port = 15441 */
           (const void *)&optval , sizeof(int));  /* my_port = 15441 */
      fprintf(stdout, "bind addr[%d]:port[%d]\n", htonl(INADDR_ANY), htons((unsigned short)port));
      fflush(stdout);
      if (bind(sockfd, (struct sockaddr *) &conn, /* my_conn = /(all), 20796(15441) */  /* bind = /(all), 20796(15441) */
        sizeof(conn)) < 0){
          perror("ERROR on binding");
          return EXIT_ERROR;
      }
      dst->conn = conn;
      fprintf(stdout, "my_conn addr[%d]:port[%d]\n", conn.sin_addr.s_addr, conn.sin_port);
      fflush(stdout);
      break;

    default:
      perror("Unknown Flag");
      return EXIT_ERROR;
  }

  getsockname(sockfd, (struct sockaddr *) &my_addr, &len);
  dst->my_port = ntohs(my_addr.sin_port);
  fprintf(stdout, "my_port port[%d]\n\n", dst->my_port);
  fflush(stdout);

  /* todo */
  cmu_handshake(dst); /* listen: 0/1   client: 0/1 */

  pthread_create(&(dst->thread_id), NULL, begin_backend, (void *)dst);  
  return EXIT_SUCCESS;
}

/*
 * Param: sock - The socket to close.
 *
 * Purpose: To remove any state tracking on the socket.
 *
 * Return: Returns error code information on the close operation.
 *
 */
int cmu_close(cmu_socket_t * sock){
  while(pthread_mutex_lock(&(sock->death_lock)) != 0);
  sock->dying = TRUE;
  pthread_mutex_unlock(&(sock->death_lock));

  pthread_join(sock->thread_id, NULL); 

  if(sock != NULL){
    if(sock->received_buf != NULL)
      free(sock->received_buf);
    if(sock->sending_buf != NULL)
      free(sock->sending_buf);
  }
  else{
    perror("ERORR Null scoket\n");
    return EXIT_ERROR;
  }
  return close(sock->socket);
}

/*
 * Param: sock - The socket to read data from the received buffer.
 * Param: dst - The buffer to place read data into.
 * Param: length - The length of data the buffer is willing to accept.
 * Param: flags - Flags to signify if the read operation should wait for
 *  available data or not.
 *
 * Purpose: To retrive data from the socket buffer for the user application.
 *
 * Return: If there is data available in the socket buffer, it is placed
 *  in the dst buffer, and error information is returned. 
 *
 */
int cmu_read(cmu_socket_t * sock, char* dst, int length, int flags){
  char* new_buf;
  int read_len = 0;

  if(length < 0){
    perror("ERROR negative length");
    return EXIT_ERROR;
  }

  while(pthread_mutex_lock(&(sock->recv_lock)) != 0);  /* lock recv */

  switch(flags){
    case NO_FLAG: /* wait if nothing to read */
      while(sock->received_len == 0){
        pthread_cond_wait(&(sock->wait_cond), &(sock->recv_lock));  /* wait for wait-cond */
      }
    case NO_WAIT: /* no wait if nothing to read */   /* [[  length   ] read_buf ] cut out length from read_buf  */
      if(sock->received_len > 0){ /* if have stuff to read */
        if(sock->received_len > length)
          read_len = length;
        else
          read_len = sock->received_len;

        memcpy(dst, sock->received_buf, read_len);

        if(read_len < sock->received_len){ /* if buffer still have stuff */
           new_buf = malloc(sock->received_len - read_len); /* change buffer */
           memcpy(new_buf, sock->received_buf + read_len, 
            sock->received_len - read_len);
           free(sock->received_buf);
           sock->received_len -= read_len;
           sock->received_buf = new_buf;
        }
        else{ /* no buffer then free */
          free(sock->received_buf);
          sock->received_buf = NULL;
          sock->received_len = 0;
        }
      }
      break;
    default:
      perror("ERROR Unknown flag.\n");
      read_len = EXIT_ERROR;
  }
  pthread_mutex_unlock(&(sock->recv_lock));
  return read_len;
}

/*
 * Param: sock - The socket which will facilitate data transfer.
 * Param: src - The data source where data will be taken from for sending.
 * Param: length - The length of the data to be sent.
 *
 * Purpose: To send data to the other side of the connection.
 *
 * Return: Writes the data from src into the sockets buffer and
 *  error information is returned. 
 *
 */
int cmu_write(cmu_socket_t * sock, char* src, int length){  /* write to send buffer */
  while(pthread_mutex_lock(&(sock->send_lock)) != 0); /* wait lock to write */

  if(sock->sending_buf == NULL) /* [ send_buf ] [  length new ]   */
    sock->sending_buf = malloc(length);
  else
    sock->sending_buf = realloc(sock->sending_buf, length + sock->sending_len);
  memcpy(sock->sending_buf + sock->sending_len, src, length);
  sock->sending_len += length;

  pthread_mutex_unlock(&(sock->send_lock));
  return EXIT_SUCCESS;
}

