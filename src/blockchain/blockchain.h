#pragma once
#include <memory>
#include <vector>
#include <map>
#include <optional>
#include "block.h"
#include "../storage/ledger_db.h"

class Blockchain {
private:
    std::unique_ptr<LedgerDB> db;
    std::map<std::string, Transaction> mempool;
    const double REWARD = 100.0;
    
public:
    Blockchain(const std::string& dbPath);
    
    bool addBlock(Block& block);
    bool addTransaction(const Transaction& tx);
    std::optional<Block> getBlock(int height);
    int getHeight() const;
    double getBalance(const std::string& address);
    
    Block createBlock(const std::string& miner);
    std::vector<Transaction> getMempoolTransactions();
};