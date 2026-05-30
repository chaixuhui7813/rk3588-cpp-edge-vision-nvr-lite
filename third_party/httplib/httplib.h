#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>

namespace httplib {

struct Request {
    std::string method;
    std::string path;
};

struct Response {
    int status = 200;
    std::string body;
    std::string content_type = "text/plain";
    std::map<std::string, std::string> headers;

    void set_content(const std::string& content, const std::string& type) {
        body = content;
        content_type = type;
    }
};

class Server {
public:
    using Handler = std::function<void(const Request&, Response&)>;
    using StreamHandler = std::function<void(int)>;

    void Get(const std::string& path, Handler handler) {
        handlers_[path] = std::move(handler);
    }

    void GetStream(const std::string& path, StreamHandler handler) {
        stream_handlers_[path] = std::move(handler);
    }

    bool listen(const char* host, int port) {
        running_.store(true);
        server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ < 0) {
            std::cerr << "[http] socket failed: " << std::strerror(errno) << std::endl;
            return false;
        }
        int yes = 1;
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port));
        addr.sin_addr.s_addr = inet_addr(host);
        if (::bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            std::cerr << "[http] bind failed on " << host << ":" << port << ": " << std::strerror(errno) << std::endl;
            ::close(server_fd_);
            server_fd_ = -1;
            return false;
        }
        if (::listen(server_fd_, 16) < 0) {
            std::cerr << "[http] listen failed: " << std::strerror(errno) << std::endl;
            ::close(server_fd_);
            server_fd_ = -1;
            return false;
        }
        while (running_.load()) {
            sockaddr_in client{};
            socklen_t len = sizeof(client);
            int fd = ::accept(server_fd_, reinterpret_cast<sockaddr*>(&client), &len);
            if (fd < 0) {
                if (running_.load()) {
                    std::cerr << "[http] accept failed: " << std::strerror(errno) << std::endl;
                }
                continue;
            }
            std::thread(&Server::handleClient, this, fd).detach();
        }
        return true;
    }

    void stop() {
        running_.store(false);
        if (server_fd_ >= 0) {
            ::shutdown(server_fd_, SHUT_RDWR);
            ::close(server_fd_);
            server_fd_ = -1;
        }
    }

    static bool writeAll(int fd, const std::string& data) {
        const char* ptr = data.data();
        size_t left = data.size();
        while (left > 0) {
            ssize_t n = ::send(fd, ptr, left, MSG_NOSIGNAL);
            if (n <= 0) {
                return false;
            }
            ptr += n;
            left -= static_cast<size_t>(n);
        }
        return true;
    }

    static bool writeAll(int fd, const unsigned char* data, size_t size) {
        const unsigned char* ptr = data;
        size_t left = size;
        while (left > 0) {
            ssize_t n = ::send(fd, ptr, left, MSG_NOSIGNAL);
            if (n <= 0) {
                return false;
            }
            ptr += n;
            left -= static_cast<size_t>(n);
        }
        return true;
    }

private:
    void handleClient(int fd) {
        char buffer[4096] = {0};
        ssize_t n = ::recv(fd, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) {
            ::close(fd);
            return;
        }
        std::istringstream req_stream(std::string(buffer, static_cast<size_t>(n)));
        Request req;
        req_stream >> req.method >> req.path;
        auto q = req.path.find('?');
        if (q != std::string::npos) {
            req.path = req.path.substr(0, q);
        }
        auto stream_it = stream_handlers_.find(req.path);
        if (req.method == "GET" && stream_it != stream_handlers_.end()) {
            stream_it->second(fd);
            ::close(fd);
            return;
        }
        Response res;
        auto it = handlers_.find(req.path);
        if (req.method == "GET" && it != handlers_.end()) {
            it->second(req, res);
        } else {
            res.status = 404;
            res.set_content("404 not found\n", "text/plain");
        }
        sendResponse(fd, res);
        ::close(fd);
    }

    void sendResponse(int fd, const Response& res) {
        const char* text = res.status == 200 ? "OK" : (res.status == 404 ? "Not Found" : "Error");
        std::ostringstream oss;
        oss << "HTTP/1.1 " << res.status << " " << text << "\r\n";
        oss << "Content-Type: " << res.content_type << "\r\n";
        oss << "Content-Length: " << res.body.size() << "\r\n";
        oss << "Connection: close\r\n";
        for (const auto& kv : res.headers) {
            oss << kv.first << ": " << kv.second << "\r\n";
        }
        oss << "\r\n";
        oss << res.body;
        writeAll(fd, oss.str());
    }

    std::atomic<bool> running_{false};
    int server_fd_ = -1;
    std::map<std::string, Handler> handlers_;
    std::map<std::string, StreamHandler> stream_handlers_;
};

}  // namespace httplib
