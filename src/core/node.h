// src/core/node.h
#pragma once
#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <vector>
#include <boost/asio.hpp>
#include "../blockchain/blockchain.h"
#include "../network/server.h"
#include "../network/client.h"
#include "../network/message.h"
#include "../metrics/metrics_registry.h"

namespace nexus {

class Node {
public:
    Node(const std::string& dbPath, int p2pPort, int metricsPort, const std::string& nodeId);
    ~Node();

    void start();
    void stop();
    void connectToPeer(const std::string& ip, int port);

private:
    void setupHandlers();
    void handleMessage(const Message& msg, std::shared_ptr<Peer> peer);
    void handleConnection(std::shared_ptr<Peer> peer);
    void syncWithPeer(std::shared_ptr<Peer> peer);
    void broadcastPeers();
    void broadcastTransaction(const Transaction& tx);
    void broadcastBlock(const Block& block);
    void updateMetrics();
    void startHttpServer();
    void broadcastPeersToAll();
    void mine_loop();
    void gossipPeers();
    void handleFork(const std::vector<Block>& alternative_chain);

    std::string nodeId_;
    int p2pPort_;
    int metricsPort_;
    std::unique_ptr<Blockchain> blockchain_;
    std::unique_ptr<Server> server_;
    std::unique_ptr<MetricsRegistry> metrics_;
    std::vector<std::shared_ptr<Client>> clients_;
    std::atomic<bool> running_{false};
    std::atomic<bool> mining_{false};
    std::thread mining_thread_;
    std::thread mempool_cleaner_thread_;

    std::vector<std::thread> background_threads_;

    boost::asio::io_context ioContext_;
    std::unique_ptr<boost::asio::io_context::work> work_;
    std::thread ioThread_;
};

} // namespace nexus
