#pragma once

#include <vector>
#include <glog/logging.h>

#include "common/configuration.h"
#include "connection/broker.h"
#include "connection/sender.h"
#include "connection/zmq_utils.h"
#include "module/base/module.h"
#include "storage/mem_only_storage.h"
#include "proto/internal.pb.h"

using std::string;
using std::shared_ptr;
using std::unique_ptr;

namespace slog {

using ConfigVec = std::vector<ConfigurationPtr>;

internal::Request MakeEchoRequest(const string& data);
internal::Response MakeEchoResponse(const string& data);

ConfigVec MakeTestConfigurations(
    string&& prefix,
    int num_replicas, 
    int num_partitions,
    uint32_t seed = 0,
    internal::Configuration common_config = {});

Transaction* FillMetadata(Transaction* txn, uint32_t master = 0, uint32_t counter = 0);

using ModuleRunnerPtr = unique_ptr<ModuleRunner>;

/**
 * This is a fake SLOG system where we can only add a subset
 * of modules to test them in isolation.
 */
class TestSlog {
public:
  TestSlog(const ConfigurationPtr& config);
  void Data(Key&& key, Record&& record);
  void AddServerAndClient();
  void AddForwarder();
  void AddSequencer();
  void AddInterleaver();
  void AddScheduler();
  void AddLocalPaxos();
  void AddGlobalPaxos();
  void AddMultiHomeOrderer();

  void AddOutputChannel(Channel channel);
  zmq::pollitem_t GetPollItemForChannel(Channel channel);

  template<typename T>
  bool ReceiveFromOutputChannel(T& out, Channel channel) {
    CHECK(channels_.count(channel) > 0) << "Channel " << channel << " does not exist";
    return ReceiveProto(channels_[channel], out);
  }

  unique_ptr<Sender> GetSender();

  void StartInNewThreads();
  void SendTxn(Transaction* txn);
  Transaction RecvTxnResult();

private:
  ConfigurationPtr config_;
  shared_ptr<zmq::context_t> context_;
  shared_ptr<MemOnlyStorage<Key, Record, Metadata>> storage_;
  shared_ptr<Broker> broker_;
  ModuleRunnerPtr ticker_;
  ModuleRunnerPtr server_;
  ModuleRunnerPtr forwarder_;
  ModuleRunnerPtr sequencer_;
  ModuleRunnerPtr interleaver_;
  ModuleRunnerPtr scheduler_;
  ModuleRunnerPtr local_paxos_;
  ModuleRunnerPtr global_paxos_;
  ModuleRunnerPtr multi_home_orderer_;

  unordered_map<Channel, zmq::socket_t> channels_;

  zmq::context_t client_context_;
  zmq::socket_t client_socket_;
};

} // namespace slog