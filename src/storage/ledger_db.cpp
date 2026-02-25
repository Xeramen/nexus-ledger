#include "ledger_db.h"
#include <iostream>
#include <fstream>
#include <sstream>

LedgerDB::LedgerDB(const std::string& path) {
    int rc = sqlite3_open(path.c_str(), &db);
    if (rc) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        throw std::runtime_error("Can't open database");
    }
    
    execute("PRAGMA foreign_keys = ON;");
    
    std::ifstream file("schema.sql");
    if (file.is_open()) {
        std::stringstream ss;
        ss << file.rdbuf();
        execute(ss.str());
    } else {
        std::ifstream file2("../schema.sql");
        if (file2.is_open()) {
            std::stringstream ss;
            ss << file2.rdbuf();
            execute(ss.str());
        }
    }
}

LedgerDB::~LedgerDB() {
    if (db) sqlite3_close(db);
}

bool LedgerDB::execute(const std::string& sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool LedgerDB::addBlock(const Block& block) {
    const char* sql = "INSERT INTO blocks (height, hash, prev_hash, merkle_root, timestamp, nonce, difficulty, mined_by, tx_count) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    
    sqlite3_bind_int(stmt, 1, block.height);
    sqlite3_bind_text(stmt, 2, block.hash.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, block.prevHash.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, block.merkleRoot.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 5, block.timestamp);
    sqlite3_bind_int(stmt, 6, block.nonce);
    sqlite3_bind_double(stmt, 7, block.difficulty);
    sqlite3_bind_text(stmt, 8, block.minedBy.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 9, block.transactions.size());
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc == SQLITE_DONE) {
        for (const auto& tx : block.transactions) {
            addTransaction(tx, block.height);
        }
        return true;
    }
    return false;
}

std::optional<Block> LedgerDB::getBlockByHeight(int height) {
    const char* sql = "SELECT * FROM blocks WHERE height = ?;";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    
    sqlite3_bind_int(stmt, 1, height);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Block block;
        block.height = sqlite3_column_int(stmt, 1);
        
        const char* hash_str = (const char*)sqlite3_column_text(stmt, 2);
        if (hash_str) block.hash = hash_str;
        
        const char* prev_str = (const char*)sqlite3_column_text(stmt, 3);
        if (prev_str) block.prevHash = prev_str;
        
        const char* merkle_str = (const char*)sqlite3_column_text(stmt, 4);
        if (merkle_str) block.merkleRoot = merkle_str;
        
        block.timestamp = sqlite3_column_int64(stmt, 5);
        block.nonce = sqlite3_column_int(stmt, 6);
        block.difficulty = sqlite3_column_double(stmt, 7);
        
        const char* miner_str = (const char*)sqlite3_column_text(stmt, 8);
        if (miner_str) block.minedBy = miner_str;
        
        sqlite3_finalize(stmt);
        
        block.transactions = getTransactionsByBlock(height);
        return block;
    }
    
    sqlite3_finalize(stmt);
    return std::nullopt;
}

std::optional<Block> LedgerDB::getBlockByHash(const std::string& hash) {
    const char* sql = "SELECT height FROM blocks WHERE hash = ?;";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    
    sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_STATIC);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int height = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return getBlockByHeight(height);
    }
    
    sqlite3_finalize(stmt);
    return std::nullopt;
}

int LedgerDB::getLatestHeight() {
    const char* sql = "SELECT MAX(height) FROM blocks;";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }
    
    int height = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        height = sqlite3_column_int(stmt, 0);
    }
    
    sqlite3_finalize(stmt);
    return height;
}

