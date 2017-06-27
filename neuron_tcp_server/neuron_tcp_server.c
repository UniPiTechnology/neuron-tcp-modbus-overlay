/*
 * SPI communication with UniPi Neuron family controllers
 *
 * using epoll pattern 
 *
 * Copyright (c) 2016  Faster CZ, ondra@faster.cz
 *
 * Copyright © 2009-2010 Stéphane Raimbault <stephane.raimbault@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
     * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <getopt.h>

//#include <modbus.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <fcntl.h>
#include <sys/epoll.h>

#include "armspi.h"
#include "armpty.h"
#include "nb_modbus.h"


//int verbose = 0;
//char spi_devices[3][100] = {"/dev/spidev0.1","/dev/spidev0.3","/dev/spidev0.2"};
//int spi_speed[3] = {12000000,12000000,12000000};
//char gpio_int[3][5] = { "27", "23", "22" };

char* spi_devices[MAX_ARMS] = {"/dev/spidev0.1","/dev/spidev0.3","/dev/spidev0.2"};
int spi_speed[MAX_ARMS] = {0,0,0};
char* gpio_int[MAX_ARMS] = { "27", "23", "22" };
char* firmwaredir = "/opt/fw";
int do_check_fw = 0;

#define MAXEVENTS 64

#define NB_CONNECTION    5

#define MAX_MB_BUFFER_LEN   MODBUS_TCP_MAX_ADU_LENGTH
#define MB_BUFFER_COUNT  128;
#define DEFAULT_POLL_TIMEOUT 20             // milisec

nb_modbus_t *nb_ctx = NULL;
int server_socket;

typedef struct _mb_buffer_t mb_buffer_t;

struct _mb_buffer_t {
    mb_buffer_t* next;
    uint16_t index;
    uint16_t sendindex;
    int     id;
    uint8_t data[MAX_MB_BUFFER_LEN];
};


#define ED_MODBUS_SOCKET  0
#define ED_SERVER_SOCKET  1
#define ED_INTERRUPT      2
#define ED_PTY            3

/* user data of event */
typedef struct {
    int fd;
    int type;
    union {
        struct {
          mb_buffer_t* rd_buffer;
          mb_buffer_t* wr_buffer;
        };    
        arm_handle* arm;
    };
    
} mb_event_data_t;


mb_buffer_t* b_stack;

void pool_allocate(void)
{
    mb_buffer_t* prev;
    int n = MB_BUFFER_COUNT;
    b_stack = calloc(1, sizeof(mb_buffer_t));
    b_stack->id = 1; 
    while(n-- > 0) {
        prev = b_stack;
        b_stack = calloc(1, sizeof(mb_buffer_t));
        b_stack->next = prev;
        b_stack->id = prev->id+1;
    }
}


void repool_buffer(mb_buffer_t* buffer)
{
    mb_buffer_t* prev = b_stack;
    b_stack = buffer;
    //printf("repool %d\n", buffer->id);
    while (buffer->next != NULL) {
        buffer = buffer->next;
    }
    buffer->next = prev;
}


mb_buffer_t* get_from_pool(void)
{
    mb_buffer_t* prev = b_stack;
    if (b_stack == NULL) {
        //printf("get from pool NULL\n");
        return NULL;
    }
    //printf("get from pool %d\n", b_stack->id);
    b_stack = prev->next;
    prev->index = 0;
    prev->sendindex = 0;
    prev->next = NULL;
    return(prev);
}


static int make_socket_non_blocking (int sfd)
{
    int flags, s;

    flags = fcntl (sfd, F_GETFL, 0);
    if (flags == -1) {
        perror ("fcntl");
        return -1;
    }

    flags |= O_NONBLOCK;
    s = fcntl (sfd, F_SETFL, flags);
    if (s == -1)  {
        perror ("fcntl");
        return -1;
    }
    return 0;
}


