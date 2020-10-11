#include "interleaver.h"

#include <stack>

#include <glog/logging.h>

#include "common/configuration.h"
#include "common/constants.h"
#include "common/proto_utils.h"
#include "proto/internal.pb.h"

using std::pair;
using std::stack;

namespace slog {

using internal::Request;
using internal::Response;

void LocalLog::AddBatchId(
    uint32_t queue_id, uint32_t position, BatchId batch_id) {
  batch_queues_[queue_id].Insert(position, batch_id);
  UpdateReadyBatches();
}

void LocalLog::AddSlot(SlotId slot_id, uint32_t queue_id) {
  slots_.Insert(slot_id, queue_id);
  UpdateReadyBatches();
}

bool LocalLog::HasNextBatch() const {
  return !ready_batches_.empty();
}

pair<SlotId, BatchId> LocalLog::NextBatch() {
  if (!HasNextBatch()) {
    throw std::runtime_error("NextBatch() was called when there is no batch");
  }
  auto next_batch = ready_batches_.front();
  ready_batches_.pop();
  return next_batch;
}

void LocalLog::UpdateReadyBatches() {
  while (slots_.HasNext()) {
    auto next_queue_id = slots_.Peek();
    if (batch_queues_.count(next_queue_id) == 0) {
      break;
    }
    auto& next_queue = batch_queues_.at(next_queue_id);
    if (!next_queue.HasNext()) {
      break;
    }
    auto slot_id = slots_.Next().first;
    auto batch_id = next_queue.Next().second;
    ready_batches_.emplace(slot_id, batch_id);
  }
}

Interleaver::Interleaver(
    const ConfigurationPtr& config,
    const shared_ptr<Broker>& broker) :
    NetworkedModule(broker, kInterleaverChannel),
    config_(config) {}

void Interleaver::HandleInternalRequest(internal::Request&& req, MachineIdNum from) {
  if (req.type_case() == Request::kLocalQueueOrder) {
    auto& order = req.local_queue_order();
    VLOG(1) << "Received local queue order. Slot id: "
            << order.slot() << ". Queue id: " << order.queue_id(); 

    local_log_.AddSlot(order.slot(), order.queue_id());

  } else if (req.type_case() == Request::kForwardBatch) {
    auto forward_batch = req.mutable_forward_batch();
    auto [from_replica, from_partition] = config_->UnpackMachineId(from);

    switch (forward_batch->part_case()) {
      case internal::ForwardBatch::kBatchData: {
        auto batch = BatchPtr{forward_batch->release_batch_data()};

        RecordTxnEvent(
            config_,
            batch.get(),
            TransactionEvent::ENTER_INTERLEAVER_IN_BATCH);

        switch (batch->transaction_type()) {

          case TransactionType::SINGLE_HOME:
            VLOG(1) << "Received data for SINGLE-HOME batch " << batch->id()
                << " from [" << from << "]. Number of txns: " << batch->transactions_size();

            if (from_replica == config_->local_replica()) {
              local_log_.AddBatchId(
                  from_partition /* queue_id */,
                  // Batches generated by the same machine need to follow the order
                  // of creation. This field is used to keep track of that order
                  forward_batch->same_origin_position(),
                  batch->id());
            }
            
            single_home_logs_[from_replica].AddBatch(move(batch));

            break; 
          case TransactionType::MULTI_HOME:
            VLOG(1) << "Received data for MULTI-HOME batch " << batch->id()
                    << ". Number of txns: " << batch->transactions_size();
            // MULTI-HOME txns are already ordered with respect to each other
            // and their IDs have been replaced with slot id in the orderer module
            // so here their id and slot id are the same
            multi_home_log_.AddSlot(batch->id(), batch->id());
            multi_home_log_.AddBatch(move(batch));
            break;
          default: 
            LOG(ERROR) << "Received batch with invalid transaction type. "
              << "Only SINGLE_HOME and MULTI_HOME are accepted. Received "
              << ENUM_NAME(batch->transaction_type(), TransactionType);
            break;
        }

        break;
      }
      case internal::ForwardBatch::kBatchOrder: {
        auto& batch_order = forward_batch->batch_order();

        VLOG(1) << "Received order for batch " << batch_order.batch_id()
                << " from [" << from << "]. Slot: " << batch_order.slot();

        single_home_logs_[from_replica].AddSlot(
            batch_order.slot(),
            batch_order.batch_id());
        break;
      }
      default:
        break;
    }
  }

  AdvanceLogs();
}

void Interleaver::AdvanceLogs() {
  // Advance local log
  auto local_partition = config_->local_partition();
  auto local_replica = config_->local_replica();
  while (local_log_.HasNextBatch()) {
    auto next_batch = local_log_.NextBatch();
    auto slot_id = next_batch.first;
    auto batch_id = next_batch.second;

    // Replicate the batch and slot id to the corresponding partition in other regions
    Request request;
    auto forward_batch_order = request.mutable_forward_batch()->mutable_batch_order();
    forward_batch_order->set_batch_id(batch_id);
    forward_batch_order->set_slot(slot_id);
    auto num_replicas = config_->num_replicas();
    for (uint32_t rep = 0; rep < num_replicas; rep++) {
      if (rep != local_replica) {
        Send(
            request,
            kInterleaverChannel,
            config_->MakeMachineIdNum(rep, local_partition));
      }
    }
    single_home_logs_[local_replica].AddSlot(slot_id, batch_id);
  }

  // Advance single-home logs
  for (auto& pair : single_home_logs_) {
    auto& log = pair.second;
    while (log.HasNextBatch()) {
      EmitBatch(log.NextBatch().second);
    }
  }

  // Advance multi-home log
  while (multi_home_log_.HasNextBatch()) {
    EmitBatch(multi_home_log_.NextBatch().second);
  }
}

void Interleaver::EmitBatch(BatchPtr&& batch) {
  VLOG(1) << "Processing batch " << batch->id() << " from global log";

  stack<Transaction*> buffer;
  auto transactions = batch->mutable_transactions();
  // Calling ReleaseLast creates an incorrect order of transactions in a batch; thus,
  // buffering them in a stack to reverse the order.
  while (!transactions->empty()) {
    auto txn = transactions->ReleaseLast();
    auto txn_internal = txn->mutable_internal();

    // Transfer recorded events from batch to each txn in the batch
    txn_internal->mutable_events()->MergeFrom(batch->events());
    txn_internal->mutable_event_times()->MergeFrom(batch->event_times());
    txn_internal->mutable_event_machines()->MergeFrom(batch->event_machines());

    buffer.push(txn);
  }
  
  while (!buffer.empty()) {
    auto txn = buffer.top();
    RecordTxnEvent(
        config_,
        txn->mutable_internal(),
        TransactionEvent::EXIT_INTERLEAVER);

    Request request;
    auto forward_txn = request.mutable_forward_txn();
    forward_txn->set_allocated_txn(txn);
    Send(request, kSchedulerChannel);
    buffer.pop();
  }
}

} // namespace slog