#pragma once

#include "src/common/types/include/ku_string.h"
#include "src/function/string/operations/include/find_operation.h"

using namespace kuzu::common;

namespace kuzu {
namespace function {
namespace operation {

struct EndsWith {
    static inline void operation(ku_string_t& left, ku_string_t& right, uint8_t& result) {
        int64_t pos = 0;
        Find::operation(left, right, pos);
        result = (pos == left.len - right.len + 1);
    }
};

} // namespace operation
} // namespace function
} // namespace kuzu
