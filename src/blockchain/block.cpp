// src/blockchain/block.cpp
#include "block.h"
#include <sstream>
#include <iostream>

Block::Block() 
    : height(0), timestamp(time(nullptr)), nonce(0), difficulty(2) {
}

std::string Block::calculateHash() const {
    std::stringstream ss;
    ss << height << prevHash << merkleRoot 
       << timestamp << nonce << difficulty << minedBy;
    return Crypto::sha256(ss.str());
}

std::string Block::calculateMerkleRoot() const {
    if (transactions.empty()) {
        return Crypto::sha256("empty");
    }
    
    std::vector<std::string> hashes;
    for (const auto& tx : transactions) {
        hashes.push_back(tx.txHash);
    }
    
    while (hashes.size() > 1) {
        if (hashes.size() % 2 != 0) {
            hashes.push_back(hashes.back());
        }
        
        std::vector<std::string> newHashes;
        for (size_t i = 0; i < hashes.size(); i += 2) {
            newHashes.push_back(Crypto::sha256(hashes[i] + hashes[i+1]));
        }
        hashes = newHashes;
    }
    
    return hashes[0];
}

bool Block::mine(int maxNonce) {
    std::string target(difficulty, '0');
    
    for (nonce = 0; nonce < maxNonce; nonce++) {
        hash = calculateHash();
        if (hash.substr(0, difficulty) == target) {
            return true;
        }
    }
    return false;
}

bool Block::validate() const {
    if (hash != calculateHash()) return false;
    
    std::string target(difficulty, '0');
    if (hash.substr(0, difficulty) != target) return false;
    
    if (merkleRoot != calculateMerkleRoot()) return false;
    
    return true;
}

nlohmann::json Block::toJson() const {
    nlohmann::json j;
    j["height"] = height;
    j["hash"] = hash;
    j["prevHash"] = prevHash;
    j["merkleRoot"] = merkleRoot;
    j["timestamp"] = timestamp;
    j["nonce"] = nonce;
    j["difficulty"] = difficulty;
    j["minedBy"] = minedBy;
    
    nlohmann::json txs = nlohmann::json::array();
    for (const auto& tx : transactions) {
        txs.push_back(tx.toJson());
    }
    j["transactions"] = txs;
    
    return j;
}

void Block::fromJson(const nlohmann::json& j) {
    height = j.value("height", 0);
    hash = j.value("hash", "");
    prevHash = j.value("prevHash", "");
    merkleRoot = j.value("merkleRoot", "");
    timestamp = j.value("timestamp", 0L);
    nonce = j.value("nonce", 0);
    difficulty = j.value("difficulty", 2.0);
    minedBy = j.value("minedBy", "");
    
    transactions.clear();
    if (j.contains("transactions") && j["transactions"].is_array()) {
        for (const auto& txJson : j["transactions"]) {
            Transaction tx;
            tx.fromAddress = txJson.value("from", "");
            tx.toAddress = txJson.value("to", "");
            tx.amount = txJson.value("amount", 0.0);
            tx.fee = txJson.value("fee", 0.0);
            tx.signature = txJson.value("signature", "");
            tx.timestamp = txJson.value("timestamp", 0L);
            tx.data = txJson.value("data", "");
            tx.status = txJson.value("status", "pending");
            tx.nonce = txJson.value("nonce", 0ULL);
            tx.txHash = tx.calculateHash();
            transactions.push_back(tx);
        }
    }
}