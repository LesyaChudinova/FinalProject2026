#pragma once
#include "db_connection.h"
#include <queue>

class ConnectionPool {
private:
    std::queue<std::shared_ptr<DbConnection>> pool_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::string host_;
    int port_;
    std::string dbname_;
    std::string user_;
    std::string password_;
    size_t maxSize_;
    bool shutdown_;

public:
    ConnectionPool(size_t maxSize = 5)
        : maxSize_(maxSize), shutdown_(false) {
    }

    ~ConnectionPool() {
        shutdown();
    }

    void initialize(const std::string& host, int port,
        const std::string& dbname,
        const std::string& user,
        const std::string& password) {
        host_ = host;
        port_ = port;
        dbname_ = dbname;
        user_ = user;
        password_ = password;

        for (size_t i = 0; i < maxSize_; i++) {
            auto conn = std::make_shared<DbConnection>();
            if (!conn->connect(host_, port_, dbname_, user_, password_)) {
                throw std::runtime_error("Failed to create connection");
            }
            pool_.push(conn);
        }
    }

    std::shared_ptr<DbConnection> acquire() {
        std::unique_lock<std::mutex> lock(mutex_);

        cv_.wait(lock, [this]() {
            return !pool_.empty() || shutdown_;
            });

        if (shutdown_) {
            return nullptr;
        }

        auto conn = pool_.front();
        pool_.pop();
        return conn;
    }

    void release(std::shared_ptr<DbConnection> conn) {
        std::lock_guard<std::mutex> lock(mutex_);
        pool_.push(conn);
        cv_.notify_one();
    }

    void shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown_ = true;
        while (!pool_.empty()) {
            pool_.pop();
        }
        cv_.notify_all();
    }
};