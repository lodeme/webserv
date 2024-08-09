#include "Server.hpp"

Server::Server(): _buffer(0), _res_buffer(0) {
	/*ddavlety*/
	/* Clean up temp files before server start up */
	/* If there is already running server behaviour is undefined */
	/* look issue #46 */
	DIR* dir = opendir(TEMP_FILES_DIRECTORY);
	if (dir == NULL) {
		std::string error_msg;
		error_msg += strerror(errno);
		error_msg += ": failed to open directory";
		throw InitialisationException(error_msg.c_str());
	}
	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_type == DT_REG) {
			std::string filePath = std::string(TEMP_FILES_DIRECTORY) + entry->d_name;
			std::remove(filePath.c_str());
		}
	}
	closedir(dir);
}

Server::Server(const HostList &hosts, short port): _port(port), _hosts(hosts), _buffer(0) {
	_main_socketfd = socket(PF_INET, SOCK_STREAM, 0);
	if (_main_socketfd < 0) {
		throw InitialisationException("server socket endpoint is not created");
	}
	_address.sin_family = AF_INET;
	_address.sin_addr.s_addr = INADDR_ANY;
	_address.sin_port = htons(port);
	_address_len = sizeof(_address);
	addPollfd(_main_socketfd, POLLIN);
	_setSocketOpt();
	_setSocketNonblock();
	_bindSocketName();
}

Server::~Server() {
	for (size_t i = 0; i < getSocketsSize(); ++i) {
		close(_fds[i].fd);
	}
	/* If there is running server behaviour is undefined */
	/* look issue #46 */
	if (_buffer) {
        delete[] _buffer;
    }
    if (_res_buffer) {
        delete[] _res_buffer;
    }
    DIR* dir = opendir(TEMP_FILES_DIRECTORY);
    if (dir) {
        dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_REG) {
                std::string filePath = std::string(TEMP_FILES_DIRECTORY) + entry->d_name;
                std::remove(filePath.c_str());
            }
        }
        closedir(dir);
    }
}

void Server::_addNewClient(int client_socket)
{
	int new_client_socket = accept(_main_socketfd, (struct sockaddr *)&_address, (socklen_t*)&_address_len);
	if (new_client_socket < 0) {
			throw PollingErrorException(strerror(errno));
		}
	// Set socket to non-blocking mode
	fcntl(client_socket, F_SETFL, O_NONBLOCK);
	addPollfd(new_client_socket, POLLIN);
	_setRequestTime(new_client_socket);
	std::cout << "New connection established on fd: " << client_socket << std::endl;

}

void Server::_requestHandling(Request &req, Response &res)
{
	/* Parse request headers */
	int bytesRead = req.parseHeaders();
	/*************************/
	if (req.isTargetingCGI()) {
		std::string scriptPath = req.getScriptPath();
		std::cout << "Attempting to execute CGI script at path: " << scriptPath << std::endl;

		if (!utils::fileExists(scriptPath)) {
			std::cerr << "CGI script not found at path: " << scriptPath << std::endl;
			res = Response(_config, 404, _res_buffer, _res_buffer_size);
		} else {
			CGIHandler cgiHandler(scriptPath, req, _config);
			std::string cgiOutput = cgiHandler.execute();
			if (cgiOutput.empty()) {
				std::cerr << "CGI script execution failed or exited with error status" << std::endl;
				res = Response(_config, 500, _res_buffer, _res_buffer_size);
			} else {
				res.setStatus(200);
				res.addHeader("Content-Type", "text/html");
				res.generateCGIResponse(cgiOutput);
				std::cout << "CGI response generated successfully." << std::endl;
			}
		}
	} else {
		/* Configure response */
		res = Response(req, _config, _res_buffer, _res_buffer_size);
		/*********************/
		/* Parse request body */
		if (res.getStatusCode() < 300 && req.getMethod() == "POST") {
			req.parseBody(bytesRead);
		}
		/*********************/
	}

}

