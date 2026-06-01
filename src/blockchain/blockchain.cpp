// src/blockchain/blockchain.cpp
#include "blockchain.h"
#include <iostream>
#include <iomanip>

Blockchain::Blockchain(const std::string& dbPath)
    : db(std::make_unique<LedgerDB>(dbPath)) {
}

bool Blockchain::addBlock(Block& block) {
    int currentHeight = getHeight();
    
    std::cout << "addBlock: received block #" << block.height 
              << ", current height=" << currentHeight << std::endl;
    
    if (block.height <= currentHeight) {
        std::cout << "Block #" << block.height << " already exists" << std::endl;
        return false;
    }
    
    if (block.height > 0) {
        auto prev = db->getBlockByHeight(currentHeight);
        if (!prev.has_value()) {
            std::cerr << "Previous block not found at height " << currentHeight << std::endl;
            return false;
        }
        if (prev->hash != block.prevHash) {
            std::cerr << "Invalid prevHash" << std::endl;
            return false;
        }
    }
    
    if (!db->addBlock(block)) {
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

    // Очищаем mempool от ставших невалидными транзакций
    cleanMempool();
    
    std::cout << "Added block #" << block.height 
              << ", removed " << removed << " txs from mempool, mempool size: " << mempool.size() << std::endl;
    
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
        
        // Проверка nonce (для генератора нагрузки отключена)
        // uint64_t expected_nonce = db->getNextNonce(tx.fromAddress);
        // if (tx.nonce != expected_nonce) {
        //     std::cerr << "Invalid nonce: expected " << expected_nonce
        //               << ", got " << tx.nonce << std::endl;
        //     return false;
        // }

        // Увеличиваем nonce в БД
        if (!db->updateNonce(tx.fromAddress, tx.nonce + 1)) {
            std::cerr << "Failed to update nonce for " << tx.fromAddress << std::endl;
            return false;
        }
    }

    double fee_per_byte = tx.fee / (tx.toJson().size()); // Комиссия за байт

    // Добавляем в mempool
    mempool[tx.txHash] = tx;
    mempool_by_priority.insert({fee_per_byte, tx.txHash, time(nullptr)});

    // Ограничение размера mempool
    while (mempool_by_priority.size() > 10000) {
        auto lowest = --mempool_by_priority.end();
        mempool.erase(lowest->tx_hash);
        mempool_by_priority.erase(lowest);
    }

    // Сохраняем в БД (как неподтверждённую)
    db->addTransaction(tx, -1);
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
    std::vector<TxPriority> to_remove;
    for (const auto& priority : mempool_by_priority) {
        if (txCount >= 10) break;
        auto it = mempool.find(priority.tx_hash);
        if (it != mempool.end()) {
            // Временно отключаем проверку баланса {здесь комментарий}
            bool valid = true;
            if (it->second.fromAddress != "SYSTEM") {
                double balance = getBalance(it->second.fromAddress);
                if (balance < it->second.amount + it->second.fee) {
                    valid = false;
                }
            }
            if (valid) {
                block.transactions.push_back(it->second);
                txCount++;
            } else {
                to_remove.push_back(priority);
            }
        }
    }

    // Если не удалось добавить ни одной транзакции, хотя mempool не пуст – очищаем его полностью
    if (txCount == 0 && !mempool.empty()) {
        std::cout << "No transactions could be included, clearing entire mempool (" << mempool.size() << " txs)" << std::endl;
        mempool.clear();
        mempool_by_priority.clear();
    }

    if (!to_remove.empty()) {
        std::cout << "createBlock: removed " << to_remove.size() << " invalid txs from mempool" << std::endl;
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
        return 4; // Начальная сложность (среднее)
    }

    // Получаем время создания последних блоков
    auto first_block = db->getBlockByHeight(height - difficulty_adjustment_interval + 1);
    auto last_block = db->getBlockByHeight(height);

    if (!first_block.has_value() || !last_block.has_value()) {
        return 2;
    }

    int64_t time_span = last_block->timestamp - first_block->timestamp;
    int expected_time = target_block_time_seconds * difficulty_adjustment_interval;

    int current_diff = last_block->difficulty;
    int new_diff = current_diff;

    if (time_span < expected_time * 0.75) {
        // Слишком быстро → увеличиваем сложность
        new_diff = current_diff * 2;
    } else if (time_span > expected_time * 1.25) {
        // Слишком медленно → уменьшаем сложность
        new_diff = std::max(1, current_diff / 2);
    }

    // Ограничиваем изменение (не более чем в 4 раза за раз)
    new_diff = std::max(1, std::min(new_diff, current_diff * 4));

    // Ограничение максимальной сложности
    const int MAX_DIFFICULTY = 6;   // можно изменить на 6, если нужно
    if (new_diff > MAX_DIFFICULTY) {
        new_diff = MAX_DIFFICULTY;
    }

    std::cout << "Difficulty adjustment: " << current_diff << " -> " << new_diff
              << " (time_span=" << time_span << "s, expected=" << expected_time << "s)" << std::endl;

    return new_diff;
}

