#include "ledger_db.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>

namespace nexus {

    LedgerDB::LedgerDB(const std::string& db_path) : db_(nullptr), db_path_(db_path) {}

    LedgerDB::~LedgerDB() {
        if(db_) {
            sqlite3_close(db_);
        }
    }
}


