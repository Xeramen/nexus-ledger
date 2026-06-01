// src/metrics/metrics_registry.h
#pragma once
#include <memory>
#include <string>
#include <prometheus/registry.h>
#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/exposer.h>

namespace nexus {

class MetricsRegistry {
public:
    MetricsRegistry(int port);
    ~MetricsRegistry() = default;
    
    void setPeers(int count);
    void setBlockchainHeight(int height);
    void setMempoolSize(int size);
    void incPacketsReceived(const std::string& type);
    void incPacketsSent(const std::string& type);
    void incBlocksMined();
    void incTransactionsProcessed();
    void setHashrate(double hashrate);
    void setMiningDifficulty(int difficulty);
    
private:
    std::shared_ptr<prometheus::Registry> registry_;
    std::unique_ptr<prometheus::Exposer> exposer_;
    
    prometheus::Family<prometheus::Gauge>* peers_gauge_;
    prometheus::Family<prometheus::Gauge>* height_gauge_;
    prometheus::Family<prometheus::Gauge>* mempool_gauge_;
    prometheus::Family<prometheus::Gauge>* hashrate_gauge_;
    prometheus::Family<prometheus::Gauge>* difficulty_gauge_;
    prometheus::Family<prometheus::Counter>* packets_recv_counter_;
    prometheus::Family<prometheus::Counter>* packets_sent_counter_;
    prometheus::Family<prometheus::Counter>* blocks_counter_;
    prometheus::Family<prometheus::Counter>* transactions_counter_;
};

} // namespace nexus