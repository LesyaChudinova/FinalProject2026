#include <windows.h>
#include <shellapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>

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

std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        std::cerr << "[WARN] Cannot open file: " << path << std::endl;
        return "";
    }
    std::stringstream ss; ss << f.rdbuf(); return ss.str();
}

std::string errorPage(const std::string& message) {
    std::string html;
    html += "<!DOCTYPE html><html lang='ru'><head><meta charset='UTF-8'><title>Error</title>";
    html += "<link rel='stylesheet' href='/style.css'></head><body><div class='container'>";
    html += "<h1>Error</h1><p style='color:red;text-align:center;'>";
    html += message;
    html += "</p><a href='javascript:history.back()'><button class='btn'>Back</button></a>";
    html += "</div></body></html>";
    return html;
}

void setupRoutes(httplib::Server& srv) {


    srv.Get("/style.css", [](auto& req, auto& res) {
        std::cout << "GET /style.css" << std::endl;
        res.set_content(readFile("front/style.css"), "text/css; charset=utf-8");
        });

    srv.Get("/", [](auto& req, auto& res) {
        std::cout << "GET / (user=" << g_userId << ")" << std::endl;
        if (g_userId) { res.set_redirect("/welcome"); return; }
        res.set_content(readFile("front/login.html"), "text/html; charset=utf-8");
        });

    srv.Get("/register", [](auto& req, auto& res) {
        std::cout << "GET /register (user=" << g_userId << ")" << std::endl;
        if (g_userId) { res.set_redirect("/welcome"); return; }
        res.set_content(readFile("front/register.html"), "text/html; charset=utf-8");
        });

    srv.Get("/welcome", [](auto& req, auto& res) {
        std::cout << "GET /welcome (user=" << g_userId << ")" << std::endl;
        if (!g_userId) { res.set_redirect("/"); return; }
        res.set_content(readFile("front/welcome.html"), "text/html; charset=utf-8");
        });

    srv.Get("/profile", [](auto& req, auto& res) {
        std::cout << "GET /profile (user=" << g_userId << ")" << std::endl;
        if (!g_userId) { res.set_redirect("/"); return; }
        res.set_content(readFile("front/profile.html"), "text/html; charset=utf-8");
        });

    srv.Get("/tasks", [](auto& req, auto& res) {
        std::cout << "GET /tasks (user=" << g_userId << ")" << std::endl;
        if (!g_userId) { res.set_redirect("/"); return; }
        res.set_content(readFile("front/tasks.html"), "text/html; charset=utf-8");
        });

    srv.Get("/schedule", [](auto& req, auto& res) {
        std::cout << "GET /schedule (user=" << g_userId << ")" << std::endl;
        if (!g_userId) { res.set_redirect("/"); return; }
        res.set_content(readFile("front/schedule.html"), "text/html; charset=utf-8");
        });

    srv.Get("/health", [](auto& req, auto& res) {
        std::cout << "GET /health (user=" << g_userId << ")" << std::endl;
        if (!g_userId) { res.set_redirect("/"); return; }
        res.set_content(readFile("front/health.html"), "text/html; charset=utf-8");
        });


    srv.Post("/login", [](auto& req, auto& res) {
        try {
            std::string email = req.get_param_value("email");
            std::string password = req.get_param_value("password");

            std::cout << "[LOGIN] email=" << email << std::endl;

            auto c = g_pool->get();
            UserRepository ur(c);
            auto result = ur.loginUser(email, password);
            g_pool->put(c);

            std::cout << "[LOGIN] ok=" << result.ok;
            if (!result.ok) std::cout << " error=" << result.error;
            std::cout << std::endl;

            if (result.ok) {
                g_userId = result.user.id;
                res.set_redirect("/welcome");
            }
            else {
                res.set_content(errorPage(result.error), "text/html; charset=utf-8");
            }
        }
        catch (const std::exception& e) {
            std::cerr << "[LOGIN] EXCEPTION: " << e.what() << std::endl;
            res.set_content(errorPage(std::string("Server error: ") + e.what()), "text/html; charset=utf-8");
        }
        });


    srv.Post("/register", [](auto& req, auto& res) {
        try {
            std::string email = req.get_param_value("email");
            std::string password = req.get_param_value("password");
            std::string nickname = req.get_param_value("nickname");
            std::string age_str = req.get_param_value("age");

            std::cout << "[REGISTER] email=" << email << " nick=" << nickname << std::endl;

            User u;
            u.email = email;
            u.nickname = nickname;
            u.age = age_str.empty() ? 25 : std::stoi(age_str);
            u.goals = "not specified";
            u.concentrationTime = "45";
            u.lifestyle = "sedentary";
            u.personalityType = "ambivert";
            u.chronotype = "neutral";

            auto c = g_pool->get();
            UserRepository ur(c);
            auto result = ur.registerUser(u, password);
            g_pool->put(c);

            std::cout << "[REGISTER] ok=" << result.ok;
            if (!result.ok) for (auto& e : result.errors) std::cout << " | " << e;
            std::cout << std::endl;

            if (result.ok) {
                res.set_redirect("/?registered=1");
            }
            else {
                std::string msg;
                for (const auto& e : result.errors) msg += e + "<br>";
                res.set_content(errorPage(msg), "text/html; charset=utf-8");
            }
        }
        catch (const std::exception& e) {
            std::cerr << "[REGISTER] EXCEPTION: " << e.what() << std::endl;
            res.set_content(errorPage(std::string("Server error: ") + e.what()), "text/html; charset=utf-8");
        }
        });

    srv.Get("/logout", [](auto& req, auto& res) {
        std::cout << "[LOGOUT] user=" << g_userId << std::endl;
        g_userId = 0;
        res.set_redirect("/");
        });

    // ==================== ПРОФИЛЬ ====================

    srv.Get("/api/profile", [](auto& req, auto& res) {
        try {
            std::cout << "[API] GET /api/profile (user=" << g_userId << ")" << std::endl;
            if (!g_userId) { res.set_content("{}", "application/json"); return; }
            auto c = g_pool->get(); UserRepository ur(c); User u = ur.getById(g_userId); g_pool->put(c);
            json j = { {"nickname",u.nickname},{"email",u.email},{"age",u.age},{"goals",u.goals},{"lifestyle",u.lifestyle},{"personality",u.personalityType},{"chronotype",u.chronotype},{"concentration",u.concentrationTime} };
            res.set_content(j.dump(), "application/json");
        }
        catch (const std::exception& e) {
            std::cerr << "[API PROFILE] " << e.what() << std::endl;
        }
        });

    srv.Post("/api/profile", [](auto& req, auto& res) {
        try {
            std::cout << "[API] POST /api/profile" << std::endl;
            if (!g_userId) { res.set_content("{}", "application/json"); return; }
            auto j = json::parse(req.body); auto c = g_pool->get(); UserRepository ur(c);
            User u = ur.getById(g_userId);
            u.nickname = j.value("nickname", u.nickname);
            u.age = j.value("age", u.age);
            u.goals = j.value("goals", u.goals);
            u.lifestyle = j.value("lifestyle", u.lifestyle);
            u.personalityType = j.value("personality", u.personalityType);
            u.chronotype = j.value("chronotype", u.chronotype);
            u.concentrationTime = j.value("concentration", u.concentrationTime);
            ur.update(g_userId, u); g_pool->put(c);
            res.set_content("{\"ok\":true}", "application/json");
        }
        catch (const std::exception& e) {
            std::cerr << "[API PROFILE POST] " << e.what() << std::endl;
        }
        });

    srv.Post("/api/tasks", [](auto& req, auto& res) {
        try {
            std::cout << "[API] POST /api/tasks" << std::endl;
            if (!g_userId) { res.set_content("{}", "application/json"); return; }
            auto j = json::parse(req.body); auto c = g_pool->get(); TaskRepository tr(c);
            Task t; t.userId = g_userId; t.name = j["name"]; t.week = j["week"];
            t.notes = j.value("notes", ""); t.activityLevel = j.value("activity", 5);
            t.taskType = j["type"]; t.taskView = j.value("view", "individual");
            int id = tr.create(t);
            if (t.taskType == "fixed") {
                FixedTask ft; ft.taskId = id; ft.date = j["date"]; ft.beginTime = j["begin"]; ft.endTime = j["end"]; tr.createFixed(ft);
            }
            else {
                FlexibleTask ft; ft.taskId = id; ft.duration = j.value("duration", 1.0);
                ft.category = j.value("category", "work"); ft.pleasure = j.value("pleasure", 5);
                ft.importance = j.value("importance", 5); ft.deadline = j.value("deadline", "");
                for (auto& d : j.value("days", json::array())) ft.preferredDays.push_back(d);
                tr.createFlexible(ft);
            }
            g_pool->put(c);
            res.set_content("{\"ok\":true,\"id\":" + std::to_string(id) + "}", "application/json");
        }
        catch (const std::exception& e) {
            std::cerr << "[API TASKS] " << e.what() << std::endl;
        }
        });

    srv.Get("/api/tasks/list", [](auto& req, auto& res) {
        try {
            if (!g_userId) { res.set_content("[]", "application/json"); return; }
            std::string week = req.has_param("week") ? req.get_param_value("week") : "2026-04-21";
            auto c = g_pool->get(); TaskRepository tr(c);
            auto fixed = tr.getFixed(g_userId, week);
            auto flexible = tr.getFlexible(g_userId, week);
            g_pool->put(c);
            json arr = json::array();
            for (auto& f : fixed) arr.push_back({ {"id",f.taskId},{"type","fixed"},{"date",f.date},{"begin",f.beginTime},{"end",f.endTime} });
            for (auto& f : flexible) arr.push_back({ {"id",f.taskId},{"type","flexible"},{"name",f.name},{"duration",f.duration},{"category",f.category},{"importance",f.importance},{"pleasure",f.pleasure} });
            res.set_content(arr.dump(), "application/json");
        }
        catch (const std::exception& e) {
            std::cerr << "[API TASKS LIST] " << e.what() << std::endl;
        }
        });


    srv.Get("/api/schedule", [](auto& req, auto& res) {
        try {
            if (!g_userId) { res.set_content("{}", "application/json"); return; }
            std::string week = req.has_param("week") ? req.get_param_value("week") : "2026-04-21";
            auto c = g_pool->get(); TaskRepository tr(c); HealthRepository hr(c); UserRepository ur(c); IdeaRepository ir(c);
            SchedulerEngine se; auto plan = se.plan(g_userId, week, tr, hr, ur, ir); g_pool->put(c);
            json j; j["week"] = week; j["scheduled"] = json::array();
            for (auto& s : plan.scheduled) j["scheduled"].push_back({ {"name",s.task.name},{"day",s.dayId},{"time",s.time},{"duration",s.task.duration},{"category",s.task.category} });
            j["unscheduled"] = json::array();
            for (auto& u : plan.unscheduled) j["unscheduled"].push_back({ {"name",u.name},{"category",u.category} });
            j["warnings"] = plan.warnings; j["suggestions"] = json::array();
            for (auto& s : plan.suggestions) j["suggestions"].push_back({ {"name",s.name},{"description",s.description},{"color",s.color},{"length",s.length} });
            j["balance"] = plan.balanceSummary;
            res.set_content(j.dump(), "application/json");
        }
        catch (const std::exception& e) {
            std::cerr << "[API SCHEDULE] " << e.what() << std::endl;
        }
        });

    srv.Post("/api/schedule/postpone", [](auto& req, auto& res) {
        try {
            if (!g_userId) { res.set_content("{}", "application/json"); return; }
            auto j = json::parse(req.body); auto c = g_pool->get(); TaskRepository tr(c);
            tr.postpone(j["task_id"], "next_week"); g_pool->put(c);
            res.set_content("{\"ok\":true}", "application/json");
        }
        catch (const std::exception& e) {
            std::cerr << "[API POSTPONE] " << e.what() << std::endl;
        }
        });


    srv.Post("/api/health", [](auto& req, auto& res) {
        try {
            if (!g_userId) { res.set_content("{}", "application/json"); return; }
            auto j = json::parse(req.body);
            HealthState h; h.userId = g_userId; h.week = j["week"];
            h.tiredness = j["tiredness"]; h.general = j["general"];
            h.emotion = j["emotion"]; h.mood = j["mood"]; h.apathy = j["apathy"];
            auto c = g_pool->get(); HealthRepository hr(c); hr.save(h); g_pool->put(c);
            res.set_content("{\"ok\":true}", "application/json");
        }
        catch (const std::exception& e) {
            std::cerr << "[API HEALTH] " << e.what() << std::endl;
        }
        });


    srv.Get("/api/balance", [](auto& req, auto& res) {
        try {
            if (!g_userId) { res.set_content("[]", "application/json"); return; }
            auto c = g_pool->get(); UserRepository ur(c); auto b = ur.getBalance(g_userId); g_pool->put(c);
            json arr = json::array();
            for (auto& x : b) arr.push_back({ {"category",x.category},{"percent",x.targetPercent} });
            res.set_content(arr.dump(), "application/json");
        }
        catch (const std::exception& e) {
            std::cerr << "[API BALANCE] " << e.what() << std::endl;
        }
        });

    srv.Post("/api/balance", [](auto& req, auto& res) {
        try {
            if (!g_userId) { res.set_content("{}", "application/json"); return; }
            auto j = json::parse(req.body); std::vector<BalanceTarget> b;
            for (auto& item : j) { BalanceTarget bt; bt.userId = g_userId; bt.category = item["category"]; bt.targetPercent = item["percent"]; b.push_back(bt); }
            auto c = g_pool->get(); UserRepository ur(c); ur.saveBalance(g_userId, b); g_pool->put(c);
            res.set_content("{\"ok\":true}", "application/json");
        }
        catch (const std::exception& e) {
            std::cerr << "[API BALANCE POST] " << e.what() << std::endl;
        }
        });
}

int main() {
    try {
        g_pool = new ConnectionPool(5);
        g_pool->init("localhost", 9999, "Calendar", "postgres", "hiLia2024");
        httplib::Server srv;
        setupRoutes(srv);
        ShellExecuteA(nullptr, "open", "http://localhost:8088", nullptr, nullptr, SW_SHOW);
        srv.listen("0.0.0.0", 8088);
    }
    catch (const std::exception& e) {
        std::cerr << "FATAL: " << e.what() << std::endl;
        system("pause");
        return 1;
    }
    delete g_pool;
    return 0;
}