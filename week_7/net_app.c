#include <poll.h>
#include <stdio.h>
#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>
#include <memory.h>
#include <net/if.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define true 1
#define false 0
#define QUEUE_SIZE 10 // Maximum host at once
#define ORIGIN_SERVER_ADDRESS "192.168.2.2"

int known_hosts = 1;
int master_sock_tcp_fd = 0;
int stop_requested = 0;

typedef struct data_struct_s {
  int is_known;
  char host_names[1024];
} data_struct_t;

data_struct_t h;

void * ping_hosts();
void * process_incoming_host();
char * get_address(data_struct_t d, int i);
void set_address(data_struct_t * d, int i, int size, char * value);

void handle_sigint(int n);

int main(int argc, char* argv[]) {
  // Initialization

  signal(SIGINT, handle_sigint);

  // Array of hosts
  int i = 0;
  while (i < 32) {
    set_address(&h, i, strlen("0.0.0.0"), "0.0.0.0");
    i++;
  }

  if (argc < 2) {
    known_hosts++;
    set_address(&h, 0, strlen(ORIGIN_SERVER_ADDRESS), ORIGIN_SERVER_ADDRESS);
  }

  // Master socket creation
  if ((master_sock_tcp_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
      printf("socket creation failed\n");
      exit(1);
  }

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = 2000;
  addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(master_sock_tcp_fd, (struct sockaddr *)&addr, (socklen_t) sizeof(struct sockaddr)) == -1) {
      printf("socket bind failed\n");
      printf("%d\n", errno);
      return;
  }

  if (listen(master_sock_tcp_fd, QUEUE_SIZE) < 0) {
      printf("listen failed\n");
      return;
  }

  // Ping known hosts
  pthread_t t1_id;
  pthread_attr_t t1_attr;
  pthread_attr_init(&t1_attr);
  pthread_create(&t1_id, &t1_attr, (void *) ping_hosts, NULL);

  fd_set read_fds; // For select

  while (!stop_requested) {

    // block on select
    FD_ZERO(&read_fds);
    FD_SET(master_sock_tcp_fd, &read_fds);

    struct timeval tv = {3, 0}; // Sleep for 2 sec
    if (select(master_sock_tcp_fd + 1, &read_fds, NULL, NULL, &tv) < 0) {
      //printf("Select failed...\n");
    } else {
      // If any - process_connection
      pthread_t t2_id;
      if (FD_ISSET(master_sock_tcp_fd, &read_fds)) {
        pthread_attr_t t2_attr;
        pthread_attr_init(&t2_attr);
        pthread_create(&t2_id, &t2_attr, (void *) process_incoming_host, NULL);
      }
      pthread_join(t2_id, NULL);
    }
  }

  pthread_join(t1_id, NULL);

  close(master_sock_tcp_fd);

  return EXIT_SUCCESS;
}

void * ping_hosts() {

  while (!stop_requested) {
    int i = 0;
    while (i < 32) {

      int fd;
      struct ifreq ifr;
      fd = socket(AF_INET, SOCK_DGRAM, 0);
      ifr.ifr_addr.sa_family = AF_INET;
      snprintf(ifr.ifr_name, IFNAMSIZ, "eth0");
      ioctl(fd, SIOCGIFADDR, &ifr);
      char my_address_1[32], my_address_2[32];;
      strcpy(my_address_1, inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
      snprintf(ifr.ifr_name, IFNAMSIZ, "eth1");
      ioctl(fd, SIOCGIFADDR, &ifr);
      strcpy(my_address_2, inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
      close(fd);

      if (get_address(h, i)[0] != '0' &&
      strcmp(my_address_1, get_address(h, i)) &&
      strcmp(my_address_2, get_address(h, i))) {

        struct sockaddr_in dest;
        dest.sin_family = AF_INET;
        dest.sin_port = 2000;
        struct hostent *host = (struct hostent *)gethostbyname(get_address(h, i));
        dest.sin_addr = *((struct in_addr *)host->h_addr);

        int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        connect(sockfd, (struct sockaddr *)&dest, (socklen_t) sizeof(struct sockaddr));

        data_struct_t received_data;

        int addr_len = sizeof(struct sockaddr);
        int bytes = recvfrom(sockfd, &received_data, sizeof(data_struct_t), 0,
        (struct sockaddr *)&dest, &addr_len);
        if (bytes <= 0) {
          printf("Client %s went offline (online :%d)\n", inet_ntoa(dest.sin_addr), --known_hosts);
          set_address(&h, i, strlen("0.0.0.0"), "0.0.0.0");
        } else {
          if (!received_data.is_known) {
            int j = 0;
            while (j < 32) {
              if (get_address(received_data, j)[0] != '0') {
                int k = 0;
                while (k < 32 && get_address(h, k)[0] != '0') {
                  k++;
                }
                if (k < 32) {
                  set_address(&h, k, strlen(get_address(received_data, j)), get_address(received_data, j));
                }
              }
              j++;
            }
          }
        }

        close(sockfd);
      }
      i++;
    }
  }
  pthread_exit(NULL);
}

// When new client appear
void * process_incoming_host() {
  // Temporary socket for data exchange
  struct sockaddr_in in_addr;
  int addr_len = sizeof(struct sockaddr);
  int comm_socket_fd = accept(master_sock_tcp_fd, (struct sockaddr *) &in_addr, &addr_len);
  if (comm_socket_fd < 0) {
      printf("accept error : errno = %d\n", errno);
      exit(0);
  }

  int i = 0;
  int unknown = true;
  while (i < 32) {
    if (!strcmp(inet_ntoa(in_addr.sin_addr), get_address(h, i))) {
      unknown = false;
    }
    i++;
  }

  if (unknown) {
    printf("NEW client %s has come (online :%d)\n", inet_ntoa(in_addr.sin_addr), ++known_hosts);
    int k = 0;
    while (k < 32 && get_address(h, k)[0] != '0') {
      k++;
    }
    set_address(&h, k, strlen(inet_ntoa(in_addr.sin_addr)), inet_ntoa(in_addr.sin_addr));
    h.is_known = false;
    sendto(comm_socket_fd, &h, sizeof(data_struct_t), 0,
    (struct sockaddr *) &in_addr, (socklen_t) sizeof(struct sockaddr)); // Send list of known hosts
  } else {
    //printf("Client: %s is online\n", inet_ntoa(in_addr.sin_addr));

    data_struct_t temp;
    temp.is_known = true;
    int bytes = sendto(comm_socket_fd, &temp, sizeof(data_struct_t), 0,
    (struct sockaddr *) &in_addr, (socklen_t) sizeof(struct sockaddr));
  }

  close(comm_socket_fd);

  pthread_exit(NULL);
}

void handle_sigint(int n) {
  printf("STOPPING...\n");
  stop_requested = true;
}

void set_address(data_struct_t * d, int i, int size, char * value) {
  int j = 0;
  while (j < size) {
    d->host_names[i * 32 + j] = value[j];
    j++;
  }
}

char * get_address(data_struct_t d, int i) {
  char * res = (char*) malloc(sizeof(char) * 32);
  int j = 0;
  while (j < 32) {
    res[j] = d.host_names[i * 32 + j];
    j++;
  }
  return res;
}
