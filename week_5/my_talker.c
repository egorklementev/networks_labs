#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "common.h"

#define SERVERPORT            "2000"
#define SERVER_IP_ADDRESS   "192.168.3.2"

msg_struct_t client_data;
msg_struct_t result;

void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int setup_udp_communication() {

	int sockfd;
	struct addrinfo hints, *servinfo, *p;
	int rv;
	int numbytes;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;

	if ((rv = getaddrinfo("listener", SERVERPORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("talker: socket");
			continue;
		}
		break;
	}

	if (p == NULL) {
		fprintf(stderr, "talker: failed to create socket\n");
		return 2;
	}

	if ((numbytes = sendto(sockfd, (void *)&client_data, sizeof(msg_struct_t), 0, p->ai_addr, p->ai_addrlen)) == -1) {
		perror("talker: sendto");
		exit(1);
	}

	printf("talker: sent %d bytes to %s\n", numbytes, "listener");

	freeaddrinfo(servinfo);

	struct sockaddr_storage their_addr;
	socklen_t addr_len;
	addr_len = sizeof their_addr;
	char s[INET6_ADDRSTRLEN];

	if ((numbytes = recvfrom(sockfd, &result, sizeof(msg_struct_t) , 0,
		(struct sockaddr *)&their_addr, &addr_len)) == -1) {
		perror("recvfrom");
		exit(1);
	}

	printf("talker: got packet from %s\n",
		inet_ntop(their_addr.ss_family,
			get_in_addr((struct sockaddr *)&their_addr),
			s, sizeof s));
	printf("talker: packet is %d bytes long\n", numbytes);
	printf("talker: packet contains \"%s\"\n", result.name);
	printf("talker: packet contains \"%u\"\n", result.age);
	printf("talker: packet contains \"%u\"\n", result.group);

	close(sockfd);
}


int main(int argc, char **argv) {

	char given_name[256];
	unsigned int given_age;
	unsigned int given_group;

	printf("Enter name: ?\n");
	scanf("%s", given_name);
	printf("Enter age : ?\n");
	scanf("%u", &given_age);
        printf("Enter group : ?\n");
	scanf("%u", &given_group);

        int i = 0;
	while (i < 256)
	{
		client_data.name[i] = given_name[i];
		i++;
	}
	client_data.age = given_age;
	client_data.group = given_group;

    setup_udp_communication();
    printf("application quits\n");
    return 0;
}
