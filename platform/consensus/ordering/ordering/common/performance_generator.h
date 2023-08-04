/*
 * Copyright (c) 2019-2022 ExpoLab, UC Davis
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#pragma once

#include <future>

#include "platform/config/resdb_config.h"
#include "platform/consensus/ordering/common/data_collector_pool.h"
#include "platform/networkstrate/replica_communicator.h"
#include "platform/networkstrate/server_comm.h"
#include "platform/statistic/stats.h"

namespace resdb {

class PerformancGeneratorBasic {
 public:
  PerformancGeneratorBasic(const ResDBConfig& config,
                           ReplicaCommunicator* replica_communicator,
                           SystemInfo* system_info,
                           SignatureVerifier* verifier);

  virtual ~PerformancGeneratorBasic();

  int StartEval();

  int ProcessResponseMsg(std::unique_ptr<Context> context,
                         std::unique_ptr<Request> request);
  void SetDataFunc(std::function<std::string()> func);

  virtual void ConverToRequest(const BatchUserRequest& batch_request,
                               Request* new_request) {}

  virtual void PostSend(){};

  virtual int GetPrimary();

 private:
  // Add response messages which will be sent back to the caller
  // if there are f+1 same messages.
  DataCollector::CollectorResultCode AddResponseMsg(
      std::unique_ptr<Request> request,
      std::function<void(const std::string&)> call_back);

  void SendResponseToClient(const BatchUserResponse& batch_response);

  struct QueueItem {
    std::unique_ptr<Request> user_request;
  };

  bool MayConsensusChangeStatus(
      int type, int received_count,
      std::atomic<DataCollector::TransactionStatue>* status);
  int DoBatch(const std::vector<std::unique_ptr<QueueItem>>& batch_req);
  int BatchProposeMsg();
  std::unique_ptr<Request> GenerateUserRequest();
  void SendMessage(const Request& new_request);

 protected:
  ResDBConfig config_;
  SystemInfo* system_info_;
  SignatureVerifier* verifier_;

 private:
  ReplicaCommunicator* replica_communicator_;
  std::unique_ptr<DataCollectorPool> collector_pool_;
  LockFreeQueue<QueueItem> batch_queue_;
  std::thread user_req_thread_[16];
  std::atomic<bool> stop_;
  Stats* global_stats_;
  std::atomic<uint32_t> send_num_;
  std::mutex mutex_;
  std::atomic<int> total_num_;
  std::function<std::string()> data_func_;
  std::future<bool> eval_ready_future_;
  std::promise<bool> eval_ready_promise_;
  std::atomic<bool> eval_started_;
};

template <typename RequestType>
class PerformancGenerator : public PerformancGeneratorBasic {
 public:
  virtual void ConverToRequest(const BatchUserRequest& batch_request,
                               Request* new_request) {
    RequestType custom_request;
    batch_request.SerializeToString(custom_request.mutable_data());
    if (verifier_) {
      auto signature_or = verifier_->SignMessage(custom_request.data());
      if (!signature_or.ok()) {
        LOG(ERROR) << "Sign message fail";
        return;
      }
      *custom_request.mutable_data_signature() = *signature_or;
    }
    custom_request.set_hash(
        SignatureVerifier::CalculateHash(custom_request.data()));
    custom_request.set_proxy_id(config_.GetSelfInfo().id());
    custom_request.set_type(Request::TYPE_CLIENT_REQUEST);
    custom_request.SerializeToString(new_request->mutable_data());
  }
};

}  // namespace resdb
