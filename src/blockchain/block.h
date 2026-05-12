#pragma once
#include <string>
#include <vector>
#include <ctime>
#include "transaction.h"
#include "../crypto/crypto.h"
#include <nlohmann/json.hpp>

struct Block {
    int height;
    std::string hash;
    std::string prevHash;
    std::string merkleRoot;
    long timestamp;
    int nonce;
    double difficulty;
    std::string minedBy;
    std::vector<Transaction> transactions;
    
    Block();
    std::string calculateHash() const;
    std::string calculateMerkleRoot() const;
    bool mine(int maxNonce = 1000000);
    bool validate() const;

    // Добавь в конец public секции класса Block (после bool validate() const;):

    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);
};