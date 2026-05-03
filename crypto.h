#pragma once
#include <string>
#include <sstream>
#include <iomanip>
#include <random>
#include <openssl/sha.h>

class Crypto {
public:
    static std::string generateSalt() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        unsigned char bytes[32];
        for (int i = 0; i < 32; i++) bytes[i] = dis(gen);

        std::stringstream ss;
        for (int i = 0; i < 32; i++)
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)bytes[i];
        return ss.str();
    }

    static std::string hashPassword(const std::string& password, const std::string& salt) {
        std::string input = salt + password;
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256((unsigned char*)input.c_str(), input.size(), hash);

        std::stringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        return ss.str();
    }
};