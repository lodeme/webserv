#include "Request.hpp"
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <cstdlib>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>

Request::Request(int clientSocket, ServerConfig &config, char *buffer, int buffer_len)
	: _clientSocket(clientSocket), _config(config), _buffer(buffer), _buffer_size(buffer_len) {}

Request::~Request() {}

int Request::parseHeaders() {
    std::string request;
    ssize_t bytesRead;
    bool headersComplete = false;
    size_t headerEnd;

	while (true) {
		bytesRead = recv(_clientSocket, _buffer, _buffer_size - 1, 0);
		if (bytesRead > 0) {
			_buffer[bytesRead] = '\0';
			request += _buffer;
			headerEnd = request.find("\r\n\r\n");
			if (headerEnd != std::string::npos) {
				headersComplete = true;
				break ;
			} else {
				throw ParsingErrorException(BAD_REQUEST, "malformed request");
			}
		} else if (bytesRead == 0) {
			throw SocketCloseException("connection closed by client");
		} else {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				continue ;
			} else {
				throw ParsingErrorException(BAD_REQUEST, "malformed request");
			}
		}
	}
    /*ddavlety*/
    /* Parse headers only if it is complete
    * What we do if it is not ? */
    if (headersComplete) {
        std::istringstream requestStream(request.substr(0, headerEnd));
        std::string line;
        if (std::getline(requestStream, line)) {
            _parseRequestLine(line);
        }
        while (std::getline(requestStream, line) && line != "\r") {
            _parseHeader(line);
        }
    } else {
		throw ParsingErrorException(BAD_REQUEST, "malformed request");
	}

	/* Check if method allowed to certain URI */


/***********************************/
	// if (this->getUri() == "/exit")
	// 	exit (0);
/***********************************/
	return bytesRead;
}

int Request::parseBody(int bytesRead) {
	std::string content_length = getHeader("Content-Length");
    int contentLength = atoi(content_length.c_str());
    std::string content_type = getHeader("Content-Type");

    if (contentLength > _config.body_limit) {
        throw ParsingErrorException(CONTENT_LENGTH, "content length is above limit");
	}
	char *body_buffer = utils::strstr(_buffer, "\r\n\r\n", bytesRead);
	if (body_buffer) {
		body_buffer += 4;
		bytesRead -= body_buffer - _buffer;
	}
	if (body_buffer && *body_buffer == 0) {
		read:
		bytesRead = recv(_clientSocket, _buffer, _buffer_size - 1, 0);
		if (bytesRead == 0)
			throw SocketCloseException("connection closed by client");
		if (bytesRead < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				goto read;
			} else {
				throw ParsingErrorException(BAD_REQUEST, "malformed request");
			}
		}
		body_buffer = _buffer;
	}
	if (content_type.find("multipart/form-data") != content_type.npos) {
		_readBodyFile(body_buffer, bytesRead);
	} else if (getHeader("Transfer-Encoding") == "chunked"){
		_readBodyChunked(body_buffer, bytesRead);
	} else {
		_readBody(body_buffer, bytesRead);
	}
	return bytesRead;
}

void Request::_parseRequestLine(const std::string& line) {
    std::istringstream iss(line);
    iss >> _method >> _uri >> _httpVersion;
}

void Request::_parseHeader(const std::string& line) {
    size_t colonPos = line.find(':');
    if (colonPos != std::string::npos) {
        std::string key = line.substr(0, colonPos);
        std::string value = line.substr(colonPos + 1);
        size_t first = value.find_first_not_of(" \t");
        size_t last = value.find_last_not_of(" \t\r\n");

        if (first != std::string::npos && last != std::string::npos) {
            value = value.substr(first, last - first + 1);
        }
        key = utils::toLowerCase(key);
        _headers[key] = value;
    }
}

void Request::_readBody(const char *buffer, ssize_t bytesRead) {
	_body += buffer;
	(void)bytesRead;
}

