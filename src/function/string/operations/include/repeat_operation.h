#pragma once

#include <cassert>
#include <cstring>

#include "src/common/types/include/ku_string.h"

using namespace std;
using namespace kuzu::common;

namespace kuzu {
namespace function {
namespace operation {

struct Repeat {
public:
    static inline void operation(
        ku_string_t& left, int64_t& right, ku_string_t& result, ValueVector& resultValueVector) {
        result.len = left.len * right;
        if (result.len <= ku_string_t::SHORT_STR_LENGTH) {
            repeatStr((char*)result.prefix, left.getAsString(), right);
        } else {
            result.overflowPtr = reinterpret_cast<uint64_t>(
                resultValueVector.getOverflowBuffer().allocateSpace(result.len));
            auto buffer = reinterpret_cast<char*>(result.overflowPtr);
            repeatStr(buffer, left.getAsString(), right);
            memcpy(result.prefix, buffer, ku_string_t::PREFIX_LENGTH);
        }
    }

private:
    static void repeatStr(char* data, string pattern, uint64_t count) {
        for (auto i = 0u; i < count; i++) {
            memcpy(data + i * pattern.length(), pattern.c_str(), pattern.length());
        }
    }
};

} // namespace operation
} // namespace function
} // namespace kuzu
