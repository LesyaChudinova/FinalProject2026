#include <windows.h>
#include <shellapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <map>
#include <chrono>
#include <ctime>
#include <algorithm>
#include <set>

#include "database/db_connection.h"
#include "models/models.h"
#include "repositories/user_repository.h"
#include "repositories/task_repository.h"
#include "repositories/health_repository.h"
#include "repositories/idea_repository.h"
#include "scheduler/scheduler_kernel.h"
#include "httplib.h"
#include "json.hpp"

using json = nlohmann::json;

ConnectionPool* g_pool = nullptr;
int g_userId = 0;
std::map<std::pair<int, std::string>, WeeklyPlan> g_tempPlan;
std::map<std::pair<int, std::string>, WeeklyPlan> g_approvedPlan;

HANDLE g_hPythonProcess = NULL;

std::string getCurrentWeek() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&tt);
    int dow = tm.tm_wday;
    if (dow == 0) dow = 7;
    tm.tm_mday -= (dow - 1);
    std::mktime(&tm);
    char buf[11];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return std::string(buf);
}

std::string getNextWeek(const std::string& currentWeek) {
    int y, m, d;
    sscanf(currentWeek.c_str(), "%d-%d-%d", &y, &m, &d);
    std::tm tm = {};
    tm.tm_year = y - 1900;
    tm.tm_mon = m - 1;
    tm.tm_mday = d + 7;
    std::mktime(&tm);
    char buf[11];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return std::string(buf);
}

std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string errorPage(const std::string& msg) {
    std::string h;
    h += "<!DOCTYPE html><html lang='ru'><head><meta charset='UTF-8'><title>Error</title>";
    h += "<link rel='stylesheet' href='/style.css'></head><body><div class='form-container' style='margin-top:50px;'>";
    h += "<h1>Error</h1><p style='color:red;text-align:center;'>" + msg + "</p>";
    h += "<a href='javascript:history.back()'><button class='btn'>Back</button></a>";
    h += "</div></body></html>";
    return h;
}


static int g_tempTaskIdCounter = -1;
int getNextTempTaskId() {
    return g_tempTaskIdCounter--;
}


