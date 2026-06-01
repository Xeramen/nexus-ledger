// src/storage/ledger_db.h
#pragma once
#include <sqlite3.h>
#include <string>
#include <vector>
#include <optional>
#include "../blockchain/block.h"
#include "../blockchain/transaction.h"

class LedgerDB {
private:
    sqlite3* db;
        
public:
    LedgerDB(const std::string& path);
    ~LedgerDB();
    
    bool ensureWalletExists(const std::string& address);
    
    bool addBlock(const Block& block);
    std::optional<Block> getBlockByHeight(int height);
    std::optional<Block> getBlockByHash(const std::string& hash);
    int getLatestHeight();

    bool execute(const std::string& sql);
    
    bool addTransaction(const Transaction& tx, int blockHeight = -1);
    bool updateTransactionStatus(const std::string& txHash, const std::string& status);
    std::vector<Transaction> getTransactionsByBlock(int height);
    std::optional<Transaction> getTransactionByHash(const std::string& hash);
    
    double getBalance(const std::string& address);
    
    bool addToMempool(const Transaction& tx);
    std::vector<Transaction> getMempool();
    void clearMempool();
    
    bool beginTransaction();
    bool commitTransaction();
    bool rollbackTransaction();

    uint64_t getNextNonce(const std::string& address);
    bool updateNonce(const std::string& address, uint64_t nonce);

    bool addPeer(const std::string& ip, int port, const std::string& node_id = "");
    bool removePeer(const std::string& ip, int port);
    std::vector<std::pair<std::string, int>> getPeers(int max_count = 50);
    void updatePeerSeen(const std::string& ip, int port);
};