#pragma once
#include <memory>
#include <vector>
#include <map>
#include <set>
#include <optional>
#include "block.h"
#include "../storage/ledger_db.h"

struct TxPriority {
    double fee_per_byte;
    std::string tx_hash;
    time_t timestamp;
    
    bool operator<(const TxPriority& other) const {
        if (fee_per_byte != other.fee_per_byte) {
            return fee_per_byte > other.fee_per_byte;  // Выше комиссия - выше приоритет
        }
        return timestamp < other.timestamp;  // Старше - выше приоритет
    }
};

class Blockchain {
private:
    std::unique_ptr<LedgerDB> db;
    std::map<std::string, Transaction> mempool;
    std::set<TxPriority> mempool_by_priority;  // Сортированный по приоритету
    const double REWARD = 100.0;
    int target_block_time_seconds = 60;  // Целевое время между блоками (1 минута)
    int difficulty_adjustment_interval = 10;  // Пересчитывать сложность каждые 10 блоков

    int calculateNewDifficulty();
    
public:
    Blockchain(const std::string& dbPath);
    
    bool addBlock(Block& block);
    bool addTransaction(const Transaction& tx);
    std::optional<Block> getBlock(int height);
    int getHeight() const { return db->getLatestHeight(); }
    double getBalance(const std::string& address);
    
    int getCurrentDifficulty() const;
    void adjustDifficulty();
    Block createBlock(const std::string& miner);
    std::vector<Transaction> getMempoolTransactions();
    int getMempoolSize() const { return mempool.size(); }
    void removeFromMempool(const std::string& txHash) {
    mempool.erase(txHash); 
    }
};