void Request::_readBodyChunked(const char *buffer, ssize_t bytesRead) {
	int read_len;
	char *stream;

	stream = (char *)buffer;
	std::string	file_name = utils::chunkFileName(getSocket());
	if (!access(file_name.c_str(), W_OK | R_OK))
		std::cout << BLACK << "File exists with permissions" << RESET << std::endl;
	int file_fd = open(file_name.c_str(), O_WRONLY | O_CREAT | O_NONBLOCK | O_APPEND, 0600);
	if (file_fd < 0)
		throw ParsingErrorException(FILE_SYSTEM, "chunk temp file open failed");
	while (*stream) {
		read_len = atoi(stream);
		if (read_len == 0) {
			close(file_fd);
			utils::saveFile(file_name, _config, getUri());
			return ;
		}
		stream = utils::strstr(stream, "\r\n", bytesRead) + 2;
		bytesRead -= stream - buffer;
		buffer = stream;
		write(file_fd, stream, read_len);
		stream = utils::strstr(stream, "\r\n", bytesRead) + 2;
		bytesRead -= stream - buffer;
	}
	close(file_fd);
}

void Request::_readBodyFile(char *buffer, ssize_t bytesRead)
{
	char *start_pos;
	std::string stream;

	std::string boundary = getHeader("Content-Type");
	int pos = boundary.find("boundary=");
	boundary = boundary.substr(pos + 9);
	boundary = "--" + boundary;

	start_pos = utils::strstr(buffer, (char *)boundary.c_str(), bytesRead);
	if (start_pos) {
		/* Find start of data */
		start_pos += boundary.length() + 2;

		/* Start parsing data header */
		std::string unique_filename;
		{
			const char *header_pos = start_pos;
			start_pos = utils::strstr(start_pos, (const char *)"\r\n\r\n", bytesRead - (start_pos - buffer)) + 4;
			std::string headers = header_pos;
			headers = headers.substr(0, start_pos - header_pos);

			/* Does it alway have filename in header? */
			std::string new_file_name = headers.substr(headers.find("filename=") + 9);
			new_file_name = new_file_name.substr(0, new_file_name.find('\r'));
			new_file_name.erase(new_file_name.begin());
			new_file_name.erase(new_file_name.end() - 1);
			unique_filename = _config.root + getUri() + new_file_name;
			new_file_name = unique_filename;
			int i = 1;
			while (access(unique_filename.c_str(), F_OK) == 0) {
				std::stringstream ss;
				ss << i;
				std::string value;
				ss >> value;
				std::string extension = utils::getFileExtension(unique_filename);
				unique_filename = new_file_name.substr(0, new_file_name.find(extension) - 1) + " (" + value + ")." + extension;
				i++;
			}
		}
		int file_fd = open(unique_filename.c_str(), O_WRONLY | O_CREAT | O_NONBLOCK | O_APPEND, 0600);
		if (file_fd < 0)
			throw ParsingErrorException(FILE_SYSTEM, strerror(errno));
		/* ******************************************** */

		/* Take data body length */
		/* Checking boundaries */
		{
			size_t len = bytesRead - (start_pos - buffer);
			std::string boundary_end = boundary + "--";
			char *boundary_pos = utils::strstr(start_pos, boundary.c_str(), len);
			char *boundary_end_pos = utils::strstr(start_pos, boundary_end.c_str(), len);
			/* Check if this chunk contains another data */
			if (boundary_pos && boundary_pos != boundary_end_pos) {
				write(file_fd, start_pos, len - (bytesRead - (boundary_pos - buffer)));
				_readBodyFile(boundary_pos, bytesRead - (boundary_pos - buffer));
			}
			/* Check if this chunk contains EOF */
			else if (boundary_end_pos) {
				len -= bytesRead - (boundary_end_pos - buffer) + 6;
				write(file_fd, start_pos, len);
			}
			/* read the rest */
			else {
				write(file_fd, start_pos, len);
				while (bytesRead > 0) {
					bytesRead = recv(_clientSocket, _buffer, _buffer_size - 1, 0);
					if (bytesRead == 0)
						throw SocketCloseException("connection closed by client");
					if (bytesRead < 0) {
						if (errno == EAGAIN || errno == EWOULDBLOCK) {
                			continue;
						} else {
							throw ParsingErrorException(BAD_REQUEST, "malformed request");
						}
					}
					char *boundary_pos = utils::strstr(_buffer, boundary.c_str(), bytesRead);
					char *boundary_end_pos = utils::strstr(_buffer, boundary_end.c_str(), bytesRead);
					/* Check if this chunk contains another data */
					if (boundary_pos && boundary_pos != boundary_end_pos) {
						write(file_fd, _buffer, (boundary_pos - _buffer));
						_readBodyFile(boundary_pos, bytesRead - (boundary_pos - _buffer));
						break ;
					}
					/* Check if this chunk contains EOF */
					else if (boundary_end_pos) {
						bytesRead = (boundary_end_pos - _buffer) - 6;
						write(file_fd, _buffer, bytesRead);
						break ;
					}
					write(file_fd, _buffer, bytesRead);
				}
			}
		}
		close (file_fd);
	}
}

