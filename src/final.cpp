#include <fstream>
#include <unistd.h>
#include <pthread.h>
#include <cstring>
#include <unordered_map>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <arpa/inet.h>

const unsigned int 	MAX_EVENTS {32};
const int       	DEFAULT_PORT = 80;

class Server {
private:
	int sock_;
	sockaddr_in sock_addr_;
	std::string directory_;
public:
	Server():sock_(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)), directory_(".") {
		sock_addr_.sin_family = AF_INET;
		sock_addr_.sin_port = htons(DEFAULT_PORT);
 		sock_addr_.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	}
	void set_attr(int, char **);
	void start();
	int get_sock() {return sock_;}	
	std::string get_dir() {return directory_;}
	//char *get_ip() {return inet_ntoa(sock_addr_.sin_addr);}
	int get_port() {return ntohs(sock_addr_.sin_port);}
};

void Server::set_attr(int argc, char **argv) { // fill address, port and directiry of server from arguments
	int rez = 1;
	extern char *optarg;
	while ( (rez = getopt(argc, argv, "h:p:d:")) != -1){
		switch (rez) {
		case 'h': 
			char ip_addr[16];
			strcpy(ip_addr, optarg);
			sock_addr_.sin_addr.s_addr = inet_addr(ip_addr); 
			break;
		case 'p': 
			sock_addr_.sin_port = htons(atoi(optarg));
			break;
		case 'd': 
			std::string tmp(optarg);
			directory_ = tmp;
			break;
       		}
	}
}

void Server::start() {
	bind(sock_, (sockaddr *)(&sock_addr_), sizeof(sock_addr_));
	int flags;
#if defined(O_NONBLOCK) 
	if(-1 == (flags = fcntl(sock_, F_GETFL, 0))) {
 		flags = 0;
	}
 	fcntl(sock_, F_GETFL, 0 | O_NONBLOCK);
#else
 	flags = 1;
 	ioctl(sock_, FIOBIO, &flags);
#endif	
	listen(sock_, SOMAXCONN);
}

class Events {
	struct Client {
		int fd;
		pthread_mutex_t *mtx;
		std::string dir;
	};

private:
	Server srv_;
	int epoll_;
	int master_socket_;
	std::unordered_map<int, Client> clients_;	
	
	void new_client(void);

public:
	Events(Server srv): master_socket_(srv.get_sock()), epoll_(epoll_create1(0)), srv_(srv) {
		epoll_event event;
 		event.data.fd = master_socket_;
 		event.events = EPOLLIN;
		epoll_ctl(epoll_, EPOLL_CTL_ADD, master_socket_, &event);
	}
	void wait_and_do(void);	
	static void *query(void *val);
};

void Events::new_client(void) {
	sockaddr_in sock_addr_tmp {0};
	unsigned int size_tmp;
 	int slave_socket = accept(master_socket_, (struct sockaddr *) &sock_addr_tmp, &size_tmp);

	pthread_mutex_t *mtx = new pthread_mutex_t;
	*mtx = PTHREAD_MUTEX_INITIALIZER;
	pthread_mutex_init(mtx, NULL);
	clients_[slave_socket] = {slave_socket, mtx, srv_.get_dir()};

	int flags;
#if defined(O_NONBLOCK) 
	if(-1 == (flags = fcntl(slave_socket, F_GETFL, 0))) {
 		flags = 0;
	}
 	fcntl(slave_socket, F_GETFL, 0 | O_NONBLOCK);
 #else
 	flags = 1;
 	ioctl(slave_socket, FIOBIO, &flags);
 #endif
	epoll_event event;
	event.data.fd = slave_socket;
	event.events = EPOLLIN;
	epoll_ctl(epoll_, EPOLL_CTL_ADD, slave_socket, &event);
}

void *Events::query(void *val) {
	Client *client = (Client *) val;
	pthread_mutex_t *pmtx = client->mtx;
	pthread_mutex_lock(pmtx);
	
	char buffer[1024];
	int recv_size = recv(client->fd, buffer, sizeof(buffer), MSG_NOSIGNAL);
	if (recv_size == 0 && errno != EAGAIN) {
		shutdown(client->fd, SHUT_RDWR);
		close(client->fd);
	} else if (recv_size > 0) {
		std::string name_file = "";
		size_t i = 4;
		while (i < recv_size && buffer[i] != ' ' && buffer[i] != '?' && buffer[i] & '\r') {
			name_file += buffer[i++];
		}
		std::ifstream fin(client->dir + name_file);
		if (fin.is_open()) {
			std::string str_com = "";
			while(!fin.eof()) {	
				std::string str_send;
				getline(fin, str_send);
				str_com += str_send + "\r\n";
			}
			str_com = "HTTP/1.0 200 OK\r\nContent-Length: " + std::to_string(str_com.length()) + 
				  "\r\nContent-Type: text/html\r\n\r\n" + str_com;
			send(client->fd, str_com.c_str(), str_com.length(), MSG_NOSIGNAL);
		} else {
			std::string head = "HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\nContent-Type: text/html\r\n\r\n";
			send(client->fd, head.c_str(), head.length(), MSG_NOSIGNAL);
		}
	}
	pthread_mutex_unlock(pmtx);
}

void Events::wait_and_do() {
	while (true) {
		epoll_event events[MAX_EVENTS];
		int n_events = epoll_wait(epoll_, events, MAX_EVENTS, -1);
		for (unsigned int i = 0; i < n_events; i++) {
			if (events[i].data.fd == master_socket_) {
				new_client();
			} else {
				pthread_t pth;
				pthread_create(&pth, NULL, this->query, &clients_[events[i].data.fd]);
				pthread_detach(pth);
			}
		}
	}
}


int main(int argc, char **argv) {
	if (fork()) {
		return 0;
	}
	umask(0);
	setsid();

	Server srv;
	srv.set_attr(argc, argv);
	srv.start();
	
	Events evnt(srv);
	evnt.wait_and_do();	

	return 0;
}	


