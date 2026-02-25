#pragma once
#include <string>
#include <ctime>
#include "../crypto/crypto.h"

struct Transaction {
    std::string txHash;
    std::string fromAddress;
    std::string toAddress;
    double amount;
    double fee;
    std::string signature;
    long timestamp;
    std::string data;
    std::string status;
    
    Transaction();
    std::string calculateHash() const;
    std::string toJson() const;
    static Transaction createCoinbase(const std::string& to, double reward);
};