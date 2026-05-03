#pragma once
#include <string>
#include <vector>
#include <optional>
#include "../database/db_connection.h"
#include "../models/models.h"
#include "../utils/crypto.h"
#include "../utils/validator.h"

class UserRepository {
    std::shared_ptr<DbConnection> c_;
public:
    UserRepository(std::shared_ptr<DbConnection> c) : c_(c) {}

    struct RegisterResult {
        bool ok = false;
        std::vector<std::string> errors;
    };

    struct LoginResult {
        bool ok = false;
        User user;
        std::string error;
    };

    RegisterResult registerUser(const User& u, const std::string& password) {
        RegisterResult r;

        auto emailCheck = Validator::validateEmail(u.email);
        if (!emailCheck.valid) { r.errors = emailCheck.errors; return r; }

        auto passCheck = Validator::validatePassword(password);
        if (!passCheck.valid) { r.errors = passCheck.errors; return r; }

        auto nickCheck = Validator::validateNickname(u.nickname);
        if (!nickCheck.valid) { r.errors = nickCheck.errors; return r; }

        if (emailExists(u.email)) { r.errors.push_back("Email already registered"); return r; }

        std::string salt = Crypto::generateSalt();
        std::string hash = Crypto::hashPassword(password, salt);

        auto res = c_->executeParams(
            "INSERT INTO users (email, users_password, salt, nickname, age, using_goals, concentration_time, lifestyle, personality_type, chronotype) VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10)",
            { u.email, hash, salt, u.nickname, std::to_string(u.age), u.goals, u.concentrationTime, u.lifestyle, u.personalityType, u.chronotype });

        r.ok = PQresultStatus(res) == PGRES_COMMAND_OK;
        PQclear(res);
        return r;
    }

    LoginResult loginUser(const std::string& email, const std::string& password) {
        LoginResult r;
        auto res = c_->executeParams("SELECT * FROM users WHERE email=$1", { email });
        if (PQntuples(res) == 0) { PQclear(res); r.error = "Wrong email or password"; return r; }

        std::string storedHash = PQgetvalue(res, 0, 2);
        std::string storedSalt = PQgetvalue(res, 0, 3);

        std::string computedHash = Crypto::hashPassword(password, storedSalt);
        if (computedHash != storedHash) { PQclear(res); r.error = "Wrong email or password"; return r; }

        r.ok = true;
        r.user.id = std::stoi(PQgetvalue(res, 0, 0));
        r.user.email = PQgetvalue(res, 0, 1);
        r.user.nickname = PQgetvalue(res, 0, 4) ? PQgetvalue(res, 0, 4) : "";

        r.user.age = 25;
        if (!PQgetisnull(res, 0, 5)) {
            std::string ageStr = PQgetvalue(res, 0, 5);
            if (!ageStr.empty()) r.user.age = std::stoi(ageStr);
        }

        r.user.goals = PQgetisnull(res, 0, 6) ? "" : PQgetvalue(res, 0, 6);
        r.user.concentrationTime = PQgetisnull(res, 0, 8) ? "45" : PQgetvalue(res, 0, 8);
        r.user.lifestyle = PQgetisnull(res, 0, 9) ? "" : PQgetvalue(res, 0, 9);
        r.user.personalityType = PQgetisnull(res, 0, 10) ? "" : PQgetvalue(res, 0, 10);
        r.user.chronotype = PQgetisnull(res, 0, 11) ? "neutral" : PQgetvalue(res, 0, 11);

        PQclear(res);
        return r;
    }

    bool emailExists(const std::string& e) {
        auto r = c_->executeParams("SELECT 1 FROM users WHERE email=$1", { e });
        bool ex = PQntuples(r) > 0; PQclear(r); return ex;
    }

    User getById(int id) {
        auto r = c_->executeParams("SELECT * FROM users WHERE user_id=$1", { std::to_string(id) });
        User u; u.id = id;
        u.email = PQgetvalue(r, 0, 1);
        u.nickname = PQgetisnull(r, 0, 4) ? "" : PQgetvalue(r, 0, 4);
        u.age = 25;
        if (!PQgetisnull(r, 0, 5)) {
            std::string a = PQgetvalue(r, 0, 5);
            if (!a.empty()) u.age = std::stoi(a);
        }
        u.goals = PQgetisnull(r, 0, 6) ? "" : PQgetvalue(r, 0, 6);
        u.concentrationTime = PQgetisnull(r, 0, 8) ? "45" : PQgetvalue(r, 0, 8);
        u.lifestyle = PQgetisnull(r, 0, 9) ? "" : PQgetvalue(r, 0, 9);
        u.personalityType = PQgetisnull(r, 0, 10) ? "" : PQgetvalue(r, 0, 10);
        u.chronotype = PQgetisnull(r, 0, 11) ? "neutral" : PQgetvalue(r, 0, 11);
        PQclear(r); return u;
    }

    void update(int id, const User& u) {
        c_->executeParams(
            "UPDATE users SET nickname=$1,age=$2,using_goals=$3,concentration_time=$4,lifestyle=$5,personality_type=$6,chronotype=$7 WHERE user_id=$8",
            { u.nickname, std::to_string(u.age), u.goals, u.concentrationTime, u.lifestyle, u.personalityType, u.chronotype, std::to_string(id) });
    }

    std::vector<BalanceTarget> getBalance(int userId) {
        auto r = c_->executeParams("SELECT category_name,target_percent FROM user_balance_targets WHERE user_id=$1", { std::to_string(userId) });
        std::vector<BalanceTarget> v;
        for (int i = 0; i < PQntuples(r); i++) {
            BalanceTarget b; b.userId = userId; b.category = PQgetvalue(r, i, 0); b.targetPercent = std::stoi(PQgetvalue(r, i, 1)); v.push_back(b);
        }
        PQclear(r); return v;
    }

    void saveBalance(int userId, const std::vector<BalanceTarget>& t) {
        c_->executeParams("DELETE FROM user_balance_targets WHERE user_id=$1", { std::to_string(userId) });
        for (const auto& b : t)
            c_->executeParams("INSERT INTO user_balance_targets(user_id,category_name,target_percent) VALUES($1,$2,$3)",
                { std::to_string(userId), b.category, std::to_string(b.targetPercent) });
    }
};