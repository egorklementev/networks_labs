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
#define ORIGIN_SERVER_ADDRESS "origin:192.168.2.2:2000"

int master_sock_tcp_fd = 0;
int stop_requested = 0;

typedef struct file_s {
  int word_count;
  char filename[64];
  char data[2048];
} file_t;

typedef struct host_str_s {
  char ip[32];
  char filename[64];
} host_str_t;

file_t files[32];
char sync_msg[2048];
char known_hosts[2048];
int hosts_num = 0;
int file_num = 0;

char my_ip[32];
char my_port[8];
char my_name[64];

void * my_sync();
void * my_request();
void * process_incoming_host();
char * get_host(int i);
char * parse_ip(int i);
char * parse_port(int i);
char * parse_name(int i);
int has_file(char * file);
int know_host(char * sync);
void add_host(char * host);
void add_file(char * file);
void add_host(char * host);

void handle_sigint(int n);

// argv[1] should contain name of the node,
// [./a.out name -0] - for origin
// [./a.out name] - for regular nodes
int main(int argc, char* argv[]) {
  // Initialization
  int i = 0;

  signal(SIGINT, handle_sigint); // For leaving the network

  // Master socket creation
  if ((master_sock_tcp_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
      printf("socket creation failed\n");
      exit(1);
  }

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = 2000;
  addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(master_sock_tcp_fd, (struct sockaddr *) &addr, (socklen_t) sizeof(struct sockaddr)) == -1) {
      printf("socket bind failed\n");
      printf("%d\n", errno);
      return;
  }

  if (listen(master_sock_tcp_fd, QUEUE_SIZE) < 0) {
      printf("listen failed\n");
      return;
  }

  // Examine own IP and port
  struct ifreq ifr;
  ifr.ifr_addr.sa_family = AF_INET;
  snprintf(ifr.ifr_name, IFNAMSIZ, "eth0");
  ioctl(master_sock_tcp_fd, SIOCGIFADDR, &ifr);
  strcpy(my_ip, inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
  sprintf(my_port, "%d", 2000);

  sprintf(my_name, "%s", argv[1]);
  sprintf(sync_msg, "%s", argv[1]);
  strcat(sync_msg, ":");
  strcat(sync_msg, my_ip);
  strcat(sync_msg, ":");
  strcat(sync_msg, my_port);
  strcat(sync_msg, ":");

  if (argc < 3) {
    add_host(ORIGIN_SERVER_ADDRESS);
  } else {
    while (file_num < 3) {
      // Only origin node knows the content of the files initially
      files[file_num].word_count = 1; // Assumes that file is not empty
      char path[16];
      char name = 'a' + file_num;
      sprintf(path, "%c", name);
      strcat(path, ".txt\0");
      FILE* fp = fopen(path, "r");
      char iter = fgetc(fp);
      i = 0;
      sprintf(files[file_num].filename, "%s", path);
      while(!feof(fp)) {
        if (iter == ' ') {
          files[file_num].word_count++;
        }
        files[file_num].data[i] = iter;
        iter = fgetc(fp);
        i++;
      }
      fclose(fp);
      add_file(path);
      printf("I am the origin\nand I have file '%s' with the text:\n%s\n", path, files[file_num - 1].data);
    }
  }

  printf("My sync message is: %s\n", sync_msg);

  // Ping known hosts
  pthread_t t_ping_id;
  pthread_attr_t t_ping_attr;
  pthread_attr_init(&t_ping_attr);
  pthread_create(&t_ping_id, &t_ping_attr, (void *) my_sync, NULL);

  fd_set read_fds; // For select

  while (!stop_requested) {

    // block on select
    FD_ZERO(&read_fds);
    FD_SET(master_sock_tcp_fd, &read_fds);

    struct timeval tv = {3, 0}; // Sleep for 3 sec
    if (select(master_sock_tcp_fd + 1, &read_fds, NULL, NULL, &tv) > 0) {
      // If any - process_incoming_host()
      pthread_t t_proc_id;
      if (FD_ISSET(master_sock_tcp_fd, &read_fds)) {
        pthread_attr_t t_proc_attr;
        pthread_attr_init(&t_proc_attr);
        pthread_create(&t_proc_id, &t_proc_attr, (void *) process_incoming_host, NULL);
      }
      pthread_join(t_proc_id, NULL);
    }
  }

  pthread_join(t_ping_id, NULL);

  close(master_sock_tcp_fd);

  return EXIT_SUCCESS;
}

void * my_sync() {
  while (!stop_requested) {
    int i = 0;
    while (i < hosts_num) {
      char * nip = parse_ip(i);
      char * nport = parse_port(i);
      if (strcmp(nip, my_ip)) {

        struct sockaddr_in dest;
        dest.sin_family = AF_INET;
        dest.sin_port = atoi(nport);
        struct hostent *host = (struct hostent *)gethostbyname(nip);
        dest.sin_addr = *((struct in_addr *)host->h_addr);

        free(nip);
        free(nport);

        int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        connect(sockfd, (struct sockaddr *)&dest, (socklen_t) sizeof(struct sockaddr));

        // Send sync int
        int bit = 1;
        int bytes = sendto(sockfd, &bit, sizeof(bit), 0, (struct sockaddr *) &dest, (socklen_t) sizeof(struct sockaddr));

        // Send sync message
        bytes = sendto(sockfd, &sync_msg, sizeof(sync_msg), 0, (struct sockaddr *) &dest, (socklen_t) sizeof(struct sockaddr));

        // Send number of known nodes
        bytes = sendto(sockfd, &hosts_num, sizeof(hosts_num), 0, (struct sockaddr *) &dest, (socklen_t) sizeof(struct sockaddr));

        // Send info about known nodes
        int j = 0;
        while (j < hosts_num) {
          char host_info[64];
          char * nname = parse_name(j);
          char * nip = parse_ip(j);
          char * nport = parse_port(j);
          sprintf(host_info, "%s:%s:%s", nname, nip, nport);
          bytes = sendto(sockfd, &host_info, sizeof(host_info), 0, (struct sockaddr *) &dest, (socklen_t) sizeof(struct sockaddr));
          j++;
          free(nname);
          free(nip);
          free(nport);
        }

        close(sockfd);
      }
      i++;
    }
  }
  pthread_exit(NULL);
}

void * my_request(void * data) {

  host_str_t * hs = (host_str_t *) data;

  int addr_len = sizeof(struct sockaddr);

  struct sockaddr_in dest;
  dest.sin_family = AF_INET;
  dest.sin_port = 2000;
  struct hostent *host = (struct hostent *)gethostbyname(hs->ip);
  dest.sin_addr = *((struct in_addr *)host->h_addr);

  int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  connect(sockfd, (struct sockaddr *) &dest, (socklen_t) sizeof(struct sockaddr));

  char filename[64];
  sprintf(filename, "%s", hs->filename);
  FILE* fp = fopen(filename, "w");

  int bit = 0;
  int bytes = sendto(sockfd, &bit, sizeof(bit), 0, (struct sockaddr *) &dest, (socklen_t) sizeof(struct sockaddr));

  bytes = sendto(sockfd, filename, sizeof(filename), 0, (struct sockaddr *) &dest, (socklen_t) sizeof(struct sockaddr));

  int word_count;
  bytes = recvfrom(sockfd, &word_count, sizeof(word_count), 0, (struct sockaddr *) &dest, &addr_len);

  char buf[2048];
  sprintf(buf, "--");
  while (word_count > 0) {
    char word[32];
    bytes = recvfrom(sockfd, word, sizeof(word), 0, (struct sockaddr *) &dest, &addr_len);
    fputs(word, fp);
    strcat(buf, word);
    word_count--;
  }

  printf("File [%s] with the text \n[%s]--\n was received!\n", filename, buf);

  fclose(fp);

  pthread_exit(NULL);
}

void * process_incoming_host() {

  // Temporary socket for data exchange
  struct sockaddr_in in_addr;
  int addr_len = sizeof(struct sockaddr);
  int comm_socket_fd = accept(master_sock_tcp_fd, (struct sockaddr *) &in_addr, &addr_len);
  if (comm_socket_fd < 0) {
      printf("accept error : errno = %d\n", errno);
      exit(0);
  }

  int bit = -1;
  int bytes = recvfrom(comm_socket_fd, &bit, sizeof(bit), 0, (struct sockaddr *)&in_addr, &addr_len);

  if (bit) {

    char sm[2048];
    bytes = recvfrom(comm_socket_fd, sm, sizeof(sm), 0, (struct sockaddr *)&in_addr, &addr_len);

    int node_num;
    bytes = recvfrom(comm_socket_fd, &node_num, sizeof(node_num), 0, (struct sockaddr *)&in_addr, &addr_len);

    char my_info[64];
    sprintf(my_info, "%s:%s:%s", my_name, my_ip, my_port);

    int ni = 0;
    while (ni < node_num) {
      char host_info[64];
      bytes = recvfrom(comm_socket_fd, host_info, sizeof(host_info), 0, (struct sockaddr *)&in_addr, &addr_len);
      if (strcmp(my_info, host_info) && !know_host(host_info)) {
        printf("New host [%s] is now known!\n", host_info);
        add_host(host_info);
      }
      ni++;
    }

    int end = 0;
    int semicolons = 0;
    while (semicolons < 3) {
      if (sm[end++] == ':') {
        semicolons++;
      }
    }
    end--;

    int i = end - 1;
    char shost[64];
    while (i >= 0) {
      shost[i] = sm[i];
      i--;
    }
    shost[end] = '\0';

    if (strcmp(my_info, shost) && !know_host(shost)) {
      printf("New host [%s] is now known!\n", shost);
      add_host(shost);
    }

    int nf = 0;
    i = end + 1;
    pthread_t tds[64];
    host_str_t hs[64];
    int break_requested = 0;

    while (!break_requested) {
      char filename[64];
      int fi = 0;
      int file_end = 0;
      while (!file_end && !break_requested) {
        if (sm[i] == '\0') {
          break_requested = 1;
          if (fi > 0) {
            file_end = 1;
          }
        } else if (sm[i] == ',') {
          file_end = 1;
          filename[fi] = '\0';
          i++;
        } else {
          filename[fi] = sm[i];
          fi++;
          i++;
        }
      }
      if(file_end && !has_file(filename)) {
        printf("New file [%s] is now known!\n", filename);

        // Send request message
        sprintf(hs[nf].filename, "%s", filename);
        sprintf(hs[nf].ip, "%s", inet_ntoa(in_addr.sin_addr));

        pthread_attr_t t_req_attr;
        pthread_attr_init(&t_req_attr);
        pthread_create((tds + nf), &t_req_attr, (void *) my_request, (void *) (hs + nf));

        add_file(filename);
        nf++;
      }
    }
    while (nf > 0) {
      pthread_join(tds[nf - 1], NULL);
      nf--;
    }

  } else {

    char filename[64];
    bytes = recvfrom(comm_socket_fd, filename, sizeof(filename), 0, (struct sockaddr *) &in_addr, &addr_len);

    int i = 0;
    while (i < 32 && strcmp(filename, files[i].filename)) {
      i++;
    }

    if (i == 32) {
      printf("ERROR: file doesn't exists!\n");
      pthread_exit(NULL);
    }

    int wcount = files[i].word_count;
    bytes = sendto(comm_socket_fd, &wcount, sizeof(wcount), 0, (struct sockaddr *) &in_addr, (socklen_t) sizeof(struct sockaddr));

    int w = 0;
    int j = 0;
    int br = false;
    while (w < wcount) {
      int wi = 0;
      char word[32];
      memset(word, 32, '\0');
      while (files[i].data[j] != ' ' && !br) {
        if (files[i].data[j] == '\0') {
          br = true;
        }
        word[wi] = files[i].data[j];
        wi++;
        j++;
      }
      word[wi] = ' ';
      word[wi + 1] = '\0';
      j++;
      bytes = sendto(comm_socket_fd, word, sizeof(word), 0, (struct sockaddr *) &in_addr, (socklen_t) sizeof(struct sockaddr));
      w++;
    }
  }

  close(comm_socket_fd);

  pthread_exit(NULL);
}

void handle_sigint(int n) {
  printf("%s\n", sync_msg);
  printf("%s\n", known_hosts);
  printf("%d\n", hosts_num);
  printf("%d\n", file_num);
  stop_requested = true;
}

int has_file(char * file) {
  int i = 0;
  while(sync_msg[i] != ':') {
    i++;
  }
  i++;
  while(sync_msg[i] != ':') {
    i++;
  }
  i++;
  while(sync_msg[i] != ':') {
    i++;
  }
  i++;
  int has = false;
  while(sync_msg[i] != '\0') {
    int j = 0;
    char next[32];
    while(sync_msg[i] != ',' && sync_msg[i] != '\0') {
      next[j] = sync_msg[i];
      i++;
      j++;
    }
    next[j] = '\0';
    if (!strcmp(next, file)) {
      has = true;
    }
    if (sync_msg[i] != '\0') {
      i++;
    }
  }
  return has;
}
void add_file(char * file) {
  int i = 0;
  while(sync_msg[i] != '\0') {
    i++;
  }
  if (file_num > 0) {
    sync_msg[i] = ',';
    i++;
  }
  int j = 0;
  while(file[j] != '\0') {
    sync_msg[i + j] = file[j];
    j++;
  }
  sync_msg[i + j] = '\0';
  file_num++;
}

int know_host(char * sync) {
  int i = 0;
  while (i < hosts_num) {
    if (!strcmp(sync, get_host(i))) {
      return true;
    }
    i++;
  }
  return false;
}
void add_host(char * host) {
  int i = 0;
  hosts_num++;
  while(known_hosts[i] != '\0') {
    i++;
  }
  if (hosts_num > 1) {
    known_hosts[i] = ' '; // Space is separator
    i++;
  }
  int j = 0;
  while(host[j] != '\0') {
    known_hosts[i + j] = host[j];
    j++;
  }
  known_hosts[i + j] = '\0';
}
char * get_host(int i) {
  if (i < hosts_num) {
    char * host_info = (char *) malloc(sizeof(char) * 64);
    sprintf(host_info, "%s", parse_name(i));
    strcat(host_info, ":");
    strcat(host_info, parse_ip(i));
    strcat(host_info, ":");
    strcat(host_info, parse_port(i));
    return host_info;
  }
  return NULL;
}

char * parse_ip(int i) {
  if (i < hosts_num) {
    int j = 0;
    while(i > 0) {
      if (known_hosts[j] == ' ') {
        i--;
      }
      j++;
    }
    while(known_hosts[j] != ':') {
      j++;
    }
    char * ip;
    int start = ++j;
    while(known_hosts[j] != ':') {
      j++;
    }
    int length = j - start;
    ip = (char *) malloc(sizeof(char) * (length + 1));
    j = 0;
    while(known_hosts[start + j] != ':'){
      ip[j] = known_hosts[start + j];
      j++;
    }
    ip[j] = '\0';
    return ip;
  }
  return NULL;
}
char * parse_port(int i) {
  if (i < hosts_num) {
    int j = 0;
    while(i > 0) {
      if (known_hosts[j] == ' ') {
        i--;
      }
      j++;
    }
    while(known_hosts[j] != ':') {
      j++;
    }
    j++;
    while(known_hosts[j] != ':') {
      j++;
    }
    char * port;
    int start = ++j;
    while(known_hosts[j] != ' ' && known_hosts[j] != '\0') {
      j++;
    }
    int length = j - start;
    port = (char *) malloc(sizeof(char) * (length + 1));
    j = 0;
    while(known_hosts[start + j] != ' ' && known_hosts[start + j] != '\0'){
      port[j] = known_hosts[start + j];
      j++;
    }
    port[j] = '\0';
    return port;
  }
  return NULL;
}
char * parse_name(int i) {
  if (i < hosts_num) {
    int j = 0;
    while(i > 0) {
      if (known_hosts[j] == ' ') {
        i--;
      }
      j++;
    }
    char * name;
    int start = j++;
    while(known_hosts[j] != ':') {
      j++;
    }
    int length = j - start;
    name = (char *) malloc(sizeof(char) * length);
    j = 0;
    while(known_hosts[start + j] != ':'){
      name[j] = known_hosts[start + j];
      j++;
    }
    name[j] = '\0';
    return name;
  }
  return NULL;
}
