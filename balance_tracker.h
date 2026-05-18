#pragma once
#include "../models/models.h"
#include <map>
#include <string>
#include <vector>

class BalanceTracker {
    std::map<std::string, double> hours_;
    std::map<std::string, int> targets_;
    double total_ = 0;

public:
    void setTargets(const std::vector<BalanceTarget>& t) {
        for (const auto& b : t) targets_[b.category] = b.targetPercent;
    }

    void add(const std::string& cat, double h) {
        hours_[cat] += h;
        total_ += h;
    }

    bool canAdd(const std::string& cat, double h) const {
        if (total_ == 0) return true;
        auto it = targets_.find(cat);
        if (it == targets_.end()) return true;
        double cur = hours_.count(cat) ? hours_.at(cat) : 0;
        double after = ((cur + h) / (total_ + h)) * 100;
        return after <= it->second + 15;
    }

    std::map<std::string, double> summary() const {
        std::map<std::string, double> s;
        if (total_ > 0)
            for (const auto& [cat, h] : hours_)
                s[cat] = (h / total_) * 100;
        return s;
    }
};