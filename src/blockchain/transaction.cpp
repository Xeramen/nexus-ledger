// src/blockchain/transaction.cpp
#include "transaction.h"
#include <sstream>
#include <nlohmann/json.hpp>

Transaction::Transaction() 
    : amount(0), fee(0), timestamp(time(nullptr)), status("pending") {
}

std::string Transaction::calculateHash() const {
    std::stringstream ss;
    ss << fromAddress << toAddress 
       << amount << fee << timestamp;
    return Crypto::sha256(ss.str());
}

Transaction Transaction::createCoinbase(const std::string& to, double reward) {
    Transaction tx;
    tx.fromAddress = "SYSTEM";
    tx.toAddress = to;
    tx.amount = reward;
    tx.fee = 0;
    tx.timestamp = time(nullptr);
    tx.data = "Coinbase";
    tx.status = "confirmed";
    tx.signature = "coinbase";
    tx.txHash = tx.calculateHash();
    return tx;
}

std::string Transaction::toJson() const {
    nlohmann::json j;
    j["txHash"] = txHash;
    j["from"] = fromAddress;
    j["to"] = toAddress;
    j["amount"] = amount;
    j["fee"] = fee;
    j["signature"] = signature;
    j["timestamp"] = timestamp;
    j["data"] = data;
    j["status"] = status;
    j["nonce"] = static_cast<uint64_t>(nonce);
    return j.dump();
}