#ifndef Server_HPP
# define Server_HPP

# include <poll.h>
# include <vector>
# include <unistd.h>
# include <netinet/in.h>
# include <iostream>
# include <sys/errno.h>
# include <fcntl.h>
# include <sstream>
# include <ctime>
# include <cstdio>

# include "../responses/Response.hpp"
# include "../requests/Request.hpp"
# include "../configuration/Config.hpp"
# include "../cgi/CGI.hpp"

# include "../include/debug.hpp"

class Request;
class Response;

typedef std::vector<std::string> HostList;

# define BACKLOG 3
# define TEMP_FILES_DIRECTORY "tmp/"
# define REQUEST_TIMEOUT 10
# define RESPONSE_MAX_BODY_SIZE 8000000

struct Stream {
	std::string boundary;
	std::string unique_filename;
};

class Server {
	public:
		Server();
		Server(const HostList &hosts, short port);
		~Server();

		/*Exceptions*/
		class PollingErrorException: public std::exception {
			private:
				char _error[256];
			public:
				PollingErrorException(const char *error_msg);
				const char* what() const throw();
		};
		class InitialisationException: public std::exception {
			private:
				char _error[256];
			public:
				InitialisationException(const char *error_msg);
				const char* what() const throw();
		};
		class ListenErrorException: public std::exception {
			private:
				char _error[256];
			public:
				ListenErrorException(const char *error_msg);
				const char* what() const throw();
		};
		class RuntimeErrorException: public std::exception {
			private:
				char _error[256];
			public:
				RuntimeErrorException(const char *error_msg);
				const char* what() const throw();
		};
		/* Initialize server */
		void	initEndpoint(const HostList &hosts, short port, const ServerConfig &config);
		/* Socket fucntions */
		void	addPollfd(int socket_fd, short events);
		void	pollfds();
		void	pollLoop();

		/* Utility functions */
		size_t		getSocketsSize() const;
		void		listenPort(int backlog);
		void		setBuffer(char *buffer, int buffer_size);
		void		setResBuffer(char *buffer, int buffer_size);

		/* Getters */
		const HostList				&getHostList() const;
		short						getPort() const;
		int							getMainSocketFd() const;
		const std::vector<pollfd>	&getSockets() const;

		/* Start all servers */
		static void RUN(std::vector<Server>);

	private:
		/*Variables*/
		short						_port;
		HostList					_hosts;
		int							_main_socketfd;
		sockaddr_in					_address;
		int							_address_len;
		std::vector<pollfd>			_fds;
		ServerConfig				_config;
		std::map<int, std::time_t>	_request_time;
		char*						_buffer;
		int							_buffer_size;
		std::map<int, Stream>		_requests;

		char*						_res_buffer;
		int							_res_buffer_size;

		/*Functions*/
		void _push(pollfd client_pollfd); //called when setPollfd is called
		void _setSocketOpt();
		void _setSocketNonblock();
		void _bindSocketName();
		void _setRequestTime(int client_socket);
		bool _checkRequestTimeout(int client_socket);
		void _cleanChunkFiles(int client_socket);
		void _addNewClient(int client_socket);
		void _requestHandling(Request &req, Response &res);
		void _serveExistingClient(int client_socket, size_t i);
};

std::ostream &operator<<(std::ostream &os, const Server &server);

#endif
