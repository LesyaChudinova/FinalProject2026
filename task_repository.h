#pragma once
#include "../database/db_connection.h"
#include "../models/models.h"

class TaskRepository {
    std::shared_ptr<DbConnection> c_;
public:
    TaskRepository(std::shared_ptr<DbConnection> c) : c_(c) {}

    std::vector<FixedTask> getFixed(int userId, const std::string& week) {
        auto r = c_->executeParams(
            "SELECT t.task_id, t.task_name, ft.date, ft.beginning_time, ft.ending_time "
            "FROM tasks t JOIN fixed_tasks ft ON t.task_id = ft.task_id "
            "WHERE t.user_id = $1 AND t.week = $2",
            { std::to_string(userId), week });
        std::vector<FixedTask> v;
        for (int i = 0; i < PQntuples(r); i++) {
            FixedTask f;
            f.taskId = std::stoi(PQgetvalue(r, i, 0));
            f.name = PQgetvalue(r, i, 1);
            f.date = PQgetvalue(r, i, 2);
            f.beginTime = PQgetvalue(r, i, 3);
            f.endTime = PQgetvalue(r, i, 4);
            v.push_back(f);
        }
        PQclear(r); return v;
    }

    std::vector<FlexibleTask> getFlexible(int userId, const std::string& week) {
        auto r = c_->executeParams(
            "SELECT t.task_id, t.task_name, ft.duration, ft.activity_type, "
            "ft.pleasure_level, ft.importance_level, ft.deadline, ft.color "
            "FROM tasks t JOIN flexible_tasks ft ON t.task_id = ft.task_id "
            "WHERE t.user_id = $1 AND t.week = $2",
            { std::to_string(userId), week });
        std::vector<FlexibleTask> v;
        for (int i = 0; i < PQntuples(r); i++) {
            FlexibleTask f;
            f.taskId = std::stoi(PQgetvalue(r, i, 0));
            f.name = PQgetvalue(r, i, 1);
            f.duration = std::stod(PQgetvalue(r, i, 2));
            f.category = PQgetvalue(r, i, 3);
            f.pleasure = std::stoi(PQgetvalue(r, i, 4));
            f.importance = std::stoi(PQgetvalue(r, i, 5));
            if (!PQgetisnull(r, i, 6)) f.deadline = PQgetvalue(r, i, 6);
            if (!PQgetisnull(r, i, 7)) f.color = PQgetvalue(r, i, 7);
            f.preferredDays = getDays(f.taskId);
            v.push_back(f);
        }
        PQclear(r); return v;
    }

    std::vector<int> getDays(int taskId) {
        auto r = c_->executeParams(
            "SELECT day_id FROM task_days WHERE task_id = $1",
            { std::to_string(taskId) });
        std::vector<int> v;
        for (int i = 0; i < PQntuples(r); i++)
            v.push_back(std::stoi(PQgetvalue(r, i, 0)));
        PQclear(r); return v;
    }

    int create(const Task& t) {
        auto r = c_->executeParams(
            "INSERT INTO tasks (user_id, task_name, week, notes, activity_level, task_type, task_view) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7) RETURNING task_id",
            { std::to_string(t.userId), t.name, t.week, t.notes,
              std::to_string(t.activityLevel), t.taskType, t.taskView });
        int id = std::stoi(PQgetvalue(r, 0, 0)); PQclear(r); return id;
    }

    void createFixed(const FixedTask& f) {
        c_->executeParams(
            "INSERT INTO fixed_tasks (task_id, date, beginning_time, ending_time) "
            "VALUES ($1, $2, $3, $4)",
            { std::to_string(f.taskId), f.date, f.beginTime, f.endTime });
    }

    void createFlexible(const FlexibleTask& f) {
        c_->executeParams(
            "INSERT INTO flexible_tasks (task_id, duration, activity_type, pleasure_level, importance_level, deadline, color) "
            "VALUES ($1, $2, $3, $4, $5, NULLIF($6,'')::DATE, $7)",
            { std::to_string(f.taskId), std::to_string(f.duration), f.category,
              std::to_string(f.pleasure), std::to_string(f.importance),
              f.deadline, f.color });
        for (int d : f.preferredDays)
            c_->executeParams(
                "INSERT INTO task_days (task_id, day_id) VALUES ($1, $2)",
                { std::to_string(f.taskId), std::to_string(d) });
    }

    void postpone(int taskId, const std::string& period) {
        c_->executeParams(
            "INSERT INTO postponed_tasks (task_id, period) VALUES ($1, $2)",
            { std::to_string(taskId), period });
    }

