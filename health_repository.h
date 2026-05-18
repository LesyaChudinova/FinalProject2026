#pragma once
#include "../database/db_connection.h"
#include "../models/models.h"
#include <optional>

class HealthRepository {
    std::shared_ptr<DbConnection> c_;
public:
    HealthRepository(std::shared_ptr<DbConnection> c) : c_(c) {}

    std::optional<HealthState> get(int userId, const std::string& week) {
        auto r = c_->executeParams(
            "SELECT health_state_id,tiredness_level,general_state,prevailing_emotion,mood_level,apathy_level FROM health_states WHERE user_id=$1 AND week=$2",
            { std::to_string(userId), week });
        if (PQntuples(r) == 0) { PQclear(r); return {}; }
        HealthState h; h.id = std::stoi(PQgetvalue(r, 0, 0)); h.userId = userId; h.week = week;
        h.tiredness = std::stoi(PQgetvalue(r, 0, 1)); h.general = std::stoi(PQgetvalue(r, 0, 2));
        h.emotion = PQgetvalue(r, 0, 3); h.mood = std::stoi(PQgetvalue(r, 0, 4)); h.apathy = std::stoi(PQgetvalue(r, 0, 5));
        PQclear(r); return h;
    }

    void saveOrUpdate(int userId, const std::string& week, int tiredness, int general,
        const std::string& emotion, int mood, int apathy) {
        c_->executeParams(
            "INSERT INTO health_states (user_id, week, tiredness_level, general_state, prevailing_emotion, mood_level, apathy_level) "
            "VALUES ($1, $2::date, $3, $4, $5, $6, $7) "
            "ON CONFLICT (user_id, week) DO UPDATE SET "
            "tiredness_level = $3, general_state = $4, prevailing_emotion = $5, mood_level = $6, apathy_level = $7",
            { std::to_string(userId), week, std::to_string(tiredness), std::to_string(general),
             emotion, std::to_string(mood), std::to_string(apathy) }
        );
    }

    void save(const HealthState& h) {
        c_->executeParams(
            "INSERT INTO health_states(user_id,week,tiredness_level,general_state,prevailing_emotion,mood_level,apathy_level) VALUES($1,$2,$3,$4,$5,$6,$7)",
            { std::to_string(h.userId), h.week, std::to_string(h.tiredness), std::to_string(h.general), h.emotion, std::to_string(h.mood), std::to_string(h.apathy) });
    }
};