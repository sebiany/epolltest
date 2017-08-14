#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <iostream>
#include <string.h>
using namespace std;

#define MAX_BUFFER_SIZE 5
#define MAX_EPOLL_EVENTS 20
#define EPOLL_LT 0
#define EPOLL_ET 1
#define FD_BLOCK 0
#define FD_NONBLOCK 1

int set_nonblock(int fd) {
	int old_flags = fcntl(fd, F_GETFL);
	fcntl(fd, F_SETFL, old_flags | O_NONBLOCK);
	return old_flags;
}

void addfd_to_epoll(int epoll_fd, int fd, int epoll_type, int block_type) {
	struct epoll_event ep_event;
	ep_event.data.fd = fd;
	ep_event.events = EPOLLIN;

	if (epoll_type == EPOLL_ET)
		ep_event.events |= EPOLLET;

	if (block_type == FD_NONBLOCK)
		set_nonblock(fd);

	epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ep_event);
}

void epoll_lt(int sockfd) {
	char buffer[MAX_BUFFER_SIZE];
	int ret;

	memset(buffer, 0, MAX_BUFFER_SIZE);
	cout << "开始recv()..." << endl;
	ret = recv(sockfd, buffer, MAX_BUFFER_SIZE, 0);
	cout << "ret=" << ret << endl;

	if (ret > 0) {
		cout << "收到消息：" << buffer << ", ";
		cout << "消息长度：" << ret << endl;
	} else {
		if (ret == 0)
			cout << "客户端主动关闭" << endl;
		close(sockfd);
	}

	cout << "LT处理结束" << endl;
}

void epoll_et_loop(int sockfd) {
	char buffer[MAX_BUFFER_SIZE];
	int ret;

	cout << "带循环的ET读取数据开始。。。" << endl;
	while (true) {
		memset(buffer, 0, MAX_BUFFER_SIZE);
		ret = recv(sockfd, buffer, MAX_BUFFER_SIZE, 0);
		if (ret == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				cout << "循环读完所有数据" << endl;
				break;
			}
			close(sockfd);
			break;
		} else if (ret == 0) {
			cout << "客户端主动关闭请求" << endl;
			close(sockfd);
			break;
		} else {
			cout << "收到消息：" << buffer << ", ";
			cout << "消息长度：" << ret << endl;
		}
	}
	cout << "带循环的ET处理结束" << endl;
}

void epoll_et_nonloop(int sockfd) {
	char buffer[MAX_BUFFER_SIZE];
	int ret;

	cout << "不带循环的ET模式开始读取数据" << endl;
	memset(buffer, 0, MAX_BUFFER_SIZE);
	ret = recv(sockfd, buffer, MAX_BUFFER_SIZE, 0);
	if (ret > 0) {
		cout << "收到消息：" << buffer << ", ";
		cout << "消息长度：" << ret << endl;
	} else {
		if (ret == 0)
			cout << "客户端主动关闭请求" << endl;
		close(sockfd);
	}

	cout << "不带循环的ET模式处理结束" << endl;
}

void epoll_process(int epollfd, struct epoll_event *events, int number, int sockfd, int epoll_type, int block_type) {
	struct sockaddr_in client_addr;
	socklen_t client_addrlen;
	int newfd, connfd;
	int i;

	for (int i = 0; i < number; i++) {
		newfd = events[i].data.fd;
		if (newfd == sockfd) {
			cout << "=================================新一轮accept()===================================" << endl;
			cout << "accept()开始" << endl;

			cout << "开始休眠3秒" << endl;
			sleep(3);
			cout << "3秒结束" << endl;

			client_addrlen = sizeof(client_addr);
			connfd = accept(sockfd, (struct sockaddr *)&client_addr, &client_addrlen);
			if (errno == ECONNABORTED || errno == EPROTO)
				continue;
			cout << "connfd=" << connfd << endl;

			addfd_to_epoll(epollfd, connfd, epoll_type, block_type);
			cout << "accept()结束" << endl;
		} else if (events[i].events & EPOLLIN) {
			if (epoll_type == EPOLL_LT) {
				cout << "============================>水平触发开始..." << endl;
				epoll_lt(newfd);
			} else if (epoll_type == EPOLL_ET) {
				cout << "============================>边缘触发开始..." << endl;
				epoll_et_loop(newfd);
				// epoll_et_nonloop(newfd);
			}
		} else 
			cout << "其他事件发生" << endl;
	}
}

void err_exit(const char *msg) {
	perror(msg);
	exit(1);
}

int create_socket(const char *ip, const int port_number) {
	struct sockaddr_in server_addr;
	int sockfd;
	int reuse = 1;

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port_number);

	if (inet_pton(PF_INET, ip, &server_addr.sin_addr) == -1)
		err_exit("inet_pton() error");

	if ((sockfd = socket(PF_INET, SOCK_STREAM, 0)) == -1) 
		err_exit("socket() error");

	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1)
		err_exit("setsockopt() error");

	if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
		err_exit("bind() error");
	}

	if (listen(sockfd, 5) == -1)
		err_exit("listen() error");

	return sockfd;
}



int main(int argc, const char* argv[]) {
	if (argc < 3) {
		cerr << "usage:" << argv[0] << " ip_address port_number" << endl;
		exit(1);
	}

	int sockfd, epollfd, number;

	sockfd = create_socket(argv[1], atoi(argv[2]));
	struct epoll_event events[MAX_EPOLL_EVENTS];

	if ((epollfd = epoll_create(10)) == -1)
		err_exit("epoll_create() error");

	// addfd_to_epoll(epollfd, sockfd, EPOLL_ET, FD_BLOCK);
	addfd_to_epoll(epollfd, sockfd, EPOLL_ET, FD_NONBLOCK);

	while (1) {
		number = epoll_wait(epollfd, events, MAX_EPOLL_EVENTS, -1);
		cout << "***number is " << number << endl;
		if (number == -1)
			err_exit("epoll_wait() error");
		else {
			epoll_process(epollfd, events, number, sockfd, EPOLL_ET, FD_NONBLOCK);
			// epoll_process(epollfd, events, number, sockfd, EPOLL_LT, FD_BLOCK);

		}
	}

	close(sockfd);
	return 0;
}