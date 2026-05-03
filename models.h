#pragma once
#include <string>
#include <vector>
#include <map>

struct User {
    int id = 0; std::string email, password, nickname;
    int age = 0; std::string goals, concentrationTime, lifestyle, personalityType, chronotype;
};

struct BalanceTarget {
    int userId; std::string category; int targetPercent;
};

struct Task {
    int id = 0, userId = 0; std::string name, week, notes;
    int activityLevel = 0; std::string taskType, taskView;
};

struct FixedTask {
    int taskId; std::string date, beginTime, endTime;
};

struct FlexibleTask {
    int taskId; double duration; std::string category;
    int pleasure, importance; std::string deadline;
    std::vector<int> preferredDays;
    std::string name;
};

struct HealthState {
    int id, userId; std::string week;
    int tiredness, general, mood, apathy;
    std::string emotion;
};

struct Idea {
    int id; std::string name, description, type, color;
    double length; int activityLevel, energyRestored, moodBoost;
};

struct WeeklyPlan {
    struct ScheduledItem { FlexibleTask task; int dayId; std::string time; };
    std::string weekStart; int userId;
    std::vector<ScheduledItem> scheduled;
    std::vector<FlexibleTask> unscheduled;
    std::vector<std::string> warnings;
    std::vector<Idea> suggestions;
    std::map<std::string, double> balanceSummary;
};