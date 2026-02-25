#pragma once
#include <string>
#include <sstream>
#include <iomanip>
#include <openssl/evp.h>

class Crypto {
public:
    static std::string sha256(const std::string& input) {
        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hashLen;
        
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
        EVP_DigestUpdate(ctx, input.c_str(), input.size());
        EVP_DigestFinal_ex(ctx, hash, &hashLen);
        EVP_MD_CTX_free(ctx);
        
        std::stringstream ss;
        for(unsigned int i = 0; i < hashLen; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        }
        return ss.str();
    }
};