void Request::addHeader(const std::string& key, const std::string& value) {
    std::string lowercase_key;

    lowercase_key = key;
    lowercase_key = utils::toLowerCase(lowercase_key);
    _headers[lowercase_key] = value;
}

std::string Request::getMethod() const {
    return _method;
}

std::string Request::getUri() const {
    return _uri;
}

std::string Request::getHttpVersion() const {
    return _httpVersion;
}

std::string Request::getHeader(const std::string& key) const {
    std::string lowercase_key;

    lowercase_key = key;
    lowercase_key = utils::toLowerCase(lowercase_key);
    std::map<std::string, std::string>::const_iterator it = _headers.find(lowercase_key);
    if (it != _headers.end()) {
        return it->second;
    }
    return "";
}

std::string Request::getHost() const {
    std::map<std::string, std::string>::const_iterator it = _headers.find("host");
    if (it != _headers.end()) {
        std::string host = it->second;
        std::string::size_type colonPos = host.find(':');
        if (colonPos != std::string::npos) {
            return host.substr(0, colonPos);
        }
        return host;
    }
    return "";
}

const std::map<std::string, std::string>& Request::getHeaders() const {
    return _headers;
}

std::string Request::getBody() const {
    return _body;
}

int Request::getSocket() const
{
    return _clientSocket;
}

std::ostream& operator<<(std::ostream& os, const Request& request) {
    os << "HTTP Request Details:" << std::endl;
    os << "Socket file descriptor: " << request._clientSocket << std::endl;
    os << "Method: " << request.getMethod() << std::endl;
    os << "URI: " << request.getUri() << std::endl;
    os << "HTTP Version: " << request.getHttpVersion() << std::endl;
    os << "Headers:" << std::endl;

	std::map<std::string, std::string> headers = request.getHeaders();

	std::map<std::string, std::string>::const_iterator it;
	for (it = headers.begin(); it != headers.end(); ++it) {
		os << " " << it->first << ": " << it->second << std::endl;
	}
    os << "Body: " << request.getBody() << std::endl;
    return os;
}

Request::ParsingErrorException::ParsingErrorException(ErrorType type, const char *error_msg) {
	this->type = type;
    std::memset(_error, 0, 256);
    strncpy(_error, "Request parsing error: ", 24);
	strncat(_error, error_msg, 256 - strlen(_error) - 1);;
}

const char *Request::ParsingErrorException::what() const throw() {
	return _error;
}

Request::SocketCloseException::SocketCloseException(const char *error_msg) {
	std::memset(_error, 0, 256);
    strncpy(_error, "Socket closed: ", 27);
    strncat(_error, error_msg, 256 - strlen(_error) - 1);;
}

const char *Request::SocketCloseException::what() const throw() {
    return _error;
}

std::string Request::getQueryString() const {
    size_t queryStart = _uri.find('?');
    if (queryStart != std::string::npos) {
        return _uri.substr(queryStart + 1);
    }
    return "";
}

std::string Request::RemoveQueryString(std::string uri) const {
    size_t queryEnd = uri.find('?');
    if (queryEnd != std::string::npos) {
        return uri.substr(0, queryEnd);
    }
    return "";
}


bool Request::isTargetingCGI() const {
    const std::string& uri = getUri();
    size_t queryPos = uri.find('?');
    std::string path = (queryPos != std::string::npos) ? uri.substr(0, queryPos) : uri;

    std::string lowerPath = path;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);

    return (lowerPath.find("/cgi/") == 0 ||
            lowerPath.rfind(".cgi") == lowerPath.length() - 4 ||
            lowerPath.rfind(".php") == lowerPath.length() - 4 ||
            lowerPath.rfind(".py") == lowerPath.length() - 3);
}


std::string Request::getScriptPath() const {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        std::cerr << "Error getting current working directory: " << strerror(errno) << std::endl;
        throw std::runtime_error("Failed to get current working directory");
    }

    std::string basePath = std::string(cwd) + "/web/cgi";
    std::string scriptName = _uri.substr(_uri.rfind('/'));

    std::string fullPath = basePath + scriptName;
    std::cout << "Constructed CGI script path: " << fullPath << std::endl;

    return RemoveQueryString(fullPath);
}
