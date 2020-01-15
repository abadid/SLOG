#pragma once

#include <string>

namespace slog {

const long BROKER_POLL_TIMEOUT_MS = 100;
const long MODULE_POLL_TIMEOUT_MS = 50;

const std::string SERVER_CHANNEL("server");
const std::string FORWARDER_CHANNEL("forwarder");
const std::string SEQUENCER_CHANNEL("sequencer");
const std::string SCHEDULER_CHANNEL("scheduler");

const std::string LOCAL_PAXOS("local");
const std::string MULTI_HOME_PAXOS("multi_home");

const uint32_t MAX_NUM_MACHINES = 1000;

const size_t MM_PROTO = 0;
const size_t MM_FROM_CHANNEL = 1;
const size_t MM_TO_CHANNEL = 2;

const uint32_t PAXOS_DEFAULT_LEADER_POSITION = 0;

const size_t LOCK_TABLE_SIZE_LIMIT = 1000000;

} // namespace slog