#include "transaction.h"
#include <sstream>

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
    std::stringstream ss;
    ss << "{\"hash\":\"" << txHash << "\","
       << "\"from\":\"" << fromAddress << "\","
       << "\"to\":\"" << toAddress << "\","
       << "\"amount\":" << amount << "}";
    return ss.str();
}