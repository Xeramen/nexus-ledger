#include <iostream>
#include "blockchain/blockchain.h"

void testBlockchain() {
    std::cout << "=== Nexus Ledger Test ===\n" << std::endl;
    
    try {
        Blockchain chain("nexus.db");
        
        std::cout << "Current height: " << chain.getHeight() << std::endl;
        
        auto genesis = chain.getBlock(0);
        if (genesis) {
            std::cout << "Genesis block found!" << std::endl;
            std::cout << "  Hash: " << genesis->hash << std::endl;
            std::cout << "  Miner: " << genesis->minedBy << std::endl;
        } else {
            std::cerr << "ERROR: Genesis block not found!" << std::endl;
            return;
        }
        
        double balance = chain.getBalance("genesis_miner");
        std::cout << "Genesis miner balance: " << balance << std::endl;
        
        Transaction tx;
        tx.fromAddress = "genesis_miner";
        tx.toAddress = "alice";
        tx.amount = 10.0;
        tx.fee = 0.1;
        tx.signature = "test_sig"; // Подпись (может быть)
        tx.timestamp = time(nullptr);
        tx.txHash = tx.calculateHash();
        
        if (chain.addTransaction(tx)) {
            std::cout << "Transaction added to mempool" << std::endl;
        } else {
            std::cerr << "Failed to add transaction" << std::endl;
        }
        
        std::cout << "\nMining new block..." << std::endl;
        auto newBlock = chain.createBlock("miner1");
        
        if (newBlock.mine(1000000)) {
            std::cout << "Block mined!" << std::endl;
            std::cout << "  Height: " << newBlock.height << std::endl;
            std::cout << "  Hash: " << newBlock.hash << std::endl;
            std::cout << "  Nonce: " << newBlock.nonce << std::endl;
            std::cout << "  Transactions: " << newBlock.transactions.size() << std::endl;
            
            if (chain.addBlock(newBlock)) {
                std::cout << "Block added to chain!" << std::endl;
            } else {
                std::cerr << "Failed to add block to chain" << std::endl;
            }
        } else {
            std::cout << "Failed to mine block" << std::endl;
        }
        
        std::cout << "\nFinal balances:" << std::endl;
        std::cout << "genesis_miner: " << chain.getBalance("genesis_miner") << std::endl;
        std::cout << "miner1: " << chain.getBalance("miner1") << std::endl;
        std::cout << "alice: " << chain.getBalance("alice") << std::endl;
        
        std::cout << "\nChain height: " << chain.getHeight() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}

int main() {
    testBlockchain();
    return 0;
}