void Server::_serveExistingClient(int client_socket, size_t i)
{
	Request req(client_socket, _config, _buffer, _buffer_size);
	Response res(req, _config, _res_buffer, _res_buffer_size);
	/* Request handling */
	try {
		_requestHandling(req, res);
	} catch (Request::SocketCloseException &e) {
		std::cout << e.what() << std::endl;
		_cleanChunkFiles(client_socket);
		_fds.erase(_fds.begin() + i);
		return ;
	} catch (Request::ParsingErrorException& e) {
		if (e.type == Request::BAD_REQUEST)
			res = Response(_config, 400, _res_buffer, _res_buffer_size);
		else if (e.type == Request::CONTENT_LENGTH)
			res = Response(_config, 413, _res_buffer, _res_buffer_size);
		else if (e.type == Request::FILE_SYSTEM)
			res = Response(_config, 500, _res_buffer, _res_buffer_size);
		_cleanChunkFiles(client_socket);
	}
	/*******************/
	if (res.getStatusCode() == -1)
		res = Response(_config, 500, _res_buffer, _res_buffer_size);
	/* Send response to client */
	ssize_t sent = send(client_socket, res.getContent(), res.getContentLength(), MSG_DONTWAIT);
	std::cout << res.getContentLength() - sent << std::endl;
	/**************************/
	int response_code = res.getStatusCode();
	if (response_code >= 500 || response_code >= 400) {
		_cleanChunkFiles(client_socket);
		close(req.getSocket());
		_fds.erase(_fds.begin() + i);
	}
}

void Server::addPollfd(int socket_fd, short events)
{
    pollfd fd;
	fd.fd = socket_fd;
	fd.events = events;
	fd.revents = 0;
	_push(fd);
}

void Server::_push(pollfd client_pollfd) {
	_fds.push_back(client_pollfd);
}

void Server::setBuffer(char *buffer, int buffer_size)
{
	_buffer = buffer;
	_buffer_size = buffer_size;
}

void Server::setResBuffer(char *buffer, int buffer_size)
{
	_res_buffer = buffer;
	_res_buffer_size = buffer_size;
}

void Server::initEndpoint(const HostList &hosts, short port, const ServerConfig &config) {
	_port = port;
	_hosts = hosts;
	_config = config;
	_main_socketfd = socket(PF_INET, SOCK_STREAM, 0);
	if (_main_socketfd < 0) {
		throw InitialisationException("server socket endpoint is not created");
	}
	_address.sin_family = AF_INET;
	_address.sin_addr.s_addr = INADDR_ANY;
	_address.sin_port = htons(port);
	_address_len = sizeof(_address);
	addPollfd(_main_socketfd, POLLIN);
	_setSocketOpt();
	_setSocketNonblock();
	_bindSocketName();
}

void Server::pollfds() {
	int poll_count;

	poll_count = poll(_fds.data(), _fds.size(), 0);
	for (size_t i = 0; i < _fds.size(); ++i)
	{
		if (_fds[i].fd != _main_socketfd && _checkRequestTimeout(_fds[i].fd)) {
			// Response res(_config, 408);
			// const char* response = res.toCString();
			/* Debug print */
			// std::cout << CYAN << "Response sent:" << std::endl << response << RESET << std::endl;

			// send(_fds[i].fd, response, strlen(response), MSG_DONTWAIT);
			// close(_fds[i].fd);
			// _fds.erase(_fds.begin() + i);
			// _cleanChunkFiles(_fds[i].fd);
		}
	}

	if (poll_count == -1) {
		close(_main_socketfd);
		throw PollingErrorException(strerror(errno));
	}
}

size_t Server::getSocketsSize() const {
	return _fds.size();
}

void Server::pollLoop() {
	for (size_t i = 0; i < getSocketsSize(); ++i) {
		if (_fds[i].revents & POLLERR) {
			close(_main_socketfd);
			throw PollingErrorException("error from poll() function");
		}
		if (_fds[i].revents & POLLIN) {
			int client_socket = _fds[i].fd;
			if (client_socket != _main_socketfd)
				_setRequestTime(client_socket);
			if (client_socket == _main_socketfd) {
				_addNewClient(client_socket);
			} else {
				_serveExistingClient(client_socket, i);
			}
		}
	}
}

void Server::_setSocketOpt() {
	int opt = 1;
	if (setsockopt(_main_socketfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt))) {
		throw InitialisationException(strerror(errno));
	}
}

void Server::_setSocketNonblock() {
	int flags = fcntl(_main_socketfd, F_GETFL, 0); //getting current flags
	if (flags == -1) {
		throw InitialisationException(strerror(errno));
	}
	if (fcntl(_main_socketfd, F_SETFL, flags | O_NONBLOCK) == -1) {
		throw InitialisationException(strerror(errno));
	}
}

