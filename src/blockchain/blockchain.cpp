#include "blockchain.h"
#include <iostream>
#include <iomanip>

Blockchain::Blockchain(const std::string& dbPath) 
    : db(std::make_unique<LedgerDB>(dbPath)) {
}

bool Blockchain::addBlock(Block& block) {
    std::cout << "Adding block " << block.height << " with " << block.transactions.size() << " transactions" << std::endl;
    
    if (block.height > 0) {
        auto prev = db->getBlockByHeight(block.height - 1);
        if (!prev.has_value()) {
            std::cerr << "Previous block not found for height " << block.height - 1 << std::endl;
            return false;
        }
        if (prev->hash != block.prevHash) {
            std::cerr << "Invalid previous hash" << std::endl;
            return false;
        }
    }
    
    if (!block.validate()) {
        std::cerr << "Block validation failed" << std::endl;
        return false;
    }
    
    if (!db->beginTransaction()) {
        std::cerr << "Failed to begin transaction" << std::endl;
        return false;
    }
    
    if (!db->addBlock(block)) {
        std::cerr << "Failed to add block to database" << std::endl;
        db->rollbackTransaction();
        return false;
    }
    
    for (const auto& tx : block.transactions) {
        if (!db->updateTransactionStatus(tx.txHash, "confirmed")) {
            std::cerr << "Failed to update transaction status for " << tx.txHash << std::endl;
            db->rollbackTransaction();
            return false;
        }
    }
    
    if (!db->commitTransaction()) {
        std::cerr << "Failed to commit transaction" << std::endl;
        db->rollbackTransaction();
        return false;
    }
    
    for (const auto& tx : block.transactions) {
        mempool.erase(tx.txHash);
    }
    
    std::cout << "Block " << block.height << " added to chain with " 
              << block.transactions.size() << " transactions" << std::endl;
    
    return true;
}

bool Blockchain::addTransaction(const Transaction& tx) {
    if (tx.amount <= 0) {
        std::cerr << "Invalid amount: " << tx.amount << std::endl;
        return false;
    }
    if (tx.fee < 0) {
        std::cerr << "Invalid fee: " << tx.fee << std::endl;
        return false;
    }
    
    if (tx.fromAddress != "SYSTEM") {
        db->ensureWalletExists(tx.fromAddress);
    }
    db->ensureWalletExists(tx.toAddress);
    
    if (tx.fromAddress != "SYSTEM") {
        double balance = getBalance(tx.fromAddress);
        if (balance < tx.amount + tx.fee) {
            std::cerr << "Insufficient balance for " << tx.fromAddress 
                      << ": has " << std::fixed << std::setprecision(2) << balance 
                      << ", needs " << tx.amount + tx.fee << std::endl;
            return false;
        }
    }
    
    mempool[tx.txHash] = tx;
    db->addTransaction(tx);
    db->addToMempool(tx);
    
    std::cout << "Transaction " << tx.txHash.substr(0, 8) << "... added to mempool" << std::endl;
    return true;
}

Block Blockchain::createBlock(const std::string& miner) {
    Block block;
    
    int lastHeight = db->getLatestHeight();
    
    if (lastHeight >= 0) {
        auto lastBlock = db->getBlockByHeight(lastHeight);
        if (lastBlock.has_value()) {
            block.height = lastHeight + 1;
            block.prevHash = lastBlock->hash;
        } else {
            block.height = 0;
            block.prevHash = "0";
        }
    } else {
        block.height = 0;
        block.prevHash = "0";
    }
    
    block.minedBy = miner;
    block.timestamp = time(nullptr);
    block.difficulty = 2;
    
    Transaction coinbase = Transaction::createCoinbase(miner, REWARD);
    block.transactions.push_back(coinbase);
    
    int txCount = 0;
    for (const auto& [hash, tx] : mempool) {
        if (tx.fromAddress != "SYSTEM") {
            block.transactions.push_back(tx);
            txCount++;
            if (txCount >= 10) break;
        }
    }
    
    block.merkleRoot = block.calculateMerkleRoot();
    
    std::cout << "Block created with " << block.transactions.size() 
              << " transactions (1 coinbase + " << txCount << " from mempool)" << std::endl;
    
    return block;
}

std::optional<Block> Blockchain::getBlock(int height) {
    return db->getBlockByHeight(height);
}

int Blockchain::getHeight() const {
    return db->getLatestHeight();
}

double Blockchain::getBalance(const std::string& address) {
    return db->getBalance(address);
}

std::vector<Transaction> Blockchain::getMempoolTransactions() {
    std::vector<Transaction> txs;
    for (const auto& [hash, tx] : mempool) {
        txs.push_back(tx);
    }
    return txs;
}