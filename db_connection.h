#pragma once
#include <string>
#include <libpq-fe.h>
#include <stdexcept>
#include <sstream>
#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <queue>

class DbConnection {
private: PGconn* conn_;
public:
    DbConnection() : conn_(nullptr) {}
    ~DbConnection() { if (conn_) PQfinish(conn_); }
    bool connect(const std::string& host, int port, const std::string& dbname,
        const std::string& user, const std::string& password) {
        std::stringstream ss;
        ss << "host=" << host << " port=" << port << " dbname=" << dbname << " user=" << user;
        if (!password.empty()) ss << " password=" << password;
        conn_ = PQconnectdb(ss.str().c_str());
        return PQstatus(conn_) == CONNECTION_OK;
    }
    PGresult* execute(const std::string& sql) {
        PGresult* res = PQexec(conn_, sql.c_str());
        if (PQresultStatus(res) != PGRES_TUPLES_OK && PQresultStatus(res) != PGRES_COMMAND_OK)
        {
            std::string e = PQerrorMessage(conn_); PQclear(res); throw std::runtime_error("SQL: " + e);
        }
        return res;
    }
    PGresult* executeParams(const std::string& sql, const std::vector<std::string>& p) {
        std::vector<const char*> pv; for (const auto& x : p) pv.push_back(x.c_str());
        PGresult* res = PQexecParams(conn_, sql.c_str(), (int)p.size(), nullptr, pv.data(), nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_TUPLES_OK && PQresultStatus(res) != PGRES_COMMAND_OK)
        {
            std::string e = PQerrorMessage(conn_); PQclear(res); throw std::runtime_error("SQL: " + e);
        }
        return res;
    }
};

class ConnectionPool {
private:
    std::queue<std::shared_ptr<DbConnection>> pool_;
    std::mutex mx_; std::condition_variable cv_;
    std::string host_, dbname_, user_, pass_;
    int port_; size_t max_; bool shutdown_ = false;
public:
    ConnectionPool(size_t n = 5) : max_(n) {}
    ~ConnectionPool() { shutdown(); }
    void init(const std::string& h, int p, const std::string& d, const std::string& u, const std::string& pw) {
        host_ = h; port_ = p; dbname_ = d; user_ = u; pass_ = pw;
        for (size_t i = 0; i < max_; i++) {
            auto c = std::make_shared<DbConnection>();
            if (!c->connect(host_, port_, dbname_, user_, pass_)) throw std::runtime_error("Pool init failed");
            pool_.push(c);
        }
    }
    std::shared_ptr<DbConnection> get() {
        std::unique_lock<std::mutex> lk(mx_);
        cv_.wait(lk, [this] { return !pool_.empty() || shutdown_; });
        if (shutdown_) return nullptr;
        auto c = pool_.front(); pool_.pop(); return c;
    }
    void put(std::shared_ptr<DbConnection> c) { std::lock_guard<std::mutex> lk(mx_); pool_.push(c); cv_.notify_one(); }
    void shutdown() { std::lock_guard<std::mutex> lk(mx_); shutdown_ = true; while (!pool_.empty()) pool_.pop(); cv_.notify_all(); }
};