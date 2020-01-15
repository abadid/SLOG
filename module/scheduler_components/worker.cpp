#include "module/scheduler_components/worker.h"

#include <thread>

#include "module/scheduler.h"
#include "module/scheduler_components/outputter.h"

namespace slog {

using internal::Request;
using internal::Response;

Worker::Worker(
    zmq::context_t& context,
    shared_ptr<Storage<Key, Record>> storage)
  : scheduler_socket_(context, ZMQ_REP),
    outputter_socket_(context, ZMQ_PUSH),
    storage_(storage),
    // TODO: change this dynamically based on selected experiment
    stored_procedures_(new KeyValueStoredProcedures()) {}

void Worker::SetUp() {
  scheduler_socket_.connect(Scheduler::WORKER_IN);
  outputter_socket_.connect(Outputter::WORKER_OUT);
}

void Worker::Loop() {
  MMessage msg(scheduler_socket_);
  if (!msg.IsProto<Request>()) {
    return;
  }
  Request req;
  msg.GetProto(req);
  if (req.type_case() != Request::kProcessTxn) {
    return;
  }

  auto txn = req.mutable_process_txn()->release_txn();
  ProcessTransaction(txn);

  ResponseToScheduler(txn->internal().id());
  ForwardToOutputter(txn);
}

void Worker::ProcessTransaction(Transaction* txn) {
  // Firstly, read all keys from the read set and write set to the buffer
  for (auto& key_value : *txn->mutable_read_set()) {
    Record record;
    storage_->Read(key_value.first, record);
    key_value.second = record.value;
  }
  for (auto& key_value : *txn->mutable_write_set()) {
    Record record;
    storage_->Read(key_value.first, record);
    key_value.second = record.value;
  }

  // Secondly, execute the transaction code
  stored_procedures_->Execute(*txn);

  // Lastly, apply all writes to local storage
  auto& master_metadata = txn->internal().master_metadata();
  for (const auto& key_value : txn->write_set()) {
    const auto& key = key_value.first;
    const auto& value = key_value.second;
    Record record;
    bool found = storage_->Read(key_value.first, record);
    if (!found) {
      CHECK(master_metadata.contains(key))
          << "Master metadata for key \"" << key << "\" is missing";
      record.metadata = master_metadata.at(key);
    }
    record.value = value;
    storage_->Write(key, record);
  }
  for (const auto& key : txn->delete_set()) {
    storage_->Delete(key);
  }
}

void Worker::ResponseToScheduler(TxnId txn_id) {
  Response process_txn_res;
  process_txn_res.mutable_process_txn()->set_txn_id(txn_id);
  MMessage msg;
  msg.Set(MM_PROTO, process_txn_res);
  msg.SendTo(scheduler_socket_);
}

void Worker::ForwardToOutputter(Transaction* txn) {
  Request forward_req;
  forward_req.mutable_forward_txn()->set_allocated_txn(txn);
  MMessage msg;
  msg.Set(MM_PROTO, forward_req);
  msg.SendTo(outputter_socket_);
}

} // namespace slog