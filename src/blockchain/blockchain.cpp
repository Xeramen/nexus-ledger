#include "blockchain.h"
#include <iostream>
#include <iomanip>

Blockchain::Blockchain(const std::string& dbPath)
    : db(std::make_unique<LedgerDB>(dbPath)) {
}

bool Blockchain::addBlock(Block& block) {
    int currentHeight = getHeight();
    
    std::cout << "📦 addBlock: received block #" << block.height 
              << ", current height=" << currentHeight << std::endl;
    
    if (block.height <= currentHeight) {
        std::cout << "  ⏭️ Block #" << block.height << " already exists" << std::endl;
        return false;
    }
    
    if (block.height > 0) {
        auto prev = db->getBlockByHeight(currentHeight);
        if (!prev.has_value()) {
            std::cerr << "  ❌ Previous block not found at height " << currentHeight << std::endl;
            return false;
        }
        if (prev->hash != block.prevHash) {
            std::cerr << "  ❌ Invalid prevHash" << std::endl;
            return false;
        }
    }
    
    if (!db->addBlock(block)) {
        return false;
    }

    // Проверяем сложность блока
    int expected_diff = getCurrentDifficulty();
    if (block.difficulty != expected_diff && block.height > difficulty_adjustment_interval) {
        std::cerr << "  ❌ Invalid difficulty: expected " << expected_diff 
                  << ", got " << block.difficulty << std::endl;
        return false;
    }
    
    // Очищаем mempool от транзакций блока
    int removed = 0;
    for (const auto& tx : block.transactions) {
        if (tx.fromAddress != "SYSTEM") {
            if (mempool.erase(tx.txHash)) {
                removed++;
            }
        }
    }
    
    std::cout << "  ✅ Added block #" << block.height 
              << ", removed " << removed << " txs from mempool, mempool size: " << mempool.size() << std::endl;

    // После добавления блока - корректируем сложность для следующего
    if (block.height % difficulty_adjustment_interval == 0) {
        adjustDifficulty();
    }
    
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

    double fee_per_byte = tx.fee / (tx.toJson().size());  // Комиссия за байт
    
    mempool[tx.txHash] = tx;
    mempool_by_priority.insert({fee_per_byte, tx.txHash, time(nullptr)});
    
    // Ограничение размера mempool (как в Bitcoin)
    while (mempool_by_priority.size() > 10000) {
        auto lowest = --mempool_by_priority.end();
        mempool.erase(lowest->tx_hash);
        mempool_by_priority.erase(lowest);
    }

    if (tx.fromAddress != "SYSTEM") {
    uint64_t expected_nonce = db->getNextNonce(tx.fromAddress);
    if (tx.nonce != expected_nonce) {
        std::cerr << "Invalid nonce: expected " << expected_nonce 
                  << ", got " << tx.nonce << std::endl;
        return false;
    }
}
    
    return true;
    
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
    block.difficulty = getCurrentDifficulty();  // Используем текущую сложность
    
    // Coinbase транзакция
    Transaction coinbase = Transaction::createCoinbase(miner, REWARD);
    block.transactions.push_back(coinbase);
    
    
    int txCount = 0;
    for (const auto& priority : mempool_by_priority) {
        if (txCount >= 10) break;
        auto it = mempool.find(priority.tx_hash);
        if (it != mempool.end()) {
            block.transactions.push_back(it->second);
            txCount++;
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

int Blockchain::getCurrentDifficulty() const {
    int height = getHeight();
    
    // Генезис-блок и первые блоки имеют стартовую сложность
    if (height < difficulty_adjustment_interval) {
        return 2;  // Начальная сложность (легко)
    }
    
    // Получаем время создания последних блоков
    auto first_block = db->getBlockByHeight(height - difficulty_adjustment_interval + 1);
    auto last_block = db->getBlockByHeight(height);
    
    if (!first_block.has_value() || !last_block.has_value()) {
        return 2;
    }
    
    int64_t time_span = last_block->timestamp - first_block->timestamp;
    int expected_time = target_block_time_seconds * difficulty_adjustment_interval;
    
    // Корректируем сложность (не более чем в 4 раза за раз)
    int current_diff = last_block->difficulty;
    int new_diff = current_diff;
    
    if (time_span < expected_time * 0.75) {
        // Слишком быстро → увеличиваем сложность
        new_diff = current_diff * 2;
    } else if (time_span > expected_time * 1.25) {
        // Слишком медленно → уменьшаем сложность
        new_diff = std::max(1, current_diff / 2);
    }
    
    // Ограничиваем изменение
    new_diff = std::max(1, std::min(new_diff, current_diff * 4));
    
    std::cout << "📊 Difficulty adjustment: " << current_diff << " -> " << new_diff 
              << " (time_span=" << time_span << "s, expected=" << expected_time << "s)" << std::endl;
    
    return new_diff;
}

void Blockchain::adjustDifficulty() {
    int new_diff = getCurrentDifficulty();
    // Здесь можно сохранить новую сложность, но пока просто выводим
    std::cout << "📊 Difficulty will be adjusted for next blocks" << std::endl;
}