void Blockchain::adjustDifficulty() {
    // Масштабирование сложности. Для дальнейшего обновления 
    std::cout << "Difficulty will be adjusted for next blocks" << std::endl;
}

bool Blockchain::replaceLastBlock(const Block& new_block) {
    int current_height = getHeight();
    if (current_height != new_block.height) {
        std::cerr << "replaceLastBlock: height mismatch" << std::endl;
        return false;
    }
    auto old_block = getBlock(current_height);
    if (!old_block) return false;
    
    // Проверяем, что новый блок ссылается на тот же предок
    if (old_block->prevHash != new_block.prevHash) {
        std::cerr << "replaceLastBlock: prevHash mismatch" << std::endl;
        return false;
    }
    
    // Удаляем старый блок из БД
    std::string sql = "DELETE FROM blocks WHERE height = " + std::to_string(current_height);
    if (!db->execute(sql)) return false;
    // Удаляем его транзакции из таблицы transactions (они будут пересозданы при добавлении нового блока)
    sql = "DELETE FROM transactions WHERE block_height = " + std::to_string(current_height);
    db->execute(sql);
    
    // Добавляем новый блок
    if (!db->addBlock(new_block)) return false;
    
    // Возвращаем транзакции старого блока обратно в mempool
    for (const auto& tx : old_block->transactions) {
        if (tx.fromAddress != "SYSTEM" && tx.amount > 0 && tx.fee >= 0) { // не coinbase
            addTransaction(tx); // это добавит в mempool
        }
    }

    cleanMempool();
    
    std::cout << "Replaced block #" << current_height << " with new block " << new_block.hash.substr(0,8) << std::endl;
    return true;
}

bool Blockchain::rollbackToHeight(int target_height) {
    int current = getHeight();
    if (target_height >= current) return false;
    
    // Удаляем блоки из БД
    for (int h = current; h > target_height; --h) {
        std::string sql = "DELETE FROM blocks WHERE height = " + std::to_string(h);
        db->execute(sql);
    }
    return true;
}

int Blockchain::cleanMempool() {
    int removed = 0;
    std::vector<TxPriority> to_remove;
    
    for (const auto& priority : mempool_by_priority) {
        auto it = mempool.find(priority.tx_hash);
        if (it == mempool.end()) continue;
        
        const Transaction& tx = it->second;
        if (tx.fromAddress != "SYSTEM") {
            double balance = getBalance(tx.fromAddress);
            if (balance < tx.amount + tx.fee) {
                to_remove.push_back(priority);
                removed++;
                std::cout << "Removing invalid tx " << tx.txHash.substr(0,8) 
                          << " from mempool: balance " << balance 
                          << " < " << (tx.amount + tx.fee) << std::endl;
            }
        }
    }
    
    for (const auto& p : to_remove) {
        mempool.erase(p.tx_hash);
        mempool_by_priority.erase(p);
    }
    
    if (removed > 0) {
        std::cout << "Cleaned " << removed << " invalid transactions from mempool" << std::endl;
    }
    return removed;
}