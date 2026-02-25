#include "block.h"
#include <sstream>
#include <iostream>

Block::Block() 
    : height(0), timestamp(time(nullptr)), nonce(0), difficulty(2) {
}

std::string Block::calculateHash() const {
    std::stringstream ss;
    ss << height << prevHash << merkleRoot 
       << timestamp << nonce << difficulty << minedBy;
    return Crypto::sha256(ss.str());
}

std::string Block::calculateMerkleRoot() const {
    if (transactions.empty()) {
        return Crypto::sha256("empty");
    }
    
    std::vector<std::string> hashes;
    for (const auto& tx : transactions) {
        hashes.push_back(tx.txHash);
    }
    
    while (hashes.size() > 1) {
        if (hashes.size() % 2 != 0) {
            hashes.push_back(hashes.back());
        }
        
        std::vector<std::string> newHashes;
        for (size_t i = 0; i < hashes.size(); i += 2) {
            newHashes.push_back(Crypto::sha256(hashes[i] + hashes[i+1]));
        }
        hashes = newHashes;
    }
    
    return hashes[0];
}

bool Block::mine(int maxNonce) {
    std::string target(difficulty, '0');
    
    for (nonce = 0; nonce < maxNonce; nonce++) {
        hash = calculateHash();
        if (hash.substr(0, difficulty) == target) {
            return true;
        }
    }
    return false;
}

bool Block::validate() const {
    if (hash != calculateHash()) return false;
    
    std::string target(difficulty, '0');
    if (hash.substr(0, difficulty) != target) return false;
    
    if (merkleRoot != calculateMerkleRoot()) return false;
    
    return true;
}