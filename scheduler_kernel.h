#pragma once
#include "../models/models.h"
#include "../repositories/task_repository.h"
#include "../repositories/health_repository.h"
#include "../repositories/user_repository.h"
#include "../repositories/idea_repository.h"
#include "checker.h"
#include "priority_calculator.h"
#include "balance_tracker.h"
#include <algorithm>
#include <map>
#include <ctime>
#include <cstring>
#include <sstream>

class SchedulerEngine {
    Checker checker_;
    PriorityCalculator priority_;
    BalanceTracker balance_;
    static constexpr double GAP_HOURS = 1.0;

    int getCurrentDay() {
        std::time_t now = std::time(nullptr);
        std::tm tm_buf;
        localtime_s(&tm_buf, &now);
        int w = tm_buf.tm_wday;
        return w == 0 ? 7 : w;
    }

    int findBestDayGreedy(const FlexibleTask& task,
        const std::vector<int>& preferred,
        std::map<int, double>& occupancy,
        int currentDay,
        const User& user) {
        std::vector<int> days = preferred.empty() ?
            std::vector<int>{1, 2, 3, 4, 5, 6, 7} : preferred;

        int best = -1;
        double minOcc = 999.0;

        for (int d : days) {
            if (d < currentDay) continue;
            if (!priority_.canScheduleOnDay(task, d, currentDay)) continue;
            if (occupancy[d] + task.duration + GAP_HOURS <= 8.0 && occupancy[d] < minOcc) {
                minOcc = occupancy[d];
                best = d;
            }
        }
        return best;
    }

    std::string chooseTime(int dayId, const FlexibleTask& task,
        std::map<int, double>& occ, const User& user) {
        double startHour = 9.0;
        if (user.chronotype == "evening" && task.importance >= 7) {
            startHour = 14.0;
            if (occ[dayId] > 0) startHour = 14.0 + occ[dayId];
        }
        else {
            if (occ[dayId] > 0) startHour = 9.0 + occ[dayId];
        }
        if (task.importance >= 8) startHour = 9.0;

        int hours = static_cast<int>(startHour);
        int mins = static_cast<int>((startHour - hours) * 60);
        char buf[6];
        snprintf(buf, sizeof(buf), "%02d:%02d", hours, mins);
        return std::string(buf);
    }

