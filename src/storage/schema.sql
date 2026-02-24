-- ============================================
-- Nexus Ledger Database Schema
-- Запуск: sqlite3 ledger.db < schema.sql
-- ============================================

-- Отключаем FOREIGN KEY временно для инициализации
PRAGMA foreign_keys = OFF;

-- ============================================
-- 1. КОШЕЛЬКИ
-- ============================================
CREATE TABLE IF NOT EXISTS wallets (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    address TEXT UNIQUE NOT NULL,
    public_key TEXT NOT NULL,
    private_key_encrypted TEXT,
    created_at INTEGER NOT NULL,
    last_activity INTEGER,
    nonce INTEGER DEFAULT 0
);

CREATE INDEX IF NOT EXISTS idx_wallets_address ON wallets(address);
CREATE INDEX IF NOT EXISTS idx_wallets_created ON wallets(created_at);

-- ============================================
-- 2. БЛОКИ
-- ============================================
CREATE TABLE IF NOT EXISTS blocks (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    height INTEGER UNIQUE NOT NULL,
    hash TEXT UNIQUE NOT NULL,
    prev_hash TEXT NOT NULL,
    merkle_root TEXT NOT NULL,
    timestamp INTEGER NOT NULL,
    nonce INTEGER NOT NULL,
    difficulty REAL NOT NULL,
    mined_by TEXT NOT NULL,
    tx_count INTEGER DEFAULT 0,
    block_size INTEGER,
    version INTEGER DEFAULT 1
);

CREATE INDEX IF NOT EXISTS idx_blocks_height ON blocks(height);
CREATE INDEX IF NOT EXISTS idx_blocks_hash ON blocks(hash);
CREATE INDEX IF NOT EXISTS idx_blocks_prev_hash ON blocks(prev_hash);
CREATE INDEX IF NOT EXISTS idx_blocks_miner ON blocks(mined_by);
CREATE INDEX IF NOT EXISTS idx_blocks_timestamp ON blocks(timestamp);

-- ============================================
-- 3. ТРАНЗАКЦИИ
-- ============================================
CREATE TABLE IF NOT EXISTS transactions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    tx_hash TEXT UNIQUE NOT NULL,
    block_height INTEGER,
    tx_index INTEGER,
    from_address TEXT NOT NULL,
    to_address TEXT NOT NULL,
    amount REAL NOT NULL CHECK (amount > 0),
    fee REAL NOT NULL CHECK (fee >= 0),
    signature TEXT NOT NULL,
    timestamp INTEGER NOT NULL,
    data TEXT,
    status TEXT CHECK (status IN ('pending', 'confirmed', 'invalid')) DEFAULT 'pending',
    UNIQUE (block_height, tx_index)
);

CREATE INDEX IF NOT EXISTS idx_tx_hash ON transactions(tx_hash);
CREATE INDEX IF NOT EXISTS idx_tx_from ON transactions(from_address);
CREATE INDEX IF NOT EXISTS idx_tx_to ON transactions(to_address);
CREATE INDEX IF NOT EXISTS idx_tx_block ON transactions(block_height);
CREATE INDEX IF NOT EXISTS idx_tx_status ON transactions(status);
CREATE INDEX IF NOT EXISTS idx_tx_timestamp ON transactions(timestamp);

-- ============================================
-- 4. МЕМПУЛ
-- ============================================
CREATE TABLE IF NOT EXISTS mempool (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    tx_hash TEXT UNIQUE NOT NULL,
    tx_data TEXT NOT NULL,
    received_at INTEGER NOT NULL,
    fee_per_byte REAL,
    size INTEGER
);

CREATE INDEX IF NOT EXISTS idx_mempool_tx_hash ON mempool(tx_hash);
CREATE INDEX IF NOT EXISTS idx_mempool_received ON mempool(received_at);
CREATE INDEX IF NOT EXISTS idx_mempool_fee ON mempool(fee_per_byte DESC);

-- ============================================
-- 5. ПИРЫ
-- ============================================
CREATE TABLE IF NOT EXISTS peers (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ip_address TEXT NOT NULL,
    port INTEGER NOT NULL CHECK (port > 0 AND port < 65536),
    node_id TEXT UNIQUE,
    protocol_version TEXT DEFAULT '1.0',
    last_seen INTEGER,
    is_bootstrap BOOLEAN DEFAULT 0,
    failed_attempts INTEGER DEFAULT 0,
    UNIQUE (ip_address, port)
);

CREATE INDEX IF NOT EXISTS idx_peers_ip_port ON peers(ip_address, port);
CREATE INDEX IF NOT EXISTS idx_peers_last_seen ON peers(last_seen);
CREATE INDEX IF NOT EXISTS idx_peers_bootstrap ON peers(is_bootstrap);

