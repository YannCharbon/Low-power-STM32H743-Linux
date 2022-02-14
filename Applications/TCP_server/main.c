#include <stdio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <errno.h>

#define MAX 80
#define PORT 8080
#define SA struct sockaddr

struct app_shared_struct {
	int sockinetfd;
};

int handler_inet(struct app_shared_struct app_data){
	int ret;
	char buf[MAX];
	int i = 0;

	ret = read(app_data.sockinetfd, buf, sizeof(buf));

	if (ret > 0){
		while(buf[i++] != '\n');
		buf[i] = '\0';
	}

	printf("From TCP client : (%d bytes) %s", ret, buf);

	return 0;
}

int handler_stdin(struct app_shared_struct app_data){
	int ret;
	char buf[MAX];

	ret = read(0, buf, sizeof(buf));

	if (strncmp("exit\n", buf, 4) == 0) {
		printf("Server Exit...\n");
		return 1;
	}

	write(app_data.sockinetfd, buf, ret);

	return 0;
}

// Function designed for chat between client and server.
void func(int connfd)
{
	char buff[MAX];
	int n;
	// infinite loop for chat
	for (;;) {
		bzero(buff, MAX);

		// read the message from client and copy it in buffer
		read(connfd, buff, sizeof(buff));
		// print buffer which contains the client contents
		printf("From client: %s\t To client : ", buff);
		bzero(buff, MAX);
		n = 0;
		// copy server message in the buffer
		while ((buff[n++] = getchar()) != '\n')
			;

		// and send that buffer to client
		write(connfd, buff, sizeof(buff));

		// if msg contains "Exit" then server exit and chat ended.
		if (strncmp("exit", buff, 4) == 0) {
			printf("Server Exit...\n");
			break;
		}
	}
}

// Driver function
int main()
{
	int ret;
	int sockinetfd, connfd, len;
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
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(PORT);

	// Binding newly created socket to given IP and verification
	if ((bind(sockinetfd, (SA*)&servaddr, sizeof(servaddr))) != 0) {
		printf("socket bind failed...\n");
		exit(0);
	}
	else
		printf("Socket successfully binded..\n");

	// Now server is ready to listen and verification
	if ((listen(sockinetfd, 5)) != 0) {
		printf("Listen failed...\n");
		exit(0);
	}
	else
		printf("Server listening..\n");
	len = sizeof(cli);

	// Accept the data packet from client and verification
	connfd = accept(sockinetfd, (SA*)&cli, &len);
	if (connfd < 0) {
		printf("server accept failed...\n");
		exit(0);
	}
	else
		printf("server accept the client...\n");

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
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &ev_sock_inet) == -1) {
		printf("epoll_ctl(ADD) failed: errno=%d\n", errno);
		close(epfd);
		close(sockinetfd);
		return EXIT_FAILURE;
	}
	
	
	struct epoll_event events[2];
	int nr;
	
	// prepare struct
	struct app_shared_struct app_data;
	app_data.sockinetfd = connfd;

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
			
			int(*handler)( ) = events[i].data.ptr ;
			ret = handler(app_data);

			if(ret){
				break;
			}      
		}

		if(ret){
			break;
		}
	}

	// After chatting close the socket
	close(connfd);
	close(sockinetfd);
}