static void close_sigint(int dummy)
{
    close(server_socket);
    nb_modbus_free(nb_ctx);

    exit(dummy);
}



int nb_send(int fd, mb_buffer_t* buffer)
{
	int wanted = buffer->index - buffer->sendindex;
	while (wanted > 0) {
	    int n;
	    n = send(fd, buffer->data + buffer->sendindex, wanted, MSG_NOSIGNAL);
		if (n == -1) {
			if (errno == EAGAIN) {
                return 1;  // buffer sent partially
			}
            perror ("send");
			return -1;     // error fatal 
		}
		buffer->sendindex = buffer->sendindex + n;
		wanted = wanted - n;
	}

    return 0; 
}



void debpr(uint8_t* data, int len)
{
        int x;
        for (x=0; x< len; x++) {
            printf("%02x ", data[x]);
        }
        printf("\n");
}

#define RES_WRITE_QUEUE 1

int parse_buffer(mb_event_data_t* event_data)
{
    /* There can be more than one request in buffer */
    int result = 0;

    while (1) {
        mb_buffer_t* buffer = event_data->rd_buffer;
        if (buffer == NULL) return 0;

        int reqlen = nb_modbus_reqlen(buffer->data, buffer->index);

        //printf("req len = %d\n", reqlen);
        //debpr( buffer->data, buffer->index);

        if (reqlen == 0) return result;
        if (reqlen > MAX_MB_BUFFER_LEN) return -1;   /* bad length in packet header*/

        if (reqlen < buffer->index) {
            /* copy oversized data to new buffer */
            mb_buffer_t* new_buf = get_from_pool();
            if (new_buf == NULL)  return -1;
            new_buf->index = buffer->index-reqlen;
            memmove(new_buf->data, buffer->data+reqlen,new_buf->index);
            event_data->rd_buffer = new_buf;
        } else {
            event_data->rd_buffer = NULL;
        }
        buffer->index = nb_modbus_reply(nb_ctx, buffer->data, reqlen);
        if ( buffer->index > 0) {
            //printf("wr len = %d\n", buffer->index);
            //debpr( buffer->data, buffer->index);
            if (event_data->wr_buffer != NULL) { /* add buffer to write_queue */
                mb_buffer_t* last = event_data->wr_buffer;
                while (last->next != NULL) last = last->next;
                last->next = buffer;
 
           } else {                             /* try to send data */
                int rc = nb_send(event_data->fd, buffer);
                if (rc < 0) {                   /* Fatal error */
                    repool_buffer(buffer);
                    return -1;
                }
                if (rc > 0) {                   /* Data was sent partially, add EPOLLOUT */
                    struct epoll_event event;
                    event_data->wr_buffer = buffer;
                    result = RES_WRITE_QUEUE;
                }
                repool_buffer(buffer);
            }
        } else {
            repool_buffer(buffer);
        }
    } /* while */
}

/* Close fd, return buffers do pool, free data */
void close_event(mb_event_data_t* event_data)
{
    if (event_data->rd_buffer)
        repool_buffer(event_data->rd_buffer);
    if (event_data->wr_buffer)
        repool_buffer(event_data->wr_buffer);
    /* Closing the descriptor will make epoll remove it
       from the set of descriptors which are monitored. */
    close(event_data->fd);
    free(event_data);
}


static struct option long_options[] = {
  {"verbose", no_argument,       0, 'v'},
  {"daemon",  no_argument,       0, 'd'},
  {"listen",  required_argument, 0, 'l'},
  {"port",    required_argument, 0, 'p'},
  {"timeout", required_argument, 0, 't'},
  {"nsspause", required_argument, 0, 'n'},
  {"spidev", required_argument, 0, 's'},
  {"interrupts",required_argument, 0, 'i'},
  {"bauds",required_argument, 0, 'b'},
  {"fwdir", required_argument, 0, 'f'},
  {"check-firmware", no_argument,0, 'c'},
  {0, 0, 0, 0}
};