void startPythonAnalytics() {
    httplib::Client cli("localhost", 5000);
    auto res = cli.Get("/healthcheck");
    if (res && res->status == 200) {
        std::cout << "[Python] Already running." << std::endl;
        return;
    }

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring dir = exePath;
    dir = dir.substr(0, dir.find_last_of(L"\\/") + 1);

    std::wstring scriptPath = dir + L"analytics\\analytics_service.py";
    std::wstring cmd = L"py \"" + scriptPath + L"\"";

    if (!CreateProcessW(NULL, cmd.data(), NULL, NULL, FALSE,
        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        std::cerr << "[Python] Failed to start process." << std::endl;
        return;
    }
    g_hPythonProcess = pi.hProcess;
    CloseHandle(pi.hThread);

    std::cout << "[Python] Waiting for service to start..." << std::endl;
    bool ready = false;
    for (int i = 0; i < 15; ++i) {
        Sleep(800);
        httplib::Client cli2("localhost", 5000);
        auto res2 = cli2.Get("/healthcheck");
        if (res2 && res2->status == 200) {
            ready = true;
            break;
        }
    }

    if (ready) {
        std::cout << "[Python] Service is ready." << std::endl;
    }
    else {
        std::cerr << "[Python] Service did not respond in time. Check manually: py analytics\\analytics_service.py" << std::endl;
    }
}

void stopPythonAnalytics() {
    if (g_hPythonProcess) {
        TerminateProcess(g_hPythonProcess, 0);
        CloseHandle(g_hPythonProcess);
        g_hPythonProcess = NULL;
    }
}


void saveTempTasksToDB(int userId) {
    for (auto& pair : g_tempPlan) {
        if (pair.first.first != userId) continue;
        auto& plan = pair.second;
        const std::string& week = pair.first.second;

        for (auto& s : plan.scheduled) {
            if (s.task.taskId < 0) {
                auto c = g_pool->get();
                TaskRepository tr(c);
                Task t;
                t.userId = userId;
                t.name = s.task.name;
                t.week = week;
                t.notes = "";
                t.activityLevel = 5;
                t.taskType = "flexible";
                t.taskView = "individual";
                int newId = tr.create(t);
                FlexibleTask ft = s.task;
                ft.taskId = newId;
                tr.createFlexible(ft);
                g_pool->put(c);
                s.task.taskId = newId;
            }
        }

        for (auto& u : plan.unscheduled) {
            if (u.taskId < 0) {
                auto c = g_pool->get();
                TaskRepository tr(c);
                Task t;
                t.userId = userId;
                t.name = u.name;
                t.week = week;
                t.notes = "";
                t.activityLevel = 5;
                t.taskType = "flexible";
                t.taskView = "individual";
                int newId = tr.create(t);
                FlexibleTask ft = u;
                ft.taskId = newId;
                tr.createFlexible(ft);
                g_pool->put(c);
                u.taskId = newId;
            }
        }

        for (auto& p : plan.postponed) {
            if (p.taskId < 0) {
                auto c = g_pool->get();
                TaskRepository tr(c);
                Task t;
                t.userId = userId;
                t.name = p.name;
                t.week = week;
                t.notes = "";
                t.activityLevel = 5;
                t.taskType = "flexible";
                t.taskView = "individual";
                int newId = tr.create(t);
                FlexibleTask ft = p;
                ft.taskId = newId;
                tr.createFlexible(ft);
                g_pool->put(c);
                p.taskId = newId;
            }
        }
    }
}


void setupRoutes(httplib::Server& srv) {
    srv.Get("/style.css", [](auto&, auto& res) {
        res.set_content(readFile("front/style.css"), "text/css; charset=utf-8");
        });

    srv.Get("/", [](auto&, auto& res) {
        if (g_userId) { res.set_redirect("/index"); return; }
        res.set_content(readFile("front/login.html"), "text/html; charset=utf-8");
        });
    srv.Get("/register", [](auto&, auto& res) {
        if (g_userId) { res.set_redirect("/index"); return; }
        res.set_content(readFile("front/register.html"), "text/html; charset=utf-8");
        });
    srv.Post("/login", [](auto& req, auto& res) {
        try {
            auto c = g_pool->get(); UserRepository ur(c);
            auto r = ur.loginUser(req.get_param_value("email"), req.get_param_value("password"));
            g_pool->put(c);
            if (r.ok) { g_userId = r.user.id; res.set_redirect("/index"); }
            else res.set_content(errorPage(r.error), "text/html; charset=utf-8");
        }
        catch (const std::exception& e) {
            res.set_content(errorPage(std::string("Server error: ") + e.what()), "text/html; charset=utf-8");
        }
        });
    srv.Post("/register", [](auto& req, auto& res) {
        try {
            User u; u.email = req.get_param_value("email"); u.nickname = req.get_param_value("nickname");
            u.age = req.has_param("age") ? std::stoi(req.get_param_value("age")) : 25;
            u.goals = "not specified"; u.concentrationTime = "45"; u.lifestyle = "sedentary";
            u.personalityType = "ambivert"; u.chronotype = "neutral";
            auto c = g_pool->get(); UserRepository ur(c);
            auto r = ur.registerUser(u, req.get_param_value("password"));
            g_pool->put(c);
            if (r.ok) res.set_redirect("/?registered=1");
            else { std::string m; for (auto& e : r.errors) m += e + "<br>"; res.set_content(errorPage(m), "text/html; charset=utf-8"); }
        }
        catch (const std::exception& e) {
            res.set_content(errorPage(std::string("Server error: ") + e.what()), "text/html; charset=utf-8");
        }
        });
    srv.Get("/logout", [](auto&, auto& res) { g_userId = 0; res.set_redirect("/"); });


    srv.Get("/index", [](auto&, auto& res) {
        if (!g_userId) { res.set_redirect("/"); return; }
        res.set_content(readFile("front/index.html"), "text/html; charset=utf-8");
        });
    srv.Get("/day", [](auto&, auto& res) {
        if (!g_userId) { res.set_redirect("/"); return; }
        res.set_content(readFile("front/day.html"), "text/html; charset=utf-8");
        });
    srv.Get("/analytics", [](auto& req, auto& res) {
        if (!g_userId) { res.set_redirect("/"); return; }
        res.set_content(readFile("front/analytics.html"), "text/html; charset=utf-8");
        });
    srv.Get("/profile", [](auto&, auto& res) {
        if (!g_userId) { res.set_redirect("/"); return; }
        res.set_content(readFile("front/profile.html"), "text/html; charset=utf-8");
        });
    srv.Get("/tasks", [](auto&, auto& res) {
        if (!g_userId) { res.set_redirect("/"); return; }
        res.set_content(readFile("front/tasks.html"), "text/html; charset=utf-8");
        });
    srv.Get("/health", [](auto&, auto& res) {
        if (!g_userId) { res.set_redirect("/"); return; }
        res.set_content(readFile("front/health.html"), "text/html; charset=utf-8");
        });
    srv.Get("/instructions", [](auto&, auto& res) {
        if (!g_userId) { res.set_redirect("/"); return; }
        res.set_content(readFile("front/instructions.html"), "text/html; charset=utf-8");
        });
    srv.Get("/edit", [](auto& req, auto& res) {
        if (!g_userId) { res.set_redirect("/"); return; }
        std::string week = req.has_param("week") ? req.get_param_value("week") : getCurrentWeek();

        if (req.has_param("reset")) {
            g_tempPlan.erase({ g_userId, week });
        }

        if (g_tempPlan.find({ g_userId, week }) == g_tempPlan.end()) {
            auto it = g_approvedPlan.find({ g_userId, week });
            if (it != g_approvedPlan.end()) {
                g_tempPlan[{g_userId, week}] = it->second;
            }
            else {
                g_tempPlan[{g_userId, week}] = WeeklyPlan{};
            }
        }

        res.set_content(readFile("front/edit.html"), "text/html; charset=utf-8");
        });

    srv.Get("/api/analytics/svg/tiredness", [](auto& req, auto& res) {
        httplib::Client cli("localhost", 5000);
        std::string uid = req.has_param("user_id") ? req.get_param_value("user_id") : "1";
        auto result = cli.Get(("/svg/tiredness?user_id=" + uid).c_str());
        if (result && result->status == 200)
            res.set_content(result->body, "image/png");
        else
            res.set_content("<p>Сервис недоступен</p>", "text/html; charset=utf-8");
        });
    srv.Get("/api/analytics/svg/mood", [](auto& req, auto& res) {
        httplib::Client cli("localhost", 5000);
        std::string uid = req.has_param("user_id") ? req.get_param_value("user_id") : "1";
        auto result = cli.Get(("/svg/mood?user_id=" + uid).c_str());
        if (result && result->status == 200)
            res.set_content(result->body, "image/png");
        else
            res.set_content("<p>Сервис недоступен</p>", "text/html; charset=utf-8");
        });
    srv.Get("/api/analytics/svg/apathy", [](auto& req, auto& res) {
        httplib::Client cli("localhost", 5000);
        std::string uid = req.has_param("user_id") ? req.get_param_value("user_id") : "1";
        auto result = cli.Get(("/svg/apathy?user_id=" + uid).c_str());
        if (result && result->status == 200)
            res.set_content(result->body, "image/png");
        else
            res.set_content("<p>Сервис недоступен</p>", "text/html; charset=utf-8");
        });
    srv.Get("/api/analytics/svg/general", [](auto& req, auto& res) {
        httplib::Client cli("localhost", 5000);
        std::string uid = req.has_param("user_id") ? req.get_param_value("user_id") : "1";
        auto result = cli.Get(("/svg/general?user_id=" + uid).c_str());
        if (result && result->status == 200)
            res.set_content(result->body, "image/png");
        else
            res.set_content("<p>Сервис недоступен</p>", "text/html; charset=utf-8");
        });
    srv.Get("/api/analytics/svg/activity", [](auto& req, auto& res) {
        httplib::Client cli("localhost", 5000);
        std::string uid = req.has_param("user_id") ? req.get_param_value("user_id") : "1";
        auto result = cli.Get(("/svg/activity?user_id=" + uid).c_str());
        if (result && result->status == 200)
            res.set_content(result->body, "image/png");
        else
            res.set_content("<p>Сервис недоступен</p>", "text/html; charset=utf-8");
        });
    srv.Get("/api/analytics/svg/wheel", [](auto& req, auto& res) {
        httplib::Client cli("localhost", 5000);
        std::string uid = req.has_param("user_id") ? req.get_param_value("user_id") : "1";
        std::string week = req.has_param("week") ? req.get_param_value("week") : getCurrentWeek();
        auto result = cli.Get(("/svg/wheel?user_id=" + uid + "&week=" + week).c_str());
        if (result && result->status == 200)
            res.set_content(result->body, "image/png");
        else
            res.set_content("<p>Сервис недоступен</p>", "text/html; charset=utf-8");
        });

    srv.Get("/api/profile", [](auto&, auto& res) {
        if (!g_userId) { res.set_content("{}", "application/json"); return; }
        auto c = g_pool->get(); UserRepository ur(c); User u = ur.getById(g_userId); g_pool->put(c);
        json j = { {"nickname",u.nickname},{"email",u.email},{"age",u.age},{"goals",u.goals},{"lifestyle",u.lifestyle},{"personality",u.personalityType},{"chronotype",u.chronotype},{"concentration",u.concentrationTime} };
        res.set_content(j.dump(), "application/json");
        });
    srv.Post("/api/profile", [](auto& req, auto& res) {
        if (!g_userId) { res.set_content("{}", "application/json"); return; }
        auto j = json::parse(req.body); auto c = g_pool->get(); UserRepository ur(c); User u = ur.getById(g_userId);
        u.nickname = j.value("nickname", u.nickname); u.age = j.value("age", u.age);
        u.goals = j.value("goals", u.goals); u.lifestyle = j.value("lifestyle", u.lifestyle);
        u.personalityType = j.value("personality", u.personalityType);
        u.chronotype = j.value("chronotype", u.chronotype); u.concentrationTime = j.value("concentration", u.concentrationTime);
        ur.update(g_userId, u); g_pool->put(c);
        res.set_content("{\"ok\":true}", "application/json");
        });

    srv.Get("/api/tasks/(\\d+)", [](auto& req, auto& res) {
        if (!g_userId) { res.set_content("{}", "application/json"); return; }
        int taskId = std::stoi(req.matches[1]);
        if (taskId < 0) {
            res.set_content("{}", "application/json");
            return;
        }
        auto c = g_pool->get();
        TaskRepository tr(c);
        auto task = tr.getTaskById(taskId);
        g_pool->put(c);
        if (task.has_value()) {
            json j;
            j["id"] = task->id;
            j["name"] = task->name;
            j["task_type"] = task->taskType;
            j["activity_level"] = task->activityLevel;
            auto flexible = tr.getFlexibleByTaskId(taskId);
            if (flexible.has_value()) {
                j["duration"] = flexible->duration;
                j["category"] = flexible->category;
                j["importance"] = flexible->importance;
                j["pleasure"] = flexible->pleasure;
                j["deadline"] = flexible->deadline;
                j["days"] = flexible->preferredDays;
            }
            res.set_content(j.dump(), "application/json");
        }
        else {
            res.set_content("{}", "application/json");
        }
        });

    srv.Put("/api/tasks/(\\d+)", [](auto& req, auto& res) {
        if (!g_userId) { res.set_content("{}", "application/json"); return; }
        int taskId = std::stoi(req.matches[1]);
        json j;
        try { j = json::parse(req.body); }
        catch (...) {
            res.set_content("{\"ok\":false,\"error\":\"Invalid JSON\"}", "application/json"); return;
        }


        if (taskId < 0) {
            std::string week = j.value("week", getCurrentWeek());
            auto it = g_tempPlan.find({ g_userId, week });
            if (it == g_tempPlan.end()) {
                res.set_content("{\"ok\":false,\"error\":\"plan not found\"}", "application/json");
                return;
            }
            auto& plan = it->second;

            if (j.contains("remove_from_plan") && j["remove_from_plan"].get<bool>()) {
                plan.scheduled.erase(
                    std::remove_if(plan.scheduled.begin(), plan.scheduled.end(),
                        [taskId](const WeeklyPlan::ScheduledItem& s) { return s.task.taskId == taskId; }),
                    plan.scheduled.end());
                plan.unscheduled.erase(
                    std::remove_if(plan.unscheduled.begin(), plan.unscheduled.end(),
                        [taskId](const FlexibleTask& t) { return t.taskId == taskId; }),
                    plan.unscheduled.end());
                plan.postponed.erase(
                    std::remove_if(plan.postponed.begin(), plan.postponed.end(),
                        [taskId](const FlexibleTask& t) { return t.taskId == taskId; }),
                    plan.postponed.end());
                res.set_content("{\"ok\":true}", "application/json");
                return;
            }

            if (j.contains("move_to_schedule") && j["move_to_schedule"].get<bool>()) {
                for (auto uit = plan.unscheduled.begin(); uit != plan.unscheduled.end(); ++uit) {
                    if (uit->taskId == taskId) {
                        WeeklyPlan::ScheduledItem si;
                        si.task = *uit;
                        si.dayId = j.value("day", 1);
                        si.time = j.value("time", "09:00");
                        if (j.contains("importance")) si.task.importance = j["importance"];
                        if (j.contains("pleasure")) si.task.pleasure = j["pleasure"];
                        if (j.contains("category")) si.task.category = j["category"].get<std::string>();
                        plan.scheduled.push_back(si);
                        plan.unscheduled.erase(uit);
                        res.set_content("{\"ok\":true}", "application/json");
                        return;
                    }
                }
                res.set_content("{\"ok\":false,\"error\":\"task not found in unscheduled\"}", "application/json");
                return;
            }

            if (j.contains("postpone") && j["postpone"].get<bool>()) {
                for (auto uit = plan.unscheduled.begin(); uit != plan.unscheduled.end(); ++uit) {
                    if (uit->taskId == taskId) {
                        plan.postponed.push_back(*uit);
                        plan.unscheduled.erase(uit);
                        res.set_content("{\"ok\":true}", "application/json");
                        return;
                    }
                }
                res.set_content("{\"ok\":false,\"error\":\"task not found in unscheduled\"}", "application/json");
                return;
            }

            if (j.contains("time") || j.contains("day")) {
                for (auto& s : plan.scheduled) {
                    if (s.task.taskId == taskId) {
                        if (j.contains("time")) s.time = j["time"];
                        if (j.contains("day")) s.dayId = j["day"];
                        res.set_content("{\"ok\":true}", "application/json");
                        return;
                    }
                }
            }

            res.set_content("{\"ok\":false,\"error\":\"unsupported action for temp task\"}", "application/json");
            return;
        }


        auto c = g_pool->get();
        TaskRepository tr(c);
        try {
            auto task = tr.getTaskById(taskId);
            if (!task.has_value()) {
                g_pool->put(c);
                res.set_content("{\"ok\":false,\"error\":\"Task not found\"}", "application/json");
                return;
            }
            std::string taskType = task->taskType;

            if (j.contains("name")) tr.updateTaskName(taskId, j["name"]);
            if (j.contains("activity_level")) {
                c->executeParams("UPDATE tasks SET activity_level = $1 WHERE task_id = $2",
                    { std::to_string(j["activity_level"].get<int>()), std::to_string(taskId) });
            }

            if (taskType == "flexible") {
                if (j.contains("duration")) tr.updateTaskDuration(taskId, j["duration"]);
                if (j.contains("importance")) tr.updateFlexibleField(taskId, "importance_level", j["importance"]);
                if (j.contains("pleasure")) tr.updateFlexibleField(taskId, "pleasure_level", j["pleasure"]);
                if (j.contains("deadline")) tr.updateFlexibleDeadline(taskId, j["deadline"]);
                if (j.contains("category")) tr.updateFlexibleFieldString(taskId, "activity_type", j["category"].get<std::string>());
                if (j.contains("days")) {
                    c->executeParams("DELETE FROM task_days WHERE task_id = $1", { std::to_string(taskId) });
                    for (auto& d : j["days"]) {
                        int day = d.get<int>();
                        c->executeParams("INSERT INTO task_days(task_id, day_id) VALUES($1, $2) ON CONFLICT DO NOTHING",
                            { std::to_string(taskId), std::to_string(day) });
                    }
                }
            }
            else if (taskType == "fixed") {
                if (j.contains("date") && j.contains("begin") && j.contains("end")) {
                    c->executeParams("UPDATE fixed_tasks SET date = $1, beginning_time = $2, ending_time = $3 WHERE task_id = $4",
                        { j["date"], j["begin"], j["end"], std::to_string(taskId) });
                }
            }

            std::string week = j.value("week", getCurrentWeek());
            auto it = g_tempPlan.find({ g_userId, week });
            if (it != g_tempPlan.end()) {
                auto& plan = it->second;

                if (j.contains("postpone") && j["postpone"].get<bool>()) {
                    for (auto it2 = plan.scheduled.begin(); it2 != plan.scheduled.end(); ++it2) {
                        if (it2->task.taskId == taskId) {
                            FlexibleTask ft = it2->task;
                            plan.scheduled.erase(it2);
                            plan.postponed.push_back(ft);

                            c->executeParams("INSERT INTO postponed_tasks (task_id, user_id, reason) VALUES ($1, $2, 'postponed by user') ON CONFLICT (task_id) DO NOTHING",
                                { std::to_string(taskId), std::to_string(g_userId) });

                            std::string nextWeek = getNextWeek(week);
                            c->executeParams("UPDATE tasks SET week = $1 WHERE task_id = $2",
                                { nextWeek, std::to_string(taskId) });

                            break;
                        }
                    }
                }
                else if (j.contains("move_to_schedule") && j["move_to_schedule"].get<bool>()) {
                    FlexibleTask ftTask;
                    bool found = false;
                    for (auto uit = plan.unscheduled.begin(); uit != plan.unscheduled.end(); ++uit) {
                        if (uit->taskId == taskId) {
                            ftTask = *uit;
                            plan.unscheduled.erase(uit);
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        for (auto pit = plan.postponed.begin(); pit != plan.postponed.end(); ++pit) {
                            if (pit->taskId == taskId) {
                                ftTask = *pit;
                                plan.postponed.erase(pit);
                                found = true;
                                break;
                            }
                        }
                    }
                    if (found) {
                        WeeklyPlan::ScheduledItem si;
                        si.task = ftTask;
                        si.dayId = j.value("day", 1);
                        si.time = j.value("time", "09:00");
                        if (j.contains("importance")) si.task.importance = j["importance"];
                        if (j.contains("pleasure")) si.task.pleasure = j["pleasure"];
                        plan.scheduled.push_back(si);
                    }
                }
                else if (j.contains("move_to_unscheduled_next_week") && j["move_to_unscheduled_next_week"].get<bool>()) {
                    c->executeParams("DELETE FROM postponed_tasks WHERE task_id = $1", { std::to_string(taskId) });
                    std::string nextWeek = getNextWeek(week);
                    c->executeParams("UPDATE tasks SET week = $1 WHERE task_id = $2",
                        { nextWeek, std::to_string(taskId) });
                    auto flexTask = tr.getFlexibleByTaskId(taskId);
                    if (flexTask.has_value()) {
                        g_tempPlan[{g_userId, nextWeek}].unscheduled.push_back(*flexTask);
                    }
                }
                else {
                    for (auto& s : plan.scheduled) {
                        if (s.task.taskId == taskId) {
                            if (j.contains("name")) s.task.name = j["name"];
                            if (j.contains("duration")) s.task.duration = j["duration"];
                            if (j.contains("importance")) s.task.importance = j["importance"];
                            if (j.contains("pleasure")) s.task.pleasure = j["pleasure"];
                            if (j.contains("category")) s.task.category = j["category"];
                            if (j.contains("deadline")) s.task.deadline = j["deadline"];
                            if (j.contains("time")) s.time = j["time"];
                            if (j.contains("day")) s.dayId = j["day"];
                            break;
                        }
                    }
                    for (auto& u : plan.unscheduled) {
                        if (u.taskId == taskId) {
                            if (j.contains("name")) u.name = j["name"];
                            if (j.contains("duration")) u.duration = j["duration"];
                            if (j.contains("importance")) u.importance = j["importance"];
                            if (j.contains("pleasure")) u.pleasure = j["pleasure"];
                            if (j.contains("category")) u.category = j["category"];
                            if (j.contains("deadline")) u.deadline = j["deadline"];
                            break;
                        }
                    }
                    for (auto& p : plan.postponed) {
                        if (p.taskId == taskId) {
                            if (j.contains("name")) p.name = j["name"];
                            if (j.contains("duration")) p.duration = j["duration"];
                            if (j.contains("importance")) p.importance = j["importance"];
                            if (j.contains("pleasure")) p.pleasure = j["pleasure"];
                            if (j.contains("category")) p.category = j["category"];
                            if (j.contains("deadline")) p.deadline = j["deadline"];
                            break;
                        }
                    }
                }
            }

            g_pool->put(c);
            res.set_content("{\"ok\":true}", "application/json");
        }
        catch (const std::exception& e) {
            g_pool->put(c);
            res.set_content("{\"ok\":false,\"error\":\"" + std::string(e.what()) + "\"}", "application/json");
        }
        });

    srv.Delete("/api/tasks/(\\d+)", [](auto& req, auto& res) {
        if (!g_userId) { res.set_content("{}", "application/json"); return; }
        int taskId = std::stoi(req.matches[1]);

        if (taskId < 0) {
            for (auto& pair : g_tempPlan) {
                if (pair.first.first != g_userId) continue;
                auto& plan = pair.second;
                plan.scheduled.erase(
                    std::remove_if(plan.scheduled.begin(), plan.scheduled.end(),
                        [taskId](const WeeklyPlan::ScheduledItem& s) { return s.task.taskId == taskId; }),
                    plan.scheduled.end());
                plan.unscheduled.erase(
                    std::remove_if(plan.unscheduled.begin(), plan.unscheduled.end(),
                        [taskId](const FlexibleTask& t) { return t.taskId == taskId; }),
                    plan.unscheduled.end());
                plan.postponed.erase(
                    std::remove_if(plan.postponed.begin(), plan.postponed.end(),
                        [taskId](const FlexibleTask& t) { return t.taskId == taskId; }),
                    plan.postponed.end());
            }
            res.set_content("{\"ok\":true}", "application/json");
            return;
        }

        auto c = g_pool->get();
        TaskRepository tr(c);
        tr.deleteTask(taskId);

        for (auto& pair : g_tempPlan) {
            if (pair.first.first != g_userId) continue;
            auto& plan = pair.second;
            plan.scheduled.erase(
                std::remove_if(plan.scheduled.begin(), plan.scheduled.end(),
                    [taskId](const WeeklyPlan::ScheduledItem& s) { return s.task.taskId == taskId; }),
                plan.scheduled.end());
            plan.unscheduled.erase(
                std::remove_if(plan.unscheduled.begin(), plan.unscheduled.end(),
                    [taskId](const FlexibleTask& t) { return t.taskId == taskId; }),
                plan.unscheduled.end());
            plan.postponed.erase(
                std::remove_if(plan.postponed.begin(), plan.postponed.end(),
                    [taskId](const FlexibleTask& t) { return t.taskId == taskId; }),
                plan.postponed.end());
        }
        g_pool->put(c);
        res.set_content("{\"ok\":true}", "application/json");
        });

    srv.Get("/api/health/get", [](auto& req, auto& res) {
        if (!g_userId) { res.set_content("{}", "application/json"); return; }
        std::string week = req.has_param("week") ? req.get_param_value("week") : "";
        auto c = g_pool->get(); HealthRepository hr(c);
        auto h = hr.get(g_userId, week);
        g_pool->put(c);
        if (h.has_value()) {
            json j = { {"tiredness",h->tiredness},{"general",h->general},{"emotion",h->emotion},{"mood",h->mood},{"apathy",h->apathy} };
            res.set_content(j.dump(), "application/json");
        }
        else {
            res.set_content("{}", "application/json");
        }
        });
    srv.Post("/api/health", [](auto& req, auto& res) {
        if (!g_userId) {
            res.set_content("{\"ok\":false,\"error\":\"Not authorized\"}", "application/json");
            return;
        }
        try {
            auto j = json::parse(req.body);
            auto c = g_pool->get();
            HealthRepository hr(c);
            hr.saveOrUpdate(g_userId, j["week"], j["tiredness"], j["general"],
                j["emotion"], j["mood"], j["apathy"]);
            g_pool->put(c);
            res.set_content("{\"ok\":true}", "application/json");
        }
        catch (const std::exception& e) {
            json err = { {"ok", false}, {"error", std::string(e.what())} };
            res.set_content(err.dump(), "application/json");
        }
        });

    srv.Get("/distribute", [](auto& req, auto& res) {
        if (!g_userId) { res.set_redirect("/"); return; }
        std::string week = req.has_param("week") ? req.get_param_value("week") : getCurrentWeek();

        auto c = g_pool->get();
        TaskRepository tr(c);
        HealthRepository hr(c);
        UserRepository ur(c);
        IdeaRepository ir(c);

        std::string currentWeekMonday = getCurrentWeek();
        bool isCurrentWeek = (week == currentWeekMonday);

        saveTempTasksToDB(g_userId);

        auto allFlexible = tr.getFlexible(g_userId, week);
        auto postponed = tr.getPostponed(g_userId);
        allFlexible.insert(allFlexible.end(), postponed.begin(), postponed.end());

        std::vector<WeeklyPlan::ScheduledItem> lockedItems;
        std::set<int> lockedTaskIds;
        if (isCurrentWeek) {
            std::time_t now = std::time(nullptr);
            std::tm nowTm;
            localtime_s(&nowTm, &now);
            int realCurrentDay = nowTm.tm_wday;
            if (realCurrentDay == 0) realCurrentDay = 7;

            auto itPlan = g_tempPlan.find({ g_userId, week });
            if (itPlan != g_tempPlan.end()) {
                for (const auto& s : itPlan->second.scheduled) {
                    if (s.dayId < realCurrentDay&& s.task.category != "fixed") {
                        lockedItems.push_back(s);
                        lockedTaskIds.insert(s.task.taskId);
                    }
                }
            }
        }

        allFlexible.erase(
            std::remove_if(allFlexible.begin(), allFlexible.end(),
                [&](const FlexibleTask& t) { return lockedTaskIds.count(t.taskId) > 0; }),
            allFlexible.end());

        SchedulerEngine se;
        auto newPlan = se.plan(g_userId, week, tr, hr, ur, ir);

        newPlan.scheduled.insert(newPlan.scheduled.end(), lockedItems.begin(), lockedItems.end());

        g_tempPlan[{g_userId, week}] = newPlan;

        g_pool->put(c);
        res.set_redirect("/edit?week=" + week);
        });

    srv.Get("/save-and-exit", [](auto& req, auto& res) {
        if (!g_userId) { res.set_redirect("/"); return; }
        saveTempTasksToDB(g_userId);

        for (auto& pair : g_tempPlan) {
            if (pair.first.first == g_userId) {
                g_approvedPlan[pair.first] = pair.second;
            }
        }

        for (auto it = g_tempPlan.begin(); it != g_tempPlan.end(); ) {
            if (it->first.first == g_userId) {
                it = g_tempPlan.erase(it);
            }
            else {
                ++it;
            }
        }

        res.set_redirect("/index");
        });

    srv.Get("/api/schedule", [](auto& req, auto& res) {
        if (!g_userId) { res.set_content("{}", "application/json"); return; }
        std::string week = req.has_param("week") ? req.get_param_value("week") : getCurrentWeek();
        bool isEdit = req.has_param("source") && req.get_param_value("source") == "edit";
        auto& planMap = isEdit ? g_tempPlan : g_approvedPlan;
        auto it = planMap.find({ g_userId, week });
        json j;
        j["scheduled"] = json::array();
        if (it != planMap.end()) {
            for (auto& s : it->second.scheduled) {
                j["scheduled"].push_back({
                    {"id", s.task.taskId},
                    {"name", s.task.name},
                    {"day", s.dayId},
                    {"time", s.time},
                    {"duration", s.task.duration},
                    {"category", s.task.category},
                    {"color", s.task.color}
                    });
            }
        }
        res.set_content(j.dump(), "application/json");
        });

    srv.Post("/api/tasks", [](auto& req, auto& res) {
        if (!g_userId) { res.set_content("{}", "application/json"); return; }
        try {
            auto j = json::parse(req.body);
            auto c = g_pool->get(); TaskRepository tr(c);
            Task t;
            t.userId = g_userId;
            t.name = j["name"];
            t.week = j.value("week", getCurrentWeek());
            t.notes = j.value("notes", "");
            t.activityLevel = j.value("activity_level", 5);
            t.taskType = j["type"];
            t.taskView = j.value("view", "individual");
            int id = tr.create(t);

            if (t.taskType == "fixed") {
                FixedTask ft;
                ft.taskId = id;
                ft.date = j.value("date", t.week);
                ft.beginTime = j.value("begin", "09:00");
                ft.endTime = j.value("end", "10:00");
                tr.createFixed(ft);
            }
            else {
                FlexibleTask ft;
                ft.taskId = id;
                ft.duration = j.value("duration", 1.0);
                ft.category = j.value("category", "work");
                ft.pleasure = j.value("pleasure", 5);
                ft.importance = j.value("importance", 5);
                ft.deadline = j.value("deadline", "");
                if (j.contains("days"))
                    for (auto& d : j["days"]) ft.preferredDays.push_back(d.get<int>());
                tr.createFlexible(ft);
                if (!ft.preferredDays.empty())
                    tr.addPreferredDays(id, ft.preferredDays);
            }
            g_pool->put(c);
            res.set_content("{\"ok\":true,\"id\":" + std::to_string(id) + "}", "application/json");
        }
        catch (const std::exception& e) {
            res.set_content("{\"ok\":false,\"error\":\"" + std::string(e.what()) + "\"}", "application/json");
        }
        });

    srv.Post("/task-create", [](auto& req, auto& res) {
        if (!g_userId) { res.set_redirect("/"); return; }
        try {
            std::string name = req.get_param_value("name");
            std::string type = req.get_param_value("type");
            std::string category = req.get_param_value("category");
            int activity = req.has_param("activity_level") ? std::stoi(req.get_param_value("activity_level")) : 5;
            std::string week = req.has_param("week") ? req.get_param_value("week") : "";
            if (week.empty()) week = getCurrentWeek();

            auto c = g_pool->get(); TaskRepository tr(c);
            Task t; t.userId = g_userId; t.name = name; t.week = week;
            t.notes = ""; t.activityLevel = activity;
            t.taskType = type; t.taskView = "individual";
            int id = tr.create(t);

            if (type == "fixed") {
                FixedTask ft; ft.taskId = id;
                ft.date = req.has_param("date") ? req.get_param_value("date") : week;
                ft.beginTime = req.has_param("begin") ? req.get_param_value("begin") : "09:00";
                ft.endTime = req.has_param("end") ? req.get_param_value("end") : "10:00";
                tr.createFixed(ft);
            }
            else {
                FlexibleTask ft; ft.taskId = id;
                ft.duration = req.has_param("duration") ? std::stod(req.get_param_value("duration")) : 1.0;
                ft.category = category;
                ft.importance = req.has_param("importance") ? std::stoi(req.get_param_value("importance")) : 5;
                ft.pleasure = req.has_param("pleasure") ? std::stoi(req.get_param_value("pleasure")) : 5;
                ft.deadline = req.has_param("deadline") ? req.get_param_value("deadline") : "";
                if (req.has_param("days")) {
                    std::string daysStr = req.get_param_value("days");
                    std::stringstream ss(daysStr);
                    std::string token;
                    while (std::getline(ss, token, ',')) {
                        if (!token.empty()) ft.preferredDays.push_back(std::stoi(token));
                    }
                }
                tr.createFlexible(ft);
                if (!ft.preferredDays.empty())
                    tr.addPreferredDays(id, ft.preferredDays);
            }
            g_pool->put(c);
            res.set_redirect("/edit?week=" + week);
        }
        catch (const std::exception& e) {
            res.set_content(errorPage(std::string("Ошибка при создании задачи: ") + e.what()), "text/html; charset=utf-8");
        }
        });

    srv.Get("/api/suggestions/ideas", [](auto& req, auto& res) {
        if (!g_userId) { res.set_content("[]", "application/json"); return; }
        std::string week = req.has_param("week") ? req.get_param_value("week") : getCurrentWeek();
        auto c = g_pool->get();
        HealthRepository hr(c);
        IdeaRepository ir(c);
        auto health = hr.get(g_userId, week);
        json ideas = json::array();
        auto allIdeas = ir.getAll();
        if (health.has_value()) {
            for (const auto& idea : allIdeas) {
                bool match = false;
                if (health->tiredness >= 7 && idea.energyRestored >= 6) match = true;
                if (health->mood <= 4 && idea.moodBoost >= 7) match = true;
                if (match) {
                    ideas.push_back({
                        {"id", idea.id}, {"name", idea.name}, {"description", idea.description},
                        {"length", idea.length}, {"color", idea.color},
                        {"energy", idea.energyRestored}, {"mood", idea.moodBoost}
                        });
                }
            }
        }
        else {
            for (const auto& idea : allIdeas) {
                ideas.push_back({
                    {"id", idea.id}, {"name", idea.name}, {"description", idea.description},
                    {"length", idea.length}, {"color", idea.color},
                    {"energy", idea.energyRestored}, {"mood", idea.moodBoost}
                    });
            }
        }
        g_pool->put(c);
        res.set_content(ideas.dump(), "application/json");
        });

    srv.Get("/api/suggestions/add", [](auto& req, auto& res) {
        if (!g_userId) {
            res.set_content("{\"ok\":false,\"error\":\"not authorized\"}", "application/json");
            return;
        }
        try {
            int ideaId = std::stoi(req.get_param_value("idea_id"));
            std::string week = req.has_param("week") ? req.get_param_value("week") : getCurrentWeek();

            auto c = g_pool->get();
            IdeaRepository ir(c);
            auto idea = ir.getById(ideaId);
            if (!idea.has_value()) {
                g_pool->put(c);
                res.set_content("{\"ok\":false,\"error\":\"idea not found\"}", "application/json");
                return;
            }

            // Временная задача
            FlexibleTask ft;
            ft.taskId = getNextTempTaskId();
            ft.name = idea->name;
            ft.duration = idea->length;
            ft.deadline = "";
            ft.importance = 10;
            ft.pleasure = 10;
            ft.color = idea->color;

            g_tempPlan[{g_userId, week}].unscheduled.push_back(ft);
            g_pool->put(c);

            json resp = { {"ok", true}, {"task_id", ft.taskId} };
            res.set_content(resp.dump(), "application/json");
        }
        catch (const std::exception& e) {
            res.set_content("{\"ok\":false,\"error\":\"" + std::string(e.what()) + "\"}", "application/json");
        }
        });

    srv.Get("/api/tasks/all", [](auto& req, auto& res) {
        if (!g_userId) { res.set_content("{}", "application/json"); return; }
        std::string week = req.has_param("week") ? req.get_param_value("week") : getCurrentWeek();
        bool isEdit = req.has_param("source") && req.get_param_value("source") == "edit";
        auto& planMap = isEdit ? g_tempPlan : g_approvedPlan;
        auto it = planMap.find({ g_userId, week });
        json j;
        auto c = g_pool->get();
        TaskRepository tr(c);

        if (it != planMap.end()) {
            auto& plan = it->second;
            j["fixed"] = json::array();
            j["flexible"] = json::array();
            j["postponed"] = json::array();
            j["scheduled"] = json::array();

            std::set<int> scheduledIds, scheduledFixedIds;
            for (auto& s : plan.scheduled) {
                scheduledIds.insert(s.task.taskId);
                if (s.task.category == "fixed") scheduledFixedIds.insert(s.task.taskId);
            }

            std::set<int> unscheduledIds;
            for (auto& u : plan.unscheduled) unscheduledIds.insert(u.taskId);
            std::set<int> postponedIds;
            for (auto& p : plan.postponed) postponedIds.insert(p.taskId);

            auto allFlexible = tr.getFlexible(g_userId, week);
            for (auto& f : allFlexible) {
                if (scheduledIds.count(f.taskId) || unscheduledIds.count(f.taskId) || postponedIds.count(f.taskId))
                    continue;
                j["flexible"].push_back({
                    {"id", f.taskId}, {"name", f.name},
                    {"duration", f.duration}, {"category", f.category},
                    {"importance", f.importance}, {"pleasure", f.pleasure}
                    });
            }

            for (auto& u : plan.unscheduled) {
                j["flexible"].push_back({
                    {"id", u.taskId}, {"name", u.name},
                    {"duration", u.duration}, {"category", u.category},
                    {"importance", u.importance}, {"pleasure", u.pleasure}
                    });
            }
            for (auto& p : plan.postponed) {
                j["postponed"].push_back({
                    {"id", p.taskId}, {"name", p.name}, {"category", p.category}
                    });
            }

            auto fixedTasks = tr.getFixed(g_userId, week);
            for (auto& f : fixedTasks) {
                if (scheduledFixedIds.find(f.taskId) == scheduledFixedIds.end()) {
                    j["fixed"].push_back({
                        {"id", f.taskId}, {"name", f.name},
                        {"date", f.date}, {"begin", f.beginTime}, {"end", f.endTime}
                        });
                }
            }
        }
        else {
            auto fixed = tr.getFixed(g_userId, week);
            auto flexible = tr.getFlexible(g_userId, week);
            auto postponed = tr.getPostponed(g_userId);
            j["fixed"] = json::array();
            for (auto& f : fixed)
                j["fixed"].push_back({ {"id", f.taskId}, {"name", f.name}, {"date", f.date}, {"begin", f.beginTime}, {"end", f.endTime} });
            j["flexible"] = json::array();
            for (auto& f : flexible)
                j["flexible"].push_back({ {"id", f.taskId}, {"name", f.name}, {"duration", f.duration}, {"category", f.category}, {"importance", f.importance}, {"pleasure", f.pleasure} });
            j["postponed"] = json::array();
            for (auto& p : postponed)
                j["postponed"].push_back({ {"id", p.taskId}, {"name", p.name}, {"category", p.category} });
            j["scheduled"] = json::array();
        }
        g_pool->put(c);
        res.set_content(j.dump(), "application/json");
        });
}

int main() {
    try {
        g_pool = new ConnectionPool(5);
        g_pool->init("localhost", 9999, "Calendar", "postgres", "hiLia2024");
        httplib::Server srv;
        setupRoutes(srv);
        startPythonAnalytics();
        ShellExecuteA(nullptr, "open", "http://localhost:8088", nullptr, nullptr, SW_SHOW);
        srv.listen("0.0.0.0", 8088);
    }
    catch (const std::exception& e) {
        std::cerr << "FATAL: " << e.what() << std::endl;
        system("pause"); return 1;
    }
    stopPythonAnalytics();
    delete g_pool;
    return 0;
}