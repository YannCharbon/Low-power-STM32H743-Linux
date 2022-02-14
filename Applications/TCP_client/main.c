#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <errno.h>

#define MAX 80
#define PORT 8080
#define SERVER_ADDRESS "192.168.0.4"
#define SA struct sockaddr

struct app_shared_struct {
	int sockinetfd;
};

void handler_inet(struct app_shared_struct app_data){
	int ret;
	char buf[MAX];
	int i = 0;

	ret = read(app_data.sockinetfd, buf, sizeof(buf));

	if (ret > 0){
		while(buf[i++] != '\n');
		buf[i] = '\0';
	}

	printf("From TCP server : (%d bytes) %s", ret, buf);
}


void handler_stdin(struct app_shared_struct app_data){
	int ret;
	char buf[MAX];

	ret = read(0, buf, sizeof(buf));

	write(app_data.sockinetfd, buf, ret);
}


//void func(int sockinetfd)
//{
//	char buff[MAX];
//	int n;
//	for (;;) {
//		bzero(buff, sizeof(buff));
//		printf("Enter the string : ");
//		n = 0;
//		while ((buff[n++] = getchar()) != '\n')
//			;
//		write(sockinetfd, buff, sizeof(buff));
//		bzero(buff, sizeof(buff));
//		read(sockinetfd, buff, sizeof(buff));
//		printf("From Server : %s", buff);
//		if ((strncmp(buff, "exit", 4)) == 0) {
//			printf("Client Exit...\n");
//			break;
//		}
//	}
//}

int main()
{
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

	// INIT EPOLL
	int epfd = epoll_create1(0);
	if (epfd == -1) {
		printf("epoll_create() failed: errno=%d\n", errno);
		close(sockinetfd);
		return EXIT_FAILURE;
	}
	// ADD STDIN monitoring
	struct epoll_event ev_stdin;
	ev_stdin.events = EPOLLIN;
	ev_stdin.data.ptr = handler_stdin;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, 0, &ev_stdin) == -1) {
		printf("epoll_ctl(ADD) failed: errno=%d\n", errno);
		close(epfd);
		close(sockinetfd);
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
		return EXIT_FAILURE;
	}
	
	
	struct epoll_event events[2];
	int nr;
	
	// prepare struct
	struct app_shared_struct app_data;
	app_data.sockinetfd = sockinetfd;

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

	// close the socket
	close(sockinetfd);
}
