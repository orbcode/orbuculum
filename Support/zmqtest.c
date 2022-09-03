#include <stdio.h>
#include <zmq.h>

#define DEFAULT_ZMQ_BIND_URL "tcp://localhost:3442"

int main( int argc, void **argv )

{
  void *ctx = zmq_ctx_new();
  void *sock = zmq_socket( ctx, ZMQ_SUB);
  zmq_connect(sock, DEFAULT_ZMQ_BIND_URL );
  zmq_setsockopt(sock, ZMQ_SUBSCRIBE, "f",1);

  zmq_msg_t message;
  while (1)
    {
      zmq_msg_init(&message);
      zmq_msg_recv(&message,sock,0);
      if (zmq_msg_size(&message) != 1)
	{
	  char *p = zmq_msg_data(&message);
	  int i = zmq_msg_size(&message);
	  while (i--)
	    putchar(*p++);
	}
      zmq_msg_close(&message);
    }
}