static void print_usage(const char *progname)
{
  printf("usage: %s [-v[v]] [-d] [-l listen_address] [-p port] [-s dev1[,dev2[,dev3]]] [-i gpio1[,gpio2[,gpio3]]] [-b [baud1,..] [-f firmwaredir] [-c]\n", progname);
  int i;
  for (i=0; ; i++) {
      if (long_options[i].name == NULL)  return;
      printf("  --%s%s\t %s\n", long_options[i].name, 
                                long_options[i].has_arg?"=...":"",
                                "");
  }
}

int parse_slist(char * option, char** results)//, int maxlen)
{
    int i = 0;
    int len;
    char* p = option;
    char* np;

    for (i=0; i<MAX_ARMS; i++) results[i] = NULL;
    i = 0;
    while (p != NULL) {
        if (i >= MAX_ARMS) return 0;
        np = strchr(p, ',');
        if (np != NULL) {
            len = np - p;
            np++;
        } else {
            len = strlen(p);
        }
        results[i] = malloc(len+1);
        if (! results[i]) {
            printf("Error allocate string\n");
            abort();
        }
        strncpy(results[i],p,len);
        results[i][len+1] = '\0';
        p = np; i++;
    }
    return i;
}

int parse_ilist(char * option, int* results)
{
    int i = 0;
    int len;
    char* p = option;
    char* np;

    for (i=0; i<MAX_ARMS; i++) results[i] = 0;
    i = 0;
    while (p != NULL) {
        if (i >= MAX_ARMS) return 0;
        np = strchr(p, ',');
        if (np != NULL) {
            np++;
        }
        results[i] = atoi(p);
        p = np; i++;
    }
    return i;
}

