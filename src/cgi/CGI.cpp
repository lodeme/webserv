#include "CGI.hpp"
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>

CGIHandler::CGIHandler(const std::string& path, const Request& req, const ServerConfig& conf)
    : scriptPath(path), serverConfig(conf), request(req) {
    setupEnvironment();
}

CGIHandler::~CGIHandler() {
    if (envp) {
        for (int i = 0; envp[i] != NULL; i++) {
            delete [] envp[i];
        }
        delete[] envp;
    }
}

void CGIHandler::setupEnvironment() {
    std::string contentLength = utils::to_string(request.getBody().length());

    environment["REQUEST_METHOD"] = request.getMethod();
    environment["SCRIPT_NAME"] = scriptPath;
    environment["QUERY_STRING"] = request.getQueryString();
    environment["CONTENT_LENGTH"] = contentLength;
    environment["CONTENT_TYPE"] = request.getHeader("Content-Type");
	environment["SERVER_PORT"] = utils::to_string(serverConfig.port);
	environment["SERVER_PROTOCOL"] = request.getHttpVersion();
    environment["SERVER_PROTOCOL"] = "HTTP/1.1";
    environment["PATH_INFO"] = request.getUri();
	environment["REDIRECT_STATUS"] = "200";

    std::vector<std::string> envStrings;
    envp = new char*[environment.size() + 1];
    int i = 0;
    for (std::map<std::string, std::string>::const_iterator it = environment.begin(); it != environment.end(); ++it) {
        std::string env = it->first + "=" + it->second;
        envStrings.push_back(env);
        envp[i] = new char[env.size() + 1];
        std::copy(env.begin(), env.end(), envp[i]);
        envp[i][env.size()] = '\0';
        i++;
    }
    envp[i] = NULL;
}

std::string extractFileExtension(const std::string& path) {
    std::size_t lastDot = path.find_last_of('.');
    if (lastDot == std::string::npos)
		return "";
    return path.substr(lastDot + 1);
}

std::string CGIHandler::execute() {
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        std::cerr << "Failed to create pipe: " << strerror(errno) << std::endl;
        throw std::runtime_error("Failed to create pipe");
    }

    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "Failed to fork process: " << strerror(errno) << std::endl;
        throw std::runtime_error("Failed to fork process");
    }

    if (pid == 0) {  // Child process
        close(pipe_fds[0]);  // Close the read end in the child
        if (dup2(pipe_fds[1], STDOUT_FILENO) == -1) {
            std::cerr << "dup2 failed: " << strerror(errno) << std::endl;
            exit(EXIT_FAILURE);
        }
        close(pipe_fds[1]);  // No longer need this after dup2
		const char* python_path = "/usr/bin/python3";
		char* const args[] = {
			const_cast<char*>(python_path),
			const_cast<char*>(scriptPath.c_str()),
			NULL
		};

		execve(python_path, args, envp);
		perror("execve failed");  // Execve doesn't return on success
        exit(EXIT_FAILURE);
    } else {  // Parent process
        close(pipe_fds[1]);  // Close the write end in the parent
        std::string output;
        char buffer[1024];
        ssize_t bytesRead;

         while ((bytesRead = read(pipe_fds[0], buffer, sizeof(buffer) - 1)) > 0) {
            if (bytesRead == -1) {
                std::cerr << "Read error: " << strerror(errno) << std::endl;
                close(pipe_fds[0]);
                return "Status code 500 Internal Server Error\r\n";
            }
            buffer[bytesRead] = '\0';
            output.append(buffer);
        }
        close(pipe_fds[0]);

        int status;
        waitpid(pid, &status, 0);

        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            std::cerr << "CGI script execution failed or exited with error status" << std::endl;
            return "Status code 500 Internal Server Error\r\n";
        }

        std::cout << "CGI process completed successfully.\n";
        return output;
    }
}



