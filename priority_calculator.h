#pragma once
#include "../models/models.h"
#include <string>
#include <ctime>
#include <map>

class PriorityCalculator {
private:
    std::map<std::string, double> categoryBonus_;

    int daysUntilDeadline(const std::string& deadline) {
        if (deadline.empty()) return 999; // без дедлайна – самый низкий приоритет
        std::tm tmDeadline = {};
        sscanf(deadline.c_str(), "%d-%d-%d", &tmDeadline.tm_year, &tmDeadline.tm_mon, &tmDeadline.tm_mday);
        tmDeadline.tm_year -= 1900;
        tmDeadline.tm_mon -= 1;
        std::time_t deadlineTime = std::mktime(&tmDeadline);
        std::time_t now = std::time(nullptr);
        double diff = std::difftime(deadlineTime, now) / (60 * 60 * 24);
        return static_cast<int>(diff);
    }

public:
    void updateCategoryStats(const std::string& category, double importance, double pleasure) {
        if (importance >= 7 && pleasure >= 7) {
            categoryBonus_[category] += 0.05;
        }
    }

    double calc(const FlexibleTask& task, const HealthState* health, const User& user) {
    
        double score = task.importance * 2.0 + task.pleasure;

        int daysLeft = daysUntilDeadline(task.deadline);
        if (daysLeft <= 0) {
            score += 200.0;            
        }
        else if (daysLeft <= 1) {
            score += 100.0;          
        }
        else if (daysLeft <= 3) {
            score += 50.0;             
        }
        else if (daysLeft <= 7) {
            score += 20.0;             
        }

        if (health && health->tiredness >= 7 && task.importance >= 7)
            score *= 0.7;
        if (health && health->mood <= 4 && task.pleasure >= 7)
            score *= 1.2;

        if (task.duration * 60 <= 2)
            score += 15.0;

        if (task.importance >= 8)
            score += 15.0;

        if (user.chronotype == "morning" && task.importance >= 7)
            score *= 1.1;
        if (user.chronotype == "evening" && task.pleasure >= 7)
            score *= 1.1;

        auto it = categoryBonus_.find(task.category);
        if (it != categoryBonus_.end()) {
            double bonus = it->second;
            if (bonus > 0.3) bonus = 0.3;
            score *= (1.0 + bonus);
        }

        return score;
    }

    bool canScheduleOnDay(const FlexibleTask& task, int dayId, int currentDay) {
        if (task.deadline.empty()) return true;
        int daysLeft = daysUntilDeadline(task.deadline);
        return (dayId - currentDay) <= daysLeft;
    }
};