-- ============================================
-- 6. КОНФИГУРАЦИЯ УЗЛА
-- ============================================
CREATE TABLE IF NOT EXISTS node_config (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    node_id TEXT UNIQUE NOT NULL,
    node_name TEXT DEFAULT 'Nexus Node',
    network TEXT DEFAULT 'testnet',
    listen_port INTEGER DEFAULT 8333,
    last_block_height INTEGER DEFAULT 0,
    last_block_hash TEXT,
    mining_enabled BOOLEAN DEFAULT 0,
    mining_address TEXT,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL
);

-- ============================================
-- 7. VIEWS (ПРЕДСТАВЛЕНИЯ)
-- ============================================

-- Баланс кошелька
CREATE VIEW IF NOT EXISTS wallet_balance AS
SELECT 
    w.address,
    w.public_key,
    w.created_at,
    w.last_activity,
    w.nonce,
    COALESCE((
        SELECT SUM(CASE 
            WHEN t.to_address = w.address THEN t.amount
            WHEN t.from_address = w.address THEN -t.amount - t.fee
            ELSE 0 
        END)
        FROM transactions t
        WHERE t.status = 'confirmed' 
        AND (t.to_address = w.address OR t.from_address = w.address)
    ), 0) as balance,
    COALESCE((
        SELECT SUM(amount) 
        FROM transactions 
        WHERE to_address = w.address AND status = 'confirmed'
    ), 0) as total_received,
    COALESCE((
        SELECT SUM(amount + fee) 
        FROM transactions 
        WHERE from_address = w.address AND status = 'confirmed'
    ), 0) as total_sent
FROM wallets w;

-- Последние блоки
CREATE VIEW IF NOT EXISTS recent_blocks AS
SELECT 
    b.height,
    b.hash,
    b.timestamp,
    datetime(b.timestamp, 'unixepoch') as datetime,
    b.tx_count,
    b.mined_by,
    b.difficulty
FROM blocks b
ORDER BY b.height DESC
LIMIT 20;

-- Последние транзакции
CREATE VIEW IF NOT EXISTS recent_transactions AS
SELECT 
    t.tx_hash,
    t.from_address,
    t.to_address,
    t.amount,
    t.fee,
    t.timestamp,
    datetime(t.timestamp, 'unixepoch') as datetime,
    t.block_height,
    t.status
FROM transactions t
ORDER BY t.timestamp DESC
LIMIT 50;

-- ============================================
-- ИНИЦИАЛИЗАЦИЯ ГЕНЕЗИС-ДАННЫХ
-- ============================================

-- 1. Создаем генезис-кошелек
INSERT OR IGNORE INTO wallets (address, public_key, created_at) 
VALUES (
    'genesis_miner',
    '04b5d7a4f5d9c8e3b2a1d4c7f6e9b8a2d4f6e8b1c3d5f7a9c2e4b6d8f0a1c3e5b7',
    strftime('%s', 'now')
);

-- 2. Создаем генезис-блок
INSERT OR IGNORE INTO blocks (
    height, hash, prev_hash, merkle_root, timestamp,
    nonce, difficulty, mined_by, tx_count, block_size, version
) VALUES (
    0,
    '0000000000000000000000000000000000000000000000000000000000000000',
    '0',
    '0000000000000000000000000000000000000000000000000000000000000000',
    strftime('%s', 'now'),
    0,
    1.0,
    'genesis_miner',
    1,
    256,
    1
);

-- 3. Создаем генезис-транзакцию (награда)
INSERT OR IGNORE INTO transactions (
    tx_hash, block_height, tx_index, from_address, to_address,
    amount, fee, signature, timestamp, data, status
) VALUES (
    '0000000000000000000000000000000000000000000000000000000000000000',
    0,
    0,
    'SYSTEM',
    'genesis_miner',
    100.0,
    0.0,
    'genesis_signature',
    strftime('%s', 'now'),
    'Genesis block reward',
    'confirmed'
);

-- 4. Конфигурация узла
INSERT OR IGNORE INTO node_config (
    id, node_id, node_name, network, listen_port,
    last_block_height, last_block_hash, mining_enabled,
    created_at, updated_at
) VALUES (
    1,
    hex(randomblob(16)),
    'Nexus Node',
    'testnet',
    8333,
    0,
    '0000000000000000000000000000000000000000000000000000000000000000',
    0,
    strftime('%s', 'now'),
    strftime('%s', 'now')
);

-- Включаем FOREIGN KEY обратно
PRAGMA foreign_keys = ON;

-- ============================================
-- ПРОВЕРКА
-- ============================================
SELECT '=== DATABASE INITIALIZED ===' as '';
SELECT 'Tables: ' || (SELECT count(*) FROM sqlite_master WHERE type='table');
SELECT 'Wallets: ' || (SELECT count(*) FROM wallets);
SELECT 'Blocks: ' || (SELECT count(*) FROM blocks);
SELECT 'Transactions: ' || (SELECT count(*) FROM transactions);
SELECT 'Genesis miner balance: ' || (SELECT balance FROM wallet_balance WHERE address = 'genesis_miner');