    std::vector<FlexibleTask> getPostponed(int userId) {
        auto r = c_->executeParams(
            "SELECT pt.postponed_id, t.task_name, ft.duration, ft.activity_type, "
            "ft.pleasure_level, ft.importance_level, ft.deadline, ft.task_id, ft.color "
            "FROM postponed_tasks pt "
            "JOIN flexible_tasks ft ON pt.task_id = ft.task_id "
            "JOIN tasks t ON ft.task_id = t.task_id "
            "WHERE t.user_id = $1 AND pt.period = 'next_week'",
            { std::to_string(userId) });
        std::vector<FlexibleTask> v;
        for (int i = 0; i < PQntuples(r); i++) {
            FlexibleTask f;
            f.taskId = std::stoi(PQgetvalue(r, i, 7));
            f.name = PQgetvalue(r, i, 1);
            f.duration = std::stod(PQgetvalue(r, i, 2));
            f.category = PQgetvalue(r, i, 3);
            f.pleasure = std::stoi(PQgetvalue(r, i, 4));
            f.importance = std::stoi(PQgetvalue(r, i, 5));
            if (!PQgetisnull(r, i, 6)) f.deadline = PQgetvalue(r, i, 6);
            if (!PQgetisnull(r, i, 8)) f.color = PQgetvalue(r, i, 8);
            v.push_back(f);
        }
        PQclear(r); return v;
    }

    void deleteTask(int taskId) {
        c_->executeParams("DELETE FROM tasks WHERE task_id = $1",
            { std::to_string(taskId) });
    }

    void addPreferredDays(int taskId, const std::vector<int>& days) {
        for (int d : days) {
            c_->executeParams(
                "INSERT INTO task_days(task_id, day_id) VALUES($1, $2) ON CONFLICT DO NOTHING",
                { std::to_string(taskId), std::to_string(d) });
        }
    }

    void updateTaskName(int taskId, const std::string& name) {
        c_->executeParams("UPDATE tasks SET task_name = $1 WHERE task_id = $2",
            { name, std::to_string(taskId) });
    }

    std::optional<Task> getTaskById(int taskId) {
        auto r = c_->executeParams(
            "SELECT task_id, user_id, task_name, week, notes, activity_level, task_type, task_view "
            "FROM tasks WHERE task_id = $1",
            { std::to_string(taskId) });
        if (PQntuples(r) == 0) { PQclear(r); return std::nullopt; }
        Task t;
        t.id = std::stoi(PQgetvalue(r, 0, 0));
        t.userId = std::stoi(PQgetvalue(r, 0, 1));
        t.name = PQgetvalue(r, 0, 2);
        t.week = PQgetvalue(r, 0, 3);
        t.notes = PQgetvalue(r, 0, 4);
        t.activityLevel = std::stoi(PQgetvalue(r, 0, 5));
        t.taskType = PQgetvalue(r, 0, 6);
        t.taskView = PQgetvalue(r, 0, 7);
        PQclear(r);
        return t;
    }

    std::optional<FlexibleTask> getFlexibleByTaskId(int taskId) {
        auto r = c_->executeParams(
            "SELECT task_id, duration, activity_type, pleasure_level, importance_level, deadline, color "
            "FROM flexible_tasks WHERE task_id = $1",
            { std::to_string(taskId) });
        if (PQntuples(r) == 0) { PQclear(r); return std::nullopt; }
        FlexibleTask ft;
        ft.taskId = std::stoi(PQgetvalue(r, 0, 0));
        ft.duration = std::stod(PQgetvalue(r, 0, 1));
        ft.category = PQgetvalue(r, 0, 2);
        ft.pleasure = std::stoi(PQgetvalue(r, 0, 3));
        ft.importance = std::stoi(PQgetvalue(r, 0, 4));
        if (!PQgetisnull(r, 0, 5)) ft.deadline = PQgetvalue(r, 0, 5);
        if (!PQgetisnull(r, 0, 6)) ft.color = PQgetvalue(r, 0, 6);
        ft.preferredDays = getDays(taskId);
        PQclear(r);
        return ft;
    }

    void updateTaskDuration(int taskId, double duration) {
        c_->executeParams("UPDATE flexible_tasks SET duration = $1 WHERE task_id = $2",
            { std::to_string(duration), std::to_string(taskId) });
    }

    void updateFlexibleFieldString(int taskId, const std::string& field, const std::string& value) {
        std::string sql = "UPDATE flexible_tasks SET " + field + " = $1 WHERE task_id = $2";
        c_->executeParams(sql, { value, std::to_string(taskId) });
    }

    void updateFlexibleField(int taskId, const std::string& field, int value) {
        std::string sql = "UPDATE flexible_tasks SET " + field + " = $1 WHERE task_id = $2";
        c_->executeParams(sql, { std::to_string(value), std::to_string(taskId) });
    }

    void updateFlexibleDeadline(int taskId, const std::string& deadline) {
        c_->executeParams("UPDATE flexible_tasks SET deadline = NULLIF($1,'')::DATE WHERE task_id = $2",
            { deadline, std::to_string(taskId) });
    }

    void updateTask(int taskId, const std::string& name, const std::string& notes, double duration) {
        c_->executeParams("UPDATE tasks SET task_name = $1, notes = $2 WHERE task_id = $3",
            { name, notes, std::to_string(taskId) });
        c_->executeParams("UPDATE flexible_tasks SET duration = $1 WHERE task_id = $2",
            { std::to_string(duration), std::to_string(taskId) });
    }
};