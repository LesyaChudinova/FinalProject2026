#pragma once
#include "../database/db_connection.h"
#include "../models/models.h"
#include <vector>
#include <optional>

class IdeaRepository {
    std::shared_ptr<DbConnection> c_;
public:
    IdeaRepository(std::shared_ptr<DbConnection> c) : c_(c) {}

    std::vector<Idea> getAll() {
        auto r = c_->execute("SELECT idea_id, idea_name, idea_description, idea_length, activity_level_of_idea, type_of_idea, energy_restored, mood_boost, color FROM ideas");
        std::vector<Idea> v;
        for (int i = 0; i < PQntuples(r); i++) {
            Idea idea;
            idea.id = std::stoi(PQgetvalue(r, i, 0));
            idea.name = PQgetvalue(r, i, 1);
            idea.description = PQgetvalue(r, i, 2);
            idea.length = std::stod(PQgetvalue(r, i, 3));
            idea.activityLevel = std::stoi(PQgetvalue(r, i, 4));
            idea.type = PQgetvalue(r, i, 5);
            idea.energyRestored = PQgetisnull(r, i, 6) ? 5 : std::stoi(PQgetvalue(r, i, 6));
            idea.moodBoost = PQgetisnull(r, i, 7) ? 5 : std::stoi(PQgetvalue(r, i, 7));
            idea.color = PQgetisnull(r, i, 8) ? "#E0E0E0" : PQgetvalue(r, i, 8);
            v.push_back(idea);
        }
        PQclear(r);
        return v;
    }

    std::optional<Idea> getById(int id) {
        auto r = c_->executeParams("SELECT idea_id, idea_name, idea_description, idea_length, activity_level_of_idea, type_of_idea, energy_restored, mood_boost, color FROM ideas WHERE idea_id = $1", { std::to_string(id) });
        if (PQntuples(r) == 0) {
            PQclear(r);
            return std::nullopt;
        }
        Idea idea;
        idea.id = std::stoi(PQgetvalue(r, 0, 0));
        idea.name = PQgetvalue(r, 0, 1);
        idea.description = PQgetvalue(r, 0, 2);
        idea.length = std::stod(PQgetvalue(r, 0, 3));
        idea.activityLevel = std::stoi(PQgetvalue(r, 0, 4));
        idea.type = PQgetvalue(r, 0, 5);
        idea.energyRestored = PQgetisnull(r, 0, 6) ? 5 : std::stoi(PQgetvalue(r, 0, 6));
        idea.moodBoost = PQgetisnull(r, 0, 7) ? 5 : std::stoi(PQgetvalue(r, 0, 7));
        idea.color = PQgetisnull(r, 0, 8) ? "#E0E0E0" : PQgetvalue(r, 0, 8);
        PQclear(r);
        return idea;
    }
};