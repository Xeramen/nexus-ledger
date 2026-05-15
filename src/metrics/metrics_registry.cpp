// src/metrics/metrics_registry.cpp
#include "metrics_registry.h"
#include <iostream>

namespace nexus {

MetricsRegistry::MetricsRegistry(int port) {
    exposer_ = std::make_unique<prometheus::Exposer>("0.0.0.0:" + std::to_string(port));
    registry_ = std::make_shared<prometheus::Registry>();
    
    peers_gauge_ = &prometheus::BuildGauge()
        .Name("nexus_peers_active")
        .Help("Current number of active peers")
        .Register(*registry_);
    
    height_gauge_ = &prometheus::BuildGauge()
        .Name("nexus_blockchain_height")
        .Help("Current blockchain height")
        .Register(*registry_);
    
    mempool_gauge_ = &prometheus::BuildGauge()
        .Name("nexus_mempool_size")
        .Help("Number of pending transactions")
        .Register(*registry_);
    
    packets_recv_counter_ = &prometheus::BuildCounter()
        .Name("nexus_packets_received_total")
        .Help("Total packets received")
        .Register(*registry_);
    
    packets_sent_counter_ = &prometheus::BuildCounter()
        .Name("nexus_packets_sent_total")
        .Help("Total packets sent")
        .Register(*registry_);
    
    blocks_counter_ = &prometheus::BuildCounter()
        .Name("nexus_blocks_mined_total")
        .Help("Total blocks mined")
        .Register(*registry_);
    
    transactions_counter_ = &prometheus::BuildCounter()
        .Name("nexus_transactions_processed_total")
        .Help("Total transactions processed")
        .Register(*registry_);
    
    exposer_->RegisterCollectable(registry_);
    std::cout << "✅ Metrics server started on port " << port << std::endl;
}

void MetricsRegistry::setPeers(int count) {
    peers_gauge_->Add({}).Set(count);
}

void MetricsRegistry::setBlockchainHeight(int height) {
    height_gauge_->Add({}).Set(height);
}

void MetricsRegistry::setMempoolSize(int size) {
    mempool_gauge_->Add({}).Set(size);
}

void MetricsRegistry::incPacketsReceived(const std::string& type) {
    packets_recv_counter_->Add({{"type", type}}).Increment();
}

void MetricsRegistry::incPacketsSent(const std::string& type) {
    packets_sent_counter_->Add({{"type", type}}).Increment();
}

void MetricsRegistry::incBlocksMined() {
    blocks_counter_->Add({}).Increment();
}

void MetricsRegistry::incTransactionsProcessed() {
    transactions_counter_->Add({}).Increment();
}

} // namespace nexus