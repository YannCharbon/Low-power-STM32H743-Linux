#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/epoll.h>
#include <errno.h>


#define BUF_MAX 80
#define PORT 8080
#define SERVER_ADDRESS "192.168.0.4"

#define IEEE802154_ADDR_LEN 8
#define MAX_PACKET_LEN 127
#define IEEE802154_ADDR_SHORT 0x2

struct ieee802154_addr_sa {
	int addr_type;
	uint16_t pan_id;
	union {
		uint8_t hwaddr[IEEE802154_ADDR_LEN];
		uint16_t short_addr;
	};
};

struct sockaddr_ieee802154 {
	sa_family_t family;
	struct ieee802154_addr_sa addr;
};

// Structure sharded
struct app_shared_struct {
	int sockinetfd;
	int sockieeefd;
	struct sockaddr_ieee802154* dst;
	int dst_size;
	socklen_t addrlen;
};

void handler_inet(struct app_shared_struct app_data){
	int ret;
	char buf[BUF_MAX];
	int i = 0;

	ret = read(app_data.sockinetfd, buf, sizeof(buf));

	if (ret > 0){
		while(buf[i++] != '\n');
		buf[i] = '\0';
	}

	printf("From TCP server : (%d bytes) %s", ret, buf);
	printf("Sending over IEEE\n");

	ret = sendto(app_data.sockieeefd, buf, strlen(buf), 0, (struct sockaddr *)app_data.dst, app_data.dst_size);
	if (ret < 0) {
		printf("Error : sendto");
	}
	
	printf("Finished send\n");
}

void handler_ieee(struct app_shared_struct app_data){
	int ret;
	char buf[BUF_MAX];

	ret = recvfrom(app_data.sockieeefd, buf, MAX_PACKET_LEN, 0, (struct sockaddr *)app_data.dst, &app_data.addrlen);
	if (ret < 0) {
		printf("error : recvfrom");
	}
	buf[ret] = '\0';

	printf("Received %d bytes from %x : %s\n", ret, app_data.dst->addr.short_addr, buf);
	printf("Sending to TCP server\n");

	write(app_data.sockinetfd, buf, ret);
}


int main(int argc, char* argv[])
{
	printf("TCP to IEEE - version 0.2 - <IEEE local> <IEEE remote> <srv>\n");
	printf("Usage : tcptoieee <IEEE local short addr> <IEEE remote short addr> <is IEEE server 0/1>\n");
	// INIT IEEE
	int sockieeefd;
	ssize_t len;
	int ret;
	struct sockaddr_ieee802154 src, dst;
	socklen_t addrlen;
	
	char buf[MAX_PACKET_LEN + 1];
	/* IEEE 802.15.4 extended send address, adapt to your setup */
	uint8_t long_addr[IEEE802154_ADDR_LEN] = {0xd6, 0x55, 0x2c, 0xd6, 0xe4, 0x1c, 0xeb, 0x57};

	printf("Creating socket\n");
	/* Create IEEE 802.15.4 address family socket for the SOCK_DGRAM type */
	sockieeefd = socket(PF_IEEE802154, SOCK_DGRAM, 0);
	if (sockieeefd < 0) {
		perror("socket");
		return 1;
	}

	printf("Setting up socket\n");

	/* Prepare source socket address struct */
	memset(&dst, 0, sizeof(src));
	dst.family = AF_IEEE802154;
	/* Used PAN ID is 0x23 here, adapt to your setup */
	dst.addr.pan_id = 0x0023;

	if (argc == 4){
		dst.addr.addr_type = IEEE802154_ADDR_SHORT;
		dst.addr.short_addr = atoi(argv[2]);
	} else {
		dst.addr.addr_type = IEEE802154_ADDR_SHORT;
		dst.addr.short_addr = 0x0002;
	} 

	/* Prepare source socket address struct */
	memset(&src, 0, sizeof(src));
	src.family = AF_IEEE802154;
	/* Used PAN ID is 0x23 here, adapt to your setup */
	src.addr.pan_id = 0x0023;

	if (argc == 4){
		src.addr.addr_type = IEEE802154_ADDR_SHORT;
		src.addr.short_addr = atoi(argv[1]);
	} else {
		src.addr.addr_type = IEEE802154_ADDR_SHORT;
		src.addr.short_addr = 0x0001;
	} 

	/* Bind socket on this side */
	if(argc == 4){
		if (atoi(argv[3]) == 1){
			ret = bind(sockieeefd, (struct sockaddr *)&src, sizeof(src));
			if (ret) {
				perror("bind");
				close(sockieeefd);
				return 1;
			}
		} 
	} 

	addrlen = sizeof(dst);

	// INIT INET
	int sockinetfd, connfd;
	struct sockaddr_in servaddr, cli;

	// socket create and verification
	sockinetfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockinetfd == -1) {
		printf("socket creation failed...\n");
		exit(0);
	}
	else
		printf("Socket successfully created..\n");
	bzero(&servaddr, sizeof(servaddr));

	// assign IP, PORT
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = inet_addr(SERVER_ADDRESS);
	servaddr.sin_port = htons(PORT);

	// connect the client socket to server socket
	if (connect(sockinetfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) != 0) {
		printf("connection with the server failed...\n");
		exit(0);
	}
	else
		printf("connected to the server..\n");

	//////

	// INIT EPOLL
	int epfd = epoll_create1(0);
	if (epfd == -1) {
		printf("epoll_create() failed: errno=%d\n", errno);
		close(sockinetfd);
		return EXIT_FAILURE;
	}
	// ADD IEEE monitoring
	struct epoll_event ev_sock_ieee;
	ev_sock_ieee.events = EPOLLIN;
	ev_sock_ieee.data.ptr = handler_ieee;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, sockieeefd, &ev_sock_ieee) == -1) {
		printf("epoll_ctl(ADD) failed: errno=%d\n", errno);
		close(epfd);
		close(sockinetfd);
		close(sockieeefd);
		return EXIT_FAILURE;
	}
	// ADD inet socket monitoring
	struct epoll_event ev_sock_inet;
	ev_sock_inet.events = EPOLLIN;
	ev_sock_inet.data.ptr = handler_inet;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, sockinetfd, &ev_sock_inet) == -1) {
		printf("epoll_ctl(ADD) failed: errno=%d\n", errno);
		close(epfd);
		close(sockinetfd);
		close(sockieeefd);
		return EXIT_FAILURE;
	}
	
	
	struct epoll_event events[2];
	int nr;
	
	// prepare struct
	struct app_shared_struct app_data;
	app_data.sockinetfd = sockinetfd;
	app_data.sockieeefd = sockieeefd;
	app_data.dst = &dst;
	app_data.dst_size = sizeof(dst);
	app_data.addrlen = addrlen;

	while(1) {
		
		// WAIT EVENT
		nr = epoll_wait(epfd, events, 2, -1);
		if (nr < 0) {
			printf("epoll_wait() failed: errno=%d\n", errno);
			close(epfd);
			close(sockinetfd);
			return EXIT_FAILURE;
		}	
		
		// HANDLE EVENTS 
		for (int i=0; i<nr; i++) {
			
			void(*handler)( ) = events[i].data.ptr ;
			handler(app_data);
      
		}
	}
}