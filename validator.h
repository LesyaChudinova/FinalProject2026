#pragma once
#include <string>
#include <regex>
#include <vector>

class Validator {
public:
    struct ValidationResult {
        bool valid = true;
        std::vector<std::string> errors;
    };

    static ValidationResult validateEmail(const std::string& email) {
        ValidationResult r;
        if (email.empty()) { r.valid = false; r.errors.push_back("Email required"); return r; }
        std::regex pattern(R"(^[a-zA-Z0-9._%+\-]+@[a-zA-Z0-9.\-]+\.[a-zA-Z]{2,}$)");
        if (!std::regex_match(email, pattern)) { r.valid = false; r.errors.push_back("Invalid email format"); }
        return r;
    }

    static ValidationResult validatePassword(const std::string& password) {
        ValidationResult r;
        if (password.length() < 8) { r.valid = false; r.errors.push_back("Min 8 characters"); }
        if (password.length() > 64) { r.valid = false; r.errors.push_back("Max 64 characters"); }
        bool up = false, low = false, dig = false;
        for (char c : password) {
            if (c >= 'A' && c <= 'Z') up = true;
            if (c >= 'a' && c <= 'z') low = true;
            if (c >= '0' && c <= '9') dig = true;
        }
        if (!up) { r.valid = false; r.errors.push_back("Need uppercase letter"); }
        if (!low) { r.valid = false; r.errors.push_back("Need lowercase letter"); }
        if (!dig) { r.valid = false; r.errors.push_back("Need digit"); }
        return r;
    }

    static ValidationResult validateNickname(const std::string& nick) {
        ValidationResult r;
        if (nick.empty()) { r.valid = false; r.errors.push_back("Nickname required"); return r; }
        if (nick.length() < 2) { r.valid = false; r.errors.push_back("Min 2 characters"); }
        if (nick.length() > 50) { r.valid = false; r.errors.push_back("Max 50 characters"); }
        return r;
    }
};