bool LedgerDB::addTransaction(const Transaction& tx, int blockHeight) {
    const char* sql = "INSERT INTO transactions (tx_hash, block_height, from_address, to_address, amount, fee, signature, timestamp, data, status) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, tx.txHash.c_str(), -1, SQLITE_STATIC);
    if (blockHeight >= 0) sqlite3_bind_int(stmt, 2, blockHeight);
    else sqlite3_bind_null(stmt, 2);
    sqlite3_bind_text(stmt, 3, tx.fromAddress.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, tx.toAddress.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 5, tx.amount);
    sqlite3_bind_double(stmt, 6, tx.fee);
    sqlite3_bind_text(stmt, 7, tx.signature.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 8, tx.timestamp);
    sqlite3_bind_text(stmt, 9, tx.data.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 10, tx.status.c_str(), -1, SQLITE_STATIC);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<Transaction> LedgerDB::getTransactionsByBlock(int height) {
    std::vector<Transaction> txs;
    const char* sql = "SELECT * FROM transactions WHERE block_height = ?;";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return txs;
    }
    
    sqlite3_bind_int(stmt, 1, height);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Transaction tx;
        
        const char* hash_str = (const char*)sqlite3_column_text(stmt, 1);
        if (hash_str) tx.txHash = hash_str;
        
        const char* from_str = (const char*)sqlite3_column_text(stmt, 3);
        if (from_str) tx.fromAddress = from_str;
        
        const char* to_str = (const char*)sqlite3_column_text(stmt, 4);
        if (to_str) tx.toAddress = to_str;
        
        tx.amount = sqlite3_column_double(stmt, 5);
        tx.fee = sqlite3_column_double(stmt, 6);
        
        const char* sig_str = (const char*)sqlite3_column_text(stmt, 7);
        if (sig_str) tx.signature = sig_str;
        
        tx.timestamp = sqlite3_column_int64(stmt, 8);
        
        const char* data_str = (const char*)sqlite3_column_text(stmt, 9);
        if (data_str) tx.data = data_str;
        
        const char* status_str = (const char*)sqlite3_column_text(stmt, 10);
        if (status_str) tx.status = status_str;
        
        txs.push_back(tx);
    }
    
    sqlite3_finalize(stmt);
    return txs;
}

std::optional<Transaction> LedgerDB::getTransactionByHash(const std::string& hash) {
    const char* sql = "SELECT * FROM transactions WHERE tx_hash = ?;";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    
    sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_STATIC);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Transaction tx;
        
        const char* hash_str = (const char*)sqlite3_column_text(stmt, 1);
        if (hash_str) tx.txHash = hash_str;
        
        const char* from_str = (const char*)sqlite3_column_text(stmt, 3);
        if (from_str) tx.fromAddress = from_str;
        
        const char* to_str = (const char*)sqlite3_column_text(stmt, 4);
        if (to_str) tx.toAddress = to_str;
        
        tx.amount = sqlite3_column_double(stmt, 5);
        tx.fee = sqlite3_column_double(stmt, 6);
        
        const char* sig_str = (const char*)sqlite3_column_text(stmt, 7);
        if (sig_str) tx.signature = sig_str;
        
        tx.timestamp = sqlite3_column_int64(stmt, 8);
        
        const char* data_str = (const char*)sqlite3_column_text(stmt, 9);
        if (data_str) tx.data = data_str;
        
        const char* status_str = (const char*)sqlite3_column_text(stmt, 10);
        if (status_str) tx.status = status_str;
        
        sqlite3_finalize(stmt);
        return tx;
    }
    
    sqlite3_finalize(stmt);
    return std::nullopt;
}

double LedgerDB::getBalance(const std::string& address) {
    const char* sql = "SELECT balance FROM wallet_balance WHERE address = ?;";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }
    
    sqlite3_bind_text(stmt, 1, address.c_str(), -1, SQLITE_STATIC);
    
    double balance = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        balance = sqlite3_column_double(stmt, 0);
    }
    
    sqlite3_finalize(stmt);
    return balance;
}

bool LedgerDB::addToMempool(const Transaction& tx) {
    const char* sql = "INSERT INTO mempool (tx_hash, tx_data, received_at) VALUES (?, ?, ?);";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, tx.txHash.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, tx.toJson().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, time(nullptr));
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<Transaction> LedgerDB::getMempool() {
    std::vector<Transaction> txs;
    const char* sql = "SELECT tx_hash FROM mempool ORDER BY received_at;";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return txs;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* hash_str = (const char*)sqlite3_column_text(stmt, 0);
        if (hash_str) {
            std::string hash(hash_str);
            auto tx = getTransactionByHash(hash);
            if (tx) txs.push_back(*tx);
        }
    }
    
    sqlite3_finalize(stmt);
    return txs;
}

void LedgerDB::clearMempool() {
    execute("DELETE FROM mempool;");
}