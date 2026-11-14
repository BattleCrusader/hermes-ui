// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_PROMPT_OPERATION_QUEUE_H_
#define HERMES_UI_PROMPT_OPERATION_QUEUE_H_

#include <QString>
#include <QStringList>
#include <cstddef>
#include <deque>
#include <functional>
#include <iterator>
#include <set>
#include <utility>

namespace hermes::ui {

// One outbound prompt or steer. The worker sends at most one of these at a
// time and waits for its acknowledgement before starting the next, so socket
// send order, admission order, and local transcript order cannot drift.
struct PromptOperation {
  enum class Kind { kPrompt, kSteer };

  Kind kind = Kind::kPrompt;
  QString session_id;
  QString text;
  qint64 truncate_before_row_id = 0;
  bool confirm_empty_truncate = false;
  bool confirm_deep_truncate = false;
  bool queued = false;
  int execution_generation = -1;
  QStringList attachments;
  // Attachment RPC ids still outstanding before the prompt frame may be sent.
  std::set<int> attachment_request_ids;
  // The prompt.submit or session.steer RPC currently awaiting a response.
  int request_id = -1;
  std::function<void(bool, const QString&)> done;
};

// FIFO with a single active head. `PushAfterActive` is for a fallback prompt
// created by an active steer: it belongs immediately after that steer, ahead
// of operations the user queued later.
class PromptOperationQueue {
 public:
  void PushBack(PromptOperation operation) {
    operations_.push_back(std::move(operation));
  }

  void PushAfterActive(PromptOperation operation) {
    if (active_ && !operations_.empty()) {
      operations_.insert(std::next(operations_.begin()),
                         std::move(operation));
      return;
    }
    operations_.push_front(std::move(operation));
  }

  bool BeginNext() {
    if (active_ || operations_.empty()) {
      return false;
    }
    active_ = true;
    return true;
  }

  PromptOperation* Active() {
    return active_ && !operations_.empty() ? &operations_.front() : nullptr;
  }

  const PromptOperation* Active() const {
    return active_ && !operations_.empty() ? &operations_.front() : nullptr;
  }

  bool FinishActive() {
    if (!active_ || operations_.empty()) {
      return false;
    }
    operations_.pop_front();
    active_ = false;
    return true;
  }

  bool active() const { return active_ && !operations_.empty(); }
  bool empty() const { return operations_.empty(); }
  std::size_t size() const { return operations_.size(); }

  std::deque<PromptOperation>& operations() { return operations_; }
  const std::deque<PromptOperation>& operations() const {
    return operations_;
  }

  void Clear() {
    operations_.clear();
    active_ = false;
  }

 private:
  std::deque<PromptOperation> operations_;
  bool active_ = false;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_PROMPT_OPERATION_QUEUE_H_
