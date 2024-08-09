#ifndef UTILS_HPP
# define UTILS_HPP

# include <iostream>
# include <sstream>
# include <fcntl.h>
# include <unistd.h>
# include <dirent.h>
# include <ctime>
# include <stdlib.h>
# include <string>
# include <dirent.h>
# include <sys/types.h>
# include <sys/stat.h>
# include <ctime>

# include "../server/Server.hpp"
# include "../configuration/Config.hpp"

namespace utils {
	std::string	&toLowerCase(std::string &string);
	std::string	buildPath(int socket, const char *folder_name);
	bool		checkChunkFileExistance(const std::string &file_name);
	bool		deleteFile(const std::string &file_name);
	std::string	getFileExtension(const std::string &file);
	std::string	saveFile(const std::string &file_name, const ServerConfig &config, const std::string uri);
	std::string	chunkFileName(int socket);
	char		*strstr(const char *haystack, const char *needle, ssize_t len);
	int			stoi(const std::string &str);
	// std::string generateDirectoryListing(const std::string& directoryPath);
	std::time_t getCurrentTime();
	std::string to_string(int value);
	bool		fileExists(const std::string& path);
}

#endif