int main(int argc, char *argv[])
{
    int tcp_port = 502;
    char listen_address[100] = "0.0.0.0";

    int poll_timeout = 0;
    int daemon = 0;
    int server_socket;
    int s, nss;
    int efd;
    struct epoll_event event;
    struct epoll_event *events;
    mb_event_data_t*  event_data;

     // Options
    int c;
    while (1) {
       int option_index = 0;
       c = getopt_long(argc, argv, "vdcl:p:t:s:b:i:f:n:", long_options, &option_index);
       if (c == -1) {
           if (optind < argc)  {
               printf ("non-option ARGV-element: %s\n", argv[optind]);
               exit(EXIT_FAILURE);
            }
            break;
       }

       switch (c) {
       case 'v':
           verbose++;
           break;
       case 'd':
           daemon = 1;
           break;
       case 'l':
           strncpy(listen_address, optarg, sizeof(listen_address)-1);
           listen_address[sizeof(listen_address)-1] = '\0';
           break;
       case 'p':
           tcp_port = atoi(optarg);
           if (tcp_port==0) {
               printf("Port must be non-zero integer (given %s)\n", optarg);
               exit(EXIT_FAILURE);
           }
           break;
       case 't':
           poll_timeout = atoi(optarg);
           if (poll_timeout==0) {
               printf("Timeout must be non-zero integer (given %s)\n", optarg);
               exit(EXIT_FAILURE);
           }
           break;
       case 'n':
           nss = atoi(optarg);
           if (nss <= 0) {
               printf("Nss pause must be non-zero integer (given %s)\n", optarg);
               exit(EXIT_FAILURE);
           } else {
               nss_pause = nss;
           }
           break;
       case 's':
           if (parse_slist(optarg, spi_devices) == 0) {
               printf("Bad spidevices count(1-3) (%s))\n", optarg);
               exit(EXIT_FAILURE);
           }
           break;
       case 'i':
           if (parse_slist(optarg, gpio_int) == 0) {
               printf("Bad interrupts count(1-3) (%s))\n", optarg);
               exit(EXIT_FAILURE);
           }
           break;
       case 'b':
           if (parse_ilist(optarg, spi_speed) == 0) {
               printf("Bad bauds count(1-3) (%s))\n", optarg);
               exit(EXIT_FAILURE);
           }
           break;
       case 'f':
           firmwaredir = strdup(optarg);
           break;
       case 'c':
           do_check_fw = 1;
           break;
       default:
           print_usage(argv[0]);
           exit(EXIT_FAILURE);
           break;
       }
    }


    nb_ctx = nb_modbus_new_tcp(listen_address, tcp_port);
    nb_ctx->fwdir = firmwaredir;
    server_socket = modbus_tcp_listen(nb_ctx->ctx, NB_CONNECTION);
    if (server_socket == -1) {
        perror ("modbus_tcp_listen");
        abort ();
    }

    signal(SIGINT, close_sigint);
    s = make_socket_non_blocking (server_socket);
    if (s == -1)  abort ();

    /* Create arm handles */
    int ai;
    for (ai=0; ai<MAX_ARMS; ai++) {
        nb_ctx->arm[ai] = NULL;
        char* dev=spi_devices[ai];
        if ((dev != NULL) && (strlen(dev)>0)) {
            int speed = spi_speed[ai];
            if (!(speed > 0)) speed = spi_speed[0];
            //if (!(speed > 0)) speed = 12000000;
            add_arm(nb_ctx, ai, dev, speed, gpio_int[ai]);
            if (nb_ctx->arm[ai] && do_check_fw)
                arm_firmware(nb_ctx->arm[ai], firmwaredir, FALSE);
        }
    }

    /* Prepare epoll structure, insert server_socket to epoll */
    efd = epoll_create1 (0);
    if (efd == -1) {
        perror ("epoll_create");
        abort ();
    }
    event_data = calloc(1, sizeof(mb_event_data_t));
    event_data->fd = server_socket;
    event_data->type = ED_SERVER_SOCKET;
    event.data.ptr = event_data;
    event.events = EPOLLIN | EPOLLET;
    s = epoll_ctl (efd, EPOLL_CTL_ADD, server_socket, &event);
    if (s == -1) {
        perror ("epoll_ctl");
        abort ();
    }

    /* Insert board interrupt sockets to epoll */
    int fdint;
    for (ai=0; ai < MAX_ARMS; ai++) {
        arm_handle* arm = nb_ctx->arm[ai];
        if (arm == NULL) continue;
        fdint = arm->fdint;
        if (fdint >= 0) {
            event_data = calloc(1, sizeof(mb_event_data_t));
            event_data->fd = fdint;
            event_data->type = ED_INTERRUPT;
            event_data->arm = arm;
            event.events = EPOLLPRI;// | EPOLLET;
            event.data.ptr = event_data;
            s = epoll_ctl(efd, EPOLL_CTL_ADD, fdint, &event);
        } else {
            if (poll_timeout == 0) {
                poll_timeout = DEFAULT_POLL_TIMEOUT;
            }
        }
        /* ----- ToDo more Uarts */
        int pi, pty;
        //printf ("uarts = %d\n", arm->uart_count);
        for (pi=0; pi < arm->bv.uart_count; pi++) {
            pty = armpty_open(arm, pi);
            if (pty >= 0) {
                event_data = calloc(1, sizeof(mb_event_data_t));
                event_data->fd = pty;
                event_data->type = ED_PTY;
                event_data->arm = arm;
                event.events =  EPOLLPRI | EPOLLIN | EPOLLHUP;// | EPOLLET;
                event.data.ptr = event_data;
                s = epoll_ctl (efd, EPOLL_CTL_ADD, pty, &event);
            }
        }
    }

    if (poll_timeout == 0) poll_timeout = -1; 
    if (verbose) printf ("poll timeout = %d[ms]\n", poll_timeout);
    /* Prepare buffer pool */
    pool_allocate();

    /* Event array to be returned */
    events = calloc (MAXEVENTS, sizeof event);

    if (daemon) {
        pid_t pid = fork();
        if (pid == -1)
            return -1;
        else if (pid != 0)
            exit (EXIT_SUCCESS);
        /* create new session and process group */
        if (setsid ( ) == -1)  return -1;
        /* set the working directory to the root directory */
        if (chdir ("/") == -1)  return -1;
        /* close all std file handles and redirect to null */
        close (0); close(1); close(2);
        open ("/dev/null", O_RDWR);  /* stdin */
        dup (0);                     /* stdout */
        dup (0);                     /* stderror */
    }

    if (verbose) printf("Starting loop\n");
    /* The event loop */
    while (1) {

        if (deferred_op == DFR_OP_FIRMWARE) {
            deferred_op = DFR_NONE;
            arm_firmware(deferred_arm, firmwaredir, FALSE);
        }

        int n, i;
        n = epoll_wait (efd, events, MAXEVENTS, poll_timeout);
        for (i = 0; i < n; i++) {
            event_data = events[i].data.ptr;
            /* ..  Check Interrupts .. */
            if (event_data->type == ED_INTERRUPT) {
                if (verbose>1) printf("INT on arm%d\n", event_data->arm->index);
                if ((events[i].events & EPOLLPRI) && (event_data->arm != NULL)) {
                    uint16_t intval;
                    fdint = event_data->fd;
                    pread(fdint, &intval, 2, 0); // read 2 bytes value of gpio - should be 1
                    //printf("INT on arm%d : %04x\n", event_data->arm->index, intval);
                    //if ((intval & 0xff) == 0x31)
                    armpty_readuart(event_data->arm, 1);
                }
                continue;
            }

            if (event_data->type == ED_PTY) {
                if (event_data->arm == NULL) continue;
                if ((events[i].events & EPOLLPRI)) {
                    armpty_setuart(event_data->fd, event_data->arm, 0/*event_data->uart*/);
                    //continue;
                }
                if ((events[i].events & EPOLLIN)) {
                    armpty_readpty(event_data->fd, event_data->arm, 0/*event_data->uart*/);
                    //continue;
                }
                if ((events[i].events & EPOLLHUP)) {
                    printf("HUP on PTY arm%d : %c\n", event_data->arm->index);
                }
                continue;
            }

            if ((events[i].events & EPOLLERR) ||
                (events[i].events & EPOLLHUP) ||
                (!(events[i].events & EPOLLIN))) {
                /* An error has occured on this fd, or the socket is not
                   ready for reading (why were we notified then?) */
                if (events[i].events & EPOLLERR) fprintf (stderr, "epoll ERR error\n");
                if (events[i].events & EPOLLHUP) fprintf (stderr, "epoll HUP error\n");
                if (events[i].events & EPOLLOUT) fprintf (stderr, "epoll OUT error\n");
                fprintf (stderr, "epoll error\n");
                close_event(event_data);
                continue;
            }

            /* Check listening socket */
            if (event_data->type == ED_SERVER_SOCKET) {
                /* We have a notification on the listening socket, which
                   means one or more incoming connections. */
                while (1)  {
                    /* A client is asking a new connection */
                    socklen_t addrlen;
                    struct sockaddr_in clientaddr;
                    int newfd;
                    /* Handle new connections */
                    addrlen = sizeof(clientaddr);
                    memset(&clientaddr, 0, sizeof(clientaddr));
                    newfd = accept(server_socket, (struct sockaddr *)&clientaddr, &addrlen);
                    if (newfd == -1) {
                        if (!((errno == EAGAIN) || (errno == EWOULDBLOCK)))
                            perror("Server accept() error");
                        break;
                    }
                    printf("New connection from %s:%d on socket %d\n",
                               inet_ntoa(clientaddr.sin_addr), clientaddr.sin_port, newfd);
                    /* Make the incoming socket non-blocking and add it to the
                       list of fds to monitor. */
                    s = make_socket_non_blocking (newfd);
                    if (s == -1)  {
                        close(newfd);
                        continue;
                    }

                    event_data = calloc(1, sizeof(mb_event_data_t));
                    event_data->fd = newfd;
                    event_data->type = ED_MODBUS_SOCKET;
                    event.data.ptr = event_data;
                    event.events = EPOLLIN | EPOLLET;
                    s = epoll_ctl (efd, EPOLL_CTL_ADD, newfd, &event);
                    if (s == -1) {
                        perror ("epoll_ctl");
                        close_event(event_data);
                    }
                }
                continue;
            }

            if (events[i].events & EPOLLOUT) {
                mb_buffer_t* buffer;
                while (1) {
                    /* try to send data */
                    int rc = nb_send(event_data->fd, event_data->wr_buffer);
                    if (rc < 0) { /* Fatalni error */
                        close_event(event_data);
                        break;  // continue on next socket
                    }
                    if (rc == 0) { /* All data from buffer was sent */
                        buffer = event_data->wr_buffer;
                        event_data->wr_buffer == buffer->next;
                        buffer->next = NULL;
                        repool_buffer(buffer);
                        if (event_data->wr_buffer == NULL) {
                            event.events =  EPOLLIN | EPOLLET;
                            s = epoll_ctl (efd, EPOLL_CTL_MOD, event_data->fd, &event);
                            if (s == -1)  perror ("epoll_ctl");
                            break;
                        }
                    } else break;
                }
            }

            if (events[i].events & EPOLLIN) {

                /* We have data on the fd waiting to be read.
                   We must read whatever data is available completely,
                   as we are running in edge-triggered mode and 
                   won't get a notification again for the same data. */
                while (1) {
                    ssize_t count;
                    int rc;

                    if (event_data->rd_buffer == NULL) {
                        event_data->rd_buffer = get_from_pool();
                        if (event_data->rd_buffer == NULL) break;  // ToDo - cteni neprobehne a epoll znovu neprijde!
                    }
                    int wanted = MAX_MB_BUFFER_LEN - event_data->rd_buffer->index;
                    count = read(event_data->fd,
                                 event_data->rd_buffer->data + event_data->rd_buffer->index,
                                 wanted);

                    //printf("read len = %d\n", count);
                    //debpr( event_data->rd_buffer->data, count);

                    if (count == -1) {
                        /* If errno == EAGAIN, that means we have read all data. So go back to main loop. */
                        if (errno == EAGAIN) break;
                        perror ("read");
                        close_event(event_data);
                        break;
                    }
                    if (count == 0) {
                        /* End of file. The remote has closed the connection. */
                        printf ("Closed connection on descriptor %d\n", event_data->fd);
                        close_event(event_data);
                        break;
                    }
                    event_data->rd_buffer->index = event_data->rd_buffer->index + count;

                    /* Do action on buf */
                    rc = parse_buffer(event_data);
                    if (rc == -1) {
                        close_event(event_data);
                        break;
                    } 
                    if (rc == RES_WRITE_QUEUE) {
                        event.events =  EPOLLIN | EPOLLOUT | EPOLLET;
                        event.data.ptr =  event_data;
                        if (epoll_ctl (efd, EPOLL_CTL_MOD, event_data->fd, &event) == -1) 
                            perror ("epoll_ctl");
                    }

                    //if (count < 0) break; // socket is closed due to error
                    if (count < wanted) break; //?? je to spravne ??
                } /* while */
            } /* if EPOLLIN */
            /* End of one event */
        }
        if (poll_timeout > 0) {
          for (ai=0; ai < MAX_ARMS; ai++) {
            arm_handle* arm = nb_ctx->arm[ai];
            if (arm == NULL) continue;
            if ((arm->bv.int_mask_register <= 0) && (arm->bv.uart_count>0)) {
                if (verbose > 2) printf("readpty..\n");
                armpty_readuart(arm, 1);
            }
          }
        }
    }
}
