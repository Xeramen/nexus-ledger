#include "blockchain.h"
#include <iostream>
#include <iomanip>

Blockchain::Blockchain(const std::string& dbPath) 
    : db(std::make_unique<LedgerDB>(dbPath)) {
}

bool Blockchain::addBlock(Block& block) {
    // Если блок уже есть - игнорируем
    if (block.height <= getHeight()) {
        return false;
    }

    // Если это следующий блок - добавляем напрямую
    if (block.height == getHeight() + 1) {
        if (!db->addBlock(block)) return false;
        return true;
    }
    
    // Если это блок из будущего - сохраняем во временное хранилище
    // (для простоты пока просто игнорируем)
    std::cout << "⚠️ Block #" << block.height << " is too far ahead (current height=" << getHeight() << ")" << std::endl;
    return false;
    
    // Проверяем, что блок новый (выше текущей цепи)
    int currentHeight = db->getLatestHeight();
    
    if (block.height <= currentHeight) {
        std::cout << "⚠️ Block #" << block.height << " is not newer than current height " << currentHeight << std::endl;
        return false;
    }
    
    // Проверяем prevHash (если не генезис-блок)
    if (block.height > 0) {
        auto lastBlock = db->getBlockByHeight(currentHeight);
        if (!lastBlock.has_value()) {
            std::cerr << "❌ No last block found at height " << currentHeight << std::endl;
            return false;
        }
        if (block.prevHash != lastBlock->hash) {
            std::cerr << "⚠️ Invalid prevHash for block #" << block.height 
                      << ": expected " << lastBlock->hash.substr(0, 16) 
                      << ", got " << block.prevHash.substr(0, 16) << std::endl;
            return false;
        }
    }
    
    // Сохраняем в БД
    if (!db->addBlock(block)) {
        std::cerr << "❌ Failed to save block #" << block.height << " to database" << std::endl;
        return false;
    }
    
    std::cout << "✅ Block #" << block.height << " added successfully" << std::endl;
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