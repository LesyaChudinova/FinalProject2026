#pragma once
#include "../models/models.h"
#include <string>
#include <vector>
#include <algorithm>

struct Result {
    bool ok = true;
    std::vector<std::string> warnings;
};

class Checker {
public:
    static constexpr int ULTRADIAN_MINUTES = 90;
    static constexpr int MAX_WORK_HOURS = 8;
    static constexpr int MIN_ACTIVITY_MINUTES = 30;
    static constexpr double MIN_DAILY_LOAD = 2.0;
    static constexpr double MAX_DAILY_LOAD = 8.0;

    Result check(const std::vector<FlexibleTask>& placed, const FlexibleTask& task,
        const std::vector<FixedTask>& fixed, int dayId) {
        Result r;

        double fixedHours = 0;
        for (const auto& f : fixed) {
            fixedHours += 1.0;
        }

        double workHours = fixedHours;
        std::string lastCategory = "";
        int sameCategoryStreak = 0;

        for (const auto& t : placed) {
            if (t.category == "работа" || t.category == "учёба" || t.category == "work")
                workHours += t.duration;

            if (t.category == lastCategory) {
                sameCategoryStreak++;
            }
            else {
                lastCategory = t.category;
                sameCategoryStreak = 1;
            }
        }

        if ((task.category == "работа" || task.category == "учёба" || task.category == "work") &&
            workHours + task.duration > MAX_WORK_HOURS) {
            r.ok = false;
            r.warnings.push_back("Превышен лимит рабочих часов (" + std::to_string(MAX_WORK_HOURS) + "ч)");
        }

        if (task.duration * 60 > ULTRADIAN_MINUTES) {
            r.warnings.push_back("Задача \"" + task.name + "\" длится более 90 минут. Рекомендуется разбить на части.");
        }

        if (task.duration * 60 <= 2) {
        }

        if (task.category == lastCategory && sameCategoryStreak >= 3) {
            r.warnings.push_back("Много задач подряд из категории \"" + task.category + "\". Рекомендуется сменить тип деятельности.");
        }

        double activityHours = 0;
        for (const auto& t : placed) {
            if (t.category == "спорт" || t.category == "отдых" || t.category == "хобби")
                activityHours += t.duration;
        }
        if (activityHours < 0.5 && placed.size() >= 5) {
            r.warnings.push_back("Мало физической активности. Рекомендуется добавить прогулку или растяжку.");
        }

        double totalLoad = workHours + activityHours;
        if (totalLoad > MAX_DAILY_LOAD) {
            r.warnings.push_back("Высокая нагрузка на день (" + std::to_string(totalLoad) + "ч). Риск выгорания.");
        }
        if (totalLoad < MIN_DAILY_LOAD && placed.size() > 0) {
            r.warnings.push_back("Низкая нагрузка на день. Можно добавить задачи для повышения продуктивности.");
        }

        return r;
    }
};