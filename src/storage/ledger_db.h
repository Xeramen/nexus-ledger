#ifndef NEXUS_LEDGER_DB_H
#define NEXUS_LEDGER_DB_H

#include <sqlite3.h>
#include <string>
#include <memory>
#include <vector>
#include <optional>

namespace nexus {

    struct Wallet {
        std::string address;
        std::string public_key;
        std::string private_key_encrypted;
        int64_t created_at;
        int64_t last_activity;
        int nonce;
    };

    struct Block {
        int height;
        std::string hash;
        std::string prev_hash;
        std::string merkle_root;
        int64_t timestamp;
        int nonce;
        double difficulty;
        std::string mined_by;
        int tx_count;
        int block_size;
        int version;
    };

    struct Transaction {
        std::string tx_hash;
        std::optional<int> block_height;
        std::optional<int> tx_index;
        std::string from_address;
        std::string to_address;
        double amount;
        double fee;
        std::string signature;
        int64_t timestamp;
        std::string data;
        std::string status; // pending, confirmed, invalid
    };

    class LedgerDB {
        public:
        LedgerDB(const std::string& db_path);
        ~LedgerDB();

        // Инициализация и миграции
        bool initialize();
        bool runMigrations();

        // Wallets
        bool createWallet(const Wallet& wallet);
        std::optional<Wallet> getWallet(const std::string& address);
        bool updateWalletActivity(const std::string& address, int64_t timestamp);
        bool incrementNonce(const std::string& address);

        // Blocks
        bool addBlock(const Block& block);
        std::optional<Block> getBlock(int height);
        std::optional<Block> getBlockByHash(const std::string& hash);
        std::optional<Block> getLatestBlock();
        int getBlockCount();

        // Transactions
        bool addTransaction(const Transaction& tx);
        std::optional<Transaction> getTransaction(const std::string& tx_hash);
        std::vector<Transaction> getTransactionsByAddress(const std::string& address, int limit = 100);
        bool confirmTransaction(const std::string& tx_hash, int block_height, int tx_index);
        std::vector<Transaction> getPendingTransactions(int limit = 1000);

        // Balance
        double getBalance(const std::string& address);
        std::pair<double, double> getTotalReceivedSent(const std::string& address);

        // Genesis
        bool isGenesisBlockExists();
        // bool createGenesisBlock();

    private:
        sqlite3* db_;
        std::string db_path_;
        
        bool executeScript(const std::string& script);
        bool tableExists(const std::string& table_name);

    };
} // namespace nexus

#endif // NEXUS_LEDGER_DB_H