    bool hasFixedConflict(int day, double start, double duration,
        const std::vector<FixedTask>& fixed) {
        double end = start + duration;
        for (const auto& f : fixed) {
            std::tm tm = {};
            sscanf(f.date.c_str(), "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday);
            tm.tm_year -= 1900; tm.tm_mon -= 1;
            std::mktime(&tm);
            int wday = tm.tm_wday;
            if (wday == 0) wday = 7;
            if (wday != day) continue;

            int fh1, fm1, fh2, fm2;
            sscanf(f.beginTime.c_str(), "%d:%d", &fh1, &fm1);
            sscanf(f.endTime.c_str(), "%d:%d", &fh2, &fm2);
            double fStart = fh1 + fm1 / 60.0;
            double fEnd = fh2 + fm2 / 60.0;

            if (!(end <= fStart || start >= fEnd)) {
                return true;
            }
        }
        return false;
    }

public:
    WeeklyPlan plan(int userId, const std::string& week,
        TaskRepository& tr, HealthRepository& hr,
        UserRepository& ur, IdeaRepository& ir) {
        WeeklyPlan p;
        p.weekStart = week;
        p.userId = userId;

        auto fixed = tr.getFixed(userId, week);
        auto flexible = tr.getFlexible(userId, week);
        auto postponed = tr.getPostponed(userId);
        flexible.insert(flexible.end(), postponed.begin(), postponed.end());

        User user = ur.getById(userId);
        auto health = hr.get(userId, week);
        auto targets = ur.getBalance(userId);

        balance_.setTargets(targets);

        std::string currentWeekMonday;
        {
            auto now = std::chrono::system_clock::now();
            std::time_t tt = std::chrono::system_clock::to_time_t(now);
            std::tm tm = *std::localtime(&tt);
            int dow = tm.tm_wday;
            if (dow == 0) dow = 7;
            tm.tm_mday -= (dow - 1);
            std::mktime(&tm);
            char buf[11];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
            currentWeekMonday = std::string(buf);
        }

        int currentDay;
        if (week > currentWeekMonday) {
            currentDay = 0;
        }
        else if (week == currentWeekMonday) {
            currentDay = getCurrentDay();
        }
        else {
            currentDay = 8;
        }
        for (auto& task : flexible) {
            if (task.preferredDays.empty()) {
                for (int d = currentDay + 1; d <= 7; d++) {
                    task.preferredDays.push_back(d);
                }
            }
        }

        for (const auto& t : flexible) {
            priority_.updateCategoryStats(t.category, t.importance, t.pleasure);
        }

        std::sort(flexible.begin(), flexible.end(), [&](auto& a, auto& b) {
            return priority_.calc(a, health.has_value() ? &*health : nullptr, user) >
                priority_.calc(b, health.has_value() ? &*health : nullptr, user);
            });

        std::map<int, double> occupancy;

        for (auto& task : flexible) {
            if (!balance_.canAdd(task.category, task.duration)) {
                p.warnings.push_back("Категория \"" + task.category +
                    "\" перегружена — задача \"" + task.name + "\" отложена");
                p.postponed.push_back(task);
                continue;
            }

            int day = findBestDayGreedy(task, task.preferredDays, occupancy, currentDay, user);
            if (day == -1) {
                p.warnings.push_back("Нет свободного времени для задачи \"" + task.name + "\"");
                p.postponed.push_back(task);
                continue;
            }

            double startHour = 9.0;
            if (occupancy[day] > 0) startHour = 9.0 + occupancy[day];
            if (user.chronotype == "evening" && task.importance >= 7) {
                startHour = 14.0;
                if (occupancy[day] > 0) startHour = 14.0 + occupancy[day];
            }
            if (task.importance >= 8) startHour = 9.0;

            if (hasFixedConflict(day, startHour, task.duration, fixed)) {
                p.warnings.push_back("Задача \"" + task.name +
                    "\" конфликтует с фиксированной задачей – пропущена");
                p.postponed.push_back(task);
                continue;
            }

            WeeklyPlan::ScheduledItem si;
            si.task = task;
            si.dayId = day;
            si.time = chooseTime(day, task, occupancy, user);
            p.scheduled.push_back(si);
            occupancy[day] += task.duration + GAP_HOURS;
            balance_.add(task.category, task.duration);
        }

        for (auto& f : fixed) {
            std::tm tm = {};
            sscanf(f.date.c_str(), "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday);
            tm.tm_year -= 1900;
            tm.tm_mon -= 1;
            std::mktime(&tm);
            int wday = tm.tm_wday;
            if (wday == 0) wday = 7;

            FlexibleTask ft;
            ft.taskId = f.taskId;
            ft.name = f.name;
            ft.duration = 1.0;
            ft.category = "fixed";
            ft.pleasure = 5;
            ft.importance = 5;

            WeeklyPlan::ScheduledItem si;
            si.task = ft;
            si.dayId = wday;
            si.time = f.beginTime + " - " + f.endTime;
            p.scheduled.push_back(si);
            occupancy[wday] += 1.0 + GAP_HOURS;
        }

        if (health.has_value() && health->tiredness >= 6) {
            auto ideas = ir.getAll();
            for (auto& idea : ideas) {
                if (health->tiredness >= 7 && idea.energyRestored >= 6)
                    p.suggestions.push_back(idea);
                else if (health->mood <= 4 && idea.moodBoost >= 7)
                    p.suggestions.push_back(idea);
            }
            if (p.suggestions.size() > 3) p.suggestions.resize(3);
        }

        p.balanceSummary = balance_.summary();
        return p;
    }
    
};