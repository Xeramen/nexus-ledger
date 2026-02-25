#include <iostream>
#include <iomanip>
#include "blockchain/blockchain.h"

void printBalance(Blockchain& chain, const std::string& name, const std::string& address) {
    std::cout << "  " << name << " (" << address << "): " 
              << std::fixed << std::setprecision(2) << chain.getBalance(address) << std::endl;
}

void testBlockchain() {
    std::cout << "=== Nexus Ledger Test ===\n" << std::endl;
    
    try {
        Blockchain chain("nexus.db");
        
        std::cout << "Current chain height: " << chain.getHeight() << std::endl;
        
        auto genesis = chain.getBlock(0);
        if (genesis) {
            std::cout << "Genesis block found!" << std::endl;
            std::cout << "  Hash: " << genesis->hash << std::endl;
            std::cout << "  Miner: " << genesis->minedBy << std::endl;
        } else {
            std::cerr << "ERROR: Genesis block not found!" << std::endl;
            return;
        }
        
        std::cout << "\nInitial balances:" << std::endl;
        printBalance(chain, "Genesis miner", "genesis_miner");
        printBalance(chain, "Miner1", "miner1");
        printBalance(chain, "Alice", "alice");
        printBalance(chain, "Bob", "bob");
        
        Transaction tx1;
        tx1.fromAddress = "genesis_miner";
        tx1.toAddress = "alice";
        tx1.amount = 10.0;
        tx1.fee = 0.1;
        tx1.signature = "test_sig_1";
        tx1.timestamp = time(nullptr);
        tx1.txHash = tx1.calculateHash();
        
        if (chain.addTransaction(tx1)) {
            std::cout << "\nTransaction 1 added: genesis_miner -> alice (10.0)" << std::endl;
        }
        
        Transaction tx2;
        tx2.fromAddress = "alice";
        tx2.toAddress = "bob";
        tx2.amount = 3.0;
        tx2.fee = 0.05;
        tx2.signature = "test_sig_2";
        tx2.timestamp = time(nullptr);
        tx2.txHash = tx2.calculateHash();
        
        if (!chain.addTransaction(tx2)) {
            std::cout << "Transaction 2 rejected (expected): alice -> bob (3.0) - insufficient balance" << std::endl;
        }
        
        std::cout << "\nMining new block..." << std::endl;
        auto newBlock = chain.createBlock("miner1");
        
        if (newBlock.mine(1000000)) {
            std::cout << "Block mined!" << std::endl;
            std::cout << "  Height: " << newBlock.height << std::endl;
            std::cout << "  Hash: " << newBlock.hash << std::endl;
            std::cout << "  PrevHash: " << newBlock.prevHash.substr(0, 16) << "..." << std::endl;
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
        
        std::cout << "\nTrying transaction 2 again after block is confirmed..." << std::endl;
        if (chain.addTransaction(tx2)) {
            std::cout << "Transaction 2 added: alice -> bob (3.0)" << std::endl;
            
            std::cout << "\nMining second block..." << std::endl;
            auto newBlock2 = chain.createBlock("miner1");
            
            if (newBlock2.mine(1000000)) {
                std::cout << "Second block mined!" << std::endl;
                if (chain.addBlock(newBlock2)) {
                    std::cout << "Second block added to chain!" << std::endl;
                }
            }
        }
        
        std::cout << "\nFinal balances:" << std::endl;
        printBalance(chain, "Genesis miner", "genesis_miner");
        printBalance(chain, "Miner1", "miner1");
        printBalance(chain, "Alice", "alice");
        printBalance(chain, "Bob", "bob");
        
        std::cout << "\nChain height: " << chain.getHeight() << std::endl;
        
        std::cout << "\nBlockchain:" << std::endl;
        for (int h = 0; h <= chain.getHeight(); h++) {
            auto block = chain.getBlock(h);
            if (block) {
                std::cout << "  Block " << h << ": " << block->hash.substr(0, 16) 
                          << "... (" << block->transactions.size() << " txs)" << std::endl;
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}

int main() {
    testBlockchain();
    return 0;
}