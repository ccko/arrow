// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include <gtest/gtest.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "arrow/array.h"
#include "arrow/builder.h"
#include "arrow/test-util.h"
#include "arrow/type.h"
#include "arrow/types/construct.h"
#include "arrow/types/integer.h"
#include "arrow/types/string.h"
#include "arrow/types/test-common.h"
#include "arrow/util/status.h"

using std::string;
using std::unique_ptr;
using std::vector;

namespace arrow {


TEST(TypesTest, TestCharType) {
  CharType t1(5);

  ASSERT_EQ(t1.type, TypeEnum::CHAR);
  ASSERT_TRUE(t1.nullable);
  ASSERT_EQ(t1.size, 5);

  ASSERT_EQ(t1.ToString(), string("char(5)"));

  // Test copy constructor
  CharType t2 = t1;
  ASSERT_EQ(t2.type, TypeEnum::CHAR);
  ASSERT_TRUE(t2.nullable);
  ASSERT_EQ(t2.size, 5);
}


TEST(TypesTest, TestVarcharType) {
  VarcharType t1(5);

  ASSERT_EQ(t1.type, TypeEnum::VARCHAR);
  ASSERT_TRUE(t1.nullable);
  ASSERT_EQ(t1.size, 5);
  ASSERT_EQ(t1.physical_type.size, 6);

  ASSERT_EQ(t1.ToString(), string("varchar(5)"));

  // Test copy constructor
  VarcharType t2 = t1;
  ASSERT_EQ(t2.type, TypeEnum::VARCHAR);
  ASSERT_TRUE(t2.nullable);
  ASSERT_EQ(t2.size, 5);
  ASSERT_EQ(t2.physical_type.size, 6);
}

TEST(TypesTest, TestStringType) {
  StringType str;
  StringType str_nn(false);

  ASSERT_EQ(str.type, TypeEnum::STRING);
  ASSERT_EQ(str.name(), string("string"));
  ASSERT_TRUE(str.nullable);
  ASSERT_FALSE(str_nn.nullable);
}

// ----------------------------------------------------------------------
// String container

class TestStringContainer : public ::testing::Test  {
 public:
  void SetUp() {
    chars_ = {'a', 'b', 'b', 'c', 'c', 'c'};
    offsets_ = {0, 1, 1, 1, 3, 6};
    nulls_ = {0, 0, 1, 0, 0};
    expected_ = {"a", "", "", "bb", "ccc"};

    MakeArray();
  }

  void MakeArray() {
    length_ = offsets_.size() - 1;
    int64_t nchars = chars_.size();

    value_buf_ = to_buffer(chars_);
    values_ = ArrayPtr(new UInt8Array(nchars, value_buf_));

    offsets_buf_ = to_buffer(offsets_);

    nulls_buf_ = bytes_to_null_buffer(nulls_.data(), nulls_.size());
    strings_.Init(length_, offsets_buf_, values_, nulls_buf_);
  }

 protected:
  vector<int32_t> offsets_;
  vector<char> chars_;
  vector<uint8_t> nulls_;

  vector<string> expected_;

  std::shared_ptr<Buffer> value_buf_;
  std::shared_ptr<Buffer> offsets_buf_;
  std::shared_ptr<Buffer> nulls_buf_;

  int64_t length_;

  ArrayPtr values_;
  StringArray strings_;
};


TEST_F(TestStringContainer, TestArrayBasics) {
  ASSERT_EQ(length_, strings_.length());
  ASSERT_TRUE(strings_.nullable());
}

TEST_F(TestStringContainer, TestType) {
  TypePtr type = strings_.type();

  ASSERT_EQ(TypeEnum::STRING, type->type);
  ASSERT_EQ(TypeEnum::STRING, strings_.type_enum());
}


TEST_F(TestStringContainer, TestListFunctions) {
  int pos = 0;
  for (size_t i = 0; i < expected_.size(); ++i) {
    ASSERT_EQ(pos, strings_.value_offset(i));
    ASSERT_EQ(expected_[i].size(), strings_.value_length(i));
    pos += expected_[i].size();
  }
}


TEST_F(TestStringContainer, TestDestructor) {
  auto arr = std::make_shared<StringArray>(length_, offsets_buf_, values_, nulls_buf_);
}

TEST_F(TestStringContainer, TestGetString) {
  for (size_t i = 0; i < expected_.size(); ++i) {
    if (nulls_[i]) {
      ASSERT_TRUE(strings_.IsNull(i));
    } else {
      ASSERT_EQ(expected_[i], strings_.GetString(i));
    }
  }
}

// ----------------------------------------------------------------------
// String builder tests

class TestStringBuilder : public TestBuilder {
 public:
  void SetUp() {
    TestBuilder::SetUp();
    type_ = TypePtr(new StringType());

    ArrayBuilder* tmp;
    ASSERT_OK(make_builder(type_, &tmp));
    builder_.reset(static_cast<StringBuilder*>(tmp));
  }

  void Done() {
    Array* out;
    ASSERT_OK(builder_->ToArray(&out));
    result_.reset(static_cast<StringArray*>(out));
  }

 protected:
  TypePtr type_;

  unique_ptr<StringBuilder> builder_;
  unique_ptr<StringArray> result_;
};

TEST_F(TestStringBuilder, TestAttrs) {
  ASSERT_FALSE(builder_->value_builder()->nullable());
}

TEST_F(TestStringBuilder, TestScalarAppend) {
  vector<string> strings = {"a", "bb", "", "", "ccc"};
  vector<uint8_t> is_null = {0, 0, 0, 1, 0};

  int N = strings.size();
  int reps = 1000;

  for (int j = 0; j < reps; ++j) {
    for (int i = 0; i < N; ++i) {
      if (is_null[i]) {
        builder_->AppendNull();
      } else {
        builder_->Append(strings[i]);
      }
    }
  }
  Done();

  ASSERT_EQ(reps * N, result_->length());
  ASSERT_EQ(reps * 6, result_->values()->length());

  int64_t length;
  int64_t pos = 0;
  for (int i = 0; i < N * reps; ++i) {
    if (is_null[i % N]) {
      ASSERT_TRUE(result_->IsNull(i));
    } else {
      ASSERT_FALSE(result_->IsNull(i));
      result_->GetValue(i, &length);
      ASSERT_EQ(pos, result_->offset(i));
      ASSERT_EQ(strings[i % N].size(), length);
      ASSERT_EQ(strings[i % N], result_->GetString(i));

      pos += length;
    }
  }
}

TEST_F(TestStringBuilder, TestZeroLength) {
  // All buffers are null
  Done();
}

} // namespace arrow
