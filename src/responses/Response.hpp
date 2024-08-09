#ifndef RESPONSE_HPP
# define RESPONSE_HPP

# include <string>
# include <map>
# include <algorithm>

# include "../requests/Request.hpp"
# include "../configuration/Config.hpp"
# include "../utilities/Utils.hpp"

class Request;

class Response {

public:
	Response(const ServerConfig& config, char* buffer, int buffer_size);
	Response(const ServerConfig& config, int errorCode, char* buffer, int buffer_size);
    Response(const Request& req, const ServerConfig& config, char *buffer, int buffer_size);
	Response& operator=(const Response& other);

    void initializeHttpErrors();
    void setStatus(int code);
	int getStatusCode();
    void addHeader(const std::string& key, const std::string& value);

    std::string _headersToString() const;
	void generateResponse(const std::string& filename);
	void generateDirectoryListing(const std::string& directoryPath);
	void generateCGIResponse(const std::string &cgi_response);
	const char *getContent();
	int			getContentLength();
	// const char* toCString();

	/* Exceptions */
	class FileSystemErrorException: public std::exception {
		public:
			FileSystemErrorException(const char *error_msg);
			const char* what() const throw();
		private:
			char _error[256];
	};
	class ContentLengthException: public std::exception {
		public:
			ContentLengthException(const char *error_msg);
			const char* what() const throw();
		private:
			char _error[256];
	};

private:
    std::string 						_httpVersion;
	const ServerConfig&					_config;
    int									_statusCode;
    std::string							_statusMessage;
    std::map<std::string, std::string>	_headers;
    std::map<int, std::string>			_httpErrors;
    const char*							_content;

	char*								_buffer;
	int									_buffer_size;
	int									_content_length;

    // std::string _readFile(const std::string& filename);
    std::string _getMimeType(const std::string& filename);
    std::string _toString(size_t num) const;
	// std::vector<std::string> _chunkFile(const std::string& filename);

    void _handleGetRequest(const Request& req);
    void _handlePostRequest(const Request& req);
    void _handleDeleteRequest(const Request& req);
	void _setError(int code);
    const RouteConfig* _findMostSpecificRouteConfig(const std::string& uri) const;
    void _dispatchMethodHandler(const Request& req);

};

#endif