void Server::_bindSocketName() {
	if (bind(_main_socketfd, (struct sockaddr *)&_address, sizeof(_address)) < 0) {
		throw InitialisationException(strerror(errno));
	}
}

void Server::_setRequestTime(int client_socket)
{
	_request_time[client_socket] = utils::getCurrentTime();
}

bool Server::_checkRequestTimeout(int client_socket)
{
	if (difftime(utils::getCurrentTime(), _request_time[client_socket]) >= REQUEST_TIMEOUT)
		return true;
	return false;
}

void Server::_cleanChunkFiles(int client_socket)
{
	if (utils::checkChunkFileExistance(utils::buildPath(client_socket, TEMP_FILES_DIRECTORY)))
		utils::deleteFile(utils::buildPath(client_socket, TEMP_FILES_DIRECTORY));
}

void Server::listenPort(int backlog) {
	if (listen(_main_socketfd, backlog) < 0) {
		ListenErrorException(strerror(errno));
	}
}

const HostList &Server::getHostList() const {
	return _hosts;
}

short Server::getPort() const {
	return _port;
}

int Server::getMainSocketFd() const {
	return _main_socketfd;
}

const std::vector<pollfd> &Server::getSockets() const {
	return _fds;
}

void Server::RUN(std::vector<Server> servers) {
	for (size_t i = 0; i < servers.size(); ++i) {
		{
			int buffer_size = servers[i]._config.body_limit + 10 * 1024;
			char *buffer = new char[buffer_size];
			servers[i].setBuffer(buffer, buffer_size);
		}
		{
			int buffer_size = RESPONSE_MAX_BODY_SIZE + 10 * 1024;
			char* buffer = new char[buffer_size];
			servers[i].setResBuffer(buffer, buffer_size);
		}
		servers[i].listenPort(BACKLOG);
		std::cout << "Server 1 is listening on port "
			<< servers[i].getPort() << std::endl;
	}
	while (true)
	{
		for (size_t i = 0; i < servers.size(); ++i) {
			servers[i].pollfds();
			servers[i].pollLoop();
		}
	}
}

/*Exceptions*/

Server::PollingErrorException::PollingErrorException(const char *error_msg) {
	strncpy(_error, "Pooling error: ", 16);
	_error[sizeof(_error) - 1] = '\0';
	strncat(_error, error_msg, 256 - strlen(_error) - 1);;
}

const char *Server::PollingErrorException::what() const throw() {
	return _error;
}

Server::InitialisationException::InitialisationException(const char *error_msg) {
	std::memset(_error, 0, 256);
	strncpy(_error, "Initialisation error: ", 23);
	_error[sizeof(_error) - 1] = '\0';
	strncat(_error, error_msg, 256 - strlen(_error) - 1);;
}

const char *Server::InitialisationException::what() const throw() {
	return _error;
}

Server::ListenErrorException::ListenErrorException(const char *error_msg) {
	std::memset(_error, 0, 256);
	strncpy(_error, "Listening error: ", 18);
	_error[sizeof(_error) - 1] = '\0';
	strncat(_error, error_msg, 256 - strlen(_error) - 1);;
}

const char *Server::ListenErrorException::what() const throw() {
	return _error;
}

Server::RuntimeErrorException::RuntimeErrorException(const char *error_msg) {
	std::memset(_error, 0, 256);
	strncpy(_error, "Runtime error: ", 16);
	_error[sizeof(_error) - 1] = '\0';
	strncat(_error, error_msg, 256 - strlen(_error) - 1);;
}

const char *Server::RuntimeErrorException::what() const throw() {
	return _error;
}

std::ostream &operator<<(std::ostream &os, const Server &server) {
	os << "------------------------------" << std::endl;
	os << "Server data: " << std::endl;
	os << "Server port: " << server.getPort() << std::endl;
	os << "Server hostnames:";
	if (server.getHostList().size() == 0)
		os << "empty" << std::endl;
	for (size_t i = 0; i < server.getHostList().size(); ++i)
		os << " " << server.getHostList()[i] << std::endl;
	os << "Server sockets:";
	if (server.getSocketsSize() == 0)
		os << "empty" << std::endl;
	for (size_t i = 0; i < server.getSocketsSize(); ++i)
		os << server.getSockets()[i].fd << std::endl;
	os << "end of server data" << std::endl;
	os << "------------------------------" << std::endl;
	return os;
}
