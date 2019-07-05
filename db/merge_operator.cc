//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).
//
/**
 * Back-end implementation details specific to the Merge Operator.
 */

#include "rocksdb/merge_operator.h"
#include "rocksdb/status.h"

namespace rocksdb {

bool MergeOperator::FullMergeV2(const MergeOperationInput& merge_in,
                                MergeOperationOutput* merge_out) const {
  // If FullMergeV2 is not implemented, we convert the operand_list to
  // std::deque<std::string> and pass it to FullMerge
  std::deque<std::string> operand_list_str;
  for (auto& op : merge_in.operand_list) {
    LazySlice slice = op.get();
    if (!slice.decode().ok()) {
      return false;
    }
    operand_list_str.emplace_back(slice->data(), slice->size());
  }
  const Slice* existing_value_ptr = nullptr;
  LazySlice slice;
  if (merge_in.existing_value != nullptr) {
    slice = merge_in.existing_value->get();
    if (!slice.decode().ok()) {
      return false;
    }
    existing_value_ptr = slice.get();
  }
  return FullMerge(merge_in.key, existing_value_ptr, operand_list_str,
                   &merge_out->new_value, merge_in.logger);
}

// The default implementation of PartialMergeMulti, which invokes
// PartialMerge multiple times internally and merges two operands at
// a time.
bool MergeOperator::PartialMergeMulti(
    const Slice& key, const std::vector<FutureSlice>& operand_list,
     std::string* new_value, Logger* logger) const {
  assert(operand_list.size() >= 2);
  // Simply loop through the operands
  FutureSlice temp_slice = MakeReferenceOfFutureSlice(operand_list[0]);

  for (size_t i = 1; i < operand_list.size(); ++i) {
    auto& operand = operand_list[i];
    std::string temp_value;
    if (!PartialMerge(key, temp_slice, operand, &temp_value, logger)) {
      return false;
    }
    swap(temp_value, *new_value);
    temp_slice.reset(*new_value, false/* copy */);
  }

  // The result will be in *new_value. All merges succeeded.
  return true;
}

// Given a "real" merge from the library, call the user's
// associative merge function one-by-one on each of the operands.
// NOTE: It is assumed that the client's merge-operator will handle any errors.
bool AssociativeMergeOperator::FullMergeV2(
    const MergeOperationInput& merge_in,
    MergeOperationOutput* merge_out) const {
  // Simply loop through the operands
  FutureSlice temp_existing;
  LazySlice existing_slice;
  LazySlice operand_slice;
  const FutureSlice* existing_value = merge_in.existing_value;
  for (const auto& operand : merge_in.operand_list) {
    const Slice* existing_slice_ptr = nullptr;
    if (existing_value != nullptr) {
      if (!(existing_slice = existing_value->get()).decode().ok()) {
        return false;
      }
      existing_slice_ptr = existing_slice.get();
    }
    operand_slice = operand.get();
    if (!operand_slice.decode().ok()) {
      return false;
    }
    std::string temp_value;
    if (!Merge(merge_in.key, existing_slice_ptr, *operand_slice, &temp_value,
               merge_in.logger)) {
      return false;
    }
    swap(temp_value, merge_out->new_value);
    temp_existing.reset(merge_out->new_value, false/* copy */);
    existing_value = &temp_existing;
  }

  // The result will be in *new_value. All merges succeeded.
  return true;
}

// Call the user defined simple merge on the operands;
// NOTE: It is assumed that the client's merge-operator will handle any errors.
bool AssociativeMergeOperator::PartialMerge(const Slice& key,
                                            const FutureSlice& left_operand,
                                            const FutureSlice& right_operand,
                                            std::string* new_value,
                                            Logger* logger) const {
  LazySlice left = left_operand.get();
  LazySlice right = right_operand.get();
  if (!left.decode().ok() || !right.decode().ok()) {
    return false;
  }
  return Merge(key, left.get(), *right, new_value, logger);
}

} // namespace rocksdb
