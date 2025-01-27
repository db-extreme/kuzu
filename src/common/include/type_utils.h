#pragma once

#include "src/common/types/include/literal.h"
#include "src/common/types/include/types_include.h"
#include "src/common/types/include/value.h"

namespace kuzu {
namespace common {

class TypeUtils {

public:
    static int64_t convertToInt64(const char* data);
    static double_t convertToDouble(const char* data);
    static uint32_t convertToUint32(const char* data);
    static bool convertToBoolean(const char* data);

    static inline string toString(bool boolVal) { return boolVal ? "True" : "False"; }
    static inline string toString(int64_t val) { return to_string(val); }
    static inline string toString(double val) { return to_string(val); }
    static inline string toString(const nodeID_t& val) {
        return to_string(val.tableID) + ":" + to_string(val.offset);
    }
    static inline string toString(const date_t& val) { return Date::toString(val); }
    static inline string toString(const timestamp_t& val) { return Timestamp::toString(val); }
    static inline string toString(const interval_t& val) { return Interval::toString(val); }
    static inline string toString(const ku_string_t& val) { return val.getAsString(); }
    static inline string toString(const string& val) { return val; }
    static string toString(const ku_list_t& val, const DataType& dataType);
    static string toString(const Literal& literal);
    static string toString(const Value& val);

    static inline void encodeOverflowPtr(
        uint64_t& overflowPtr, page_idx_t pageIdx, uint16_t pageOffset) {
        memcpy(&overflowPtr, &pageIdx, 4);
        memcpy(((uint8_t*)&overflowPtr) + 4, &pageOffset, 2);
    }
    static inline void decodeOverflowPtr(
        uint64_t overflowPtr, page_idx_t& pageIdx, uint16_t& pageOffset) {
        pageIdx = 0;
        memcpy(&pageIdx, &overflowPtr, 4);
        memcpy(&pageOffset, ((uint8_t*)&overflowPtr) + 4, 2);
    }

    template<typename T>
    static inline bool isValueEqual(
        T& left, T& right, const DataType& leftDataType, const DataType& rightDataType) {
        return left == right;
    }

private:
    static string elementToString(const DataType& dataType, uint8_t* overflowPtr, uint64_t pos);

    static void throwConversionExceptionOutOfRange(const char* data, DataTypeID dataTypeID);
    static void throwConversionExceptionIfNoOrNotEveryCharacterIsConsumed(
        const char* data, const char* eptr, DataTypeID dataTypeID);
    static string prefixConversionExceptionMessage(const char* data, DataTypeID dataTypeID);
};

template<>
inline bool TypeUtils::isValueEqual(ku_list_t& left, ku_list_t& right, const DataType& leftDataType,
    const DataType& rightDataType) {
    if (leftDataType != rightDataType || left.size != right.size) {
        return false;
    }

    for (auto i = 0u; i < left.size; i++) {
        switch (leftDataType.childType->typeID) {
        case BOOL: {
            if (!isValueEqual(reinterpret_cast<uint8_t*>(left.overflowPtr)[i],
                    reinterpret_cast<uint8_t*>(right.overflowPtr)[i], *leftDataType.childType,
                    *rightDataType.childType)) {
                return false;
            }
        } break;
        case INT64: {
            if (!isValueEqual(reinterpret_cast<int64_t*>(left.overflowPtr)[i],
                    reinterpret_cast<int64_t*>(right.overflowPtr)[i], *leftDataType.childType,
                    *rightDataType.childType)) {
                return false;
            }
        } break;
        case DOUBLE: {
            if (!isValueEqual(reinterpret_cast<double_t*>(left.overflowPtr)[i],
                    reinterpret_cast<double_t*>(right.overflowPtr)[i], *leftDataType.childType,
                    *rightDataType.childType)) {
                return false;
            }
        } break;
        case STRING: {
            if (!isValueEqual(reinterpret_cast<ku_string_t*>(left.overflowPtr)[i],
                    reinterpret_cast<ku_string_t*>(right.overflowPtr)[i], *leftDataType.childType,
                    *rightDataType.childType)) {
                return false;
            }
        } break;
        case DATE: {
            if (!isValueEqual(reinterpret_cast<date_t*>(left.overflowPtr)[i],
                    reinterpret_cast<date_t*>(right.overflowPtr)[i], *leftDataType.childType,
                    *rightDataType.childType)) {
                return false;
            }
        } break;
        case TIMESTAMP: {
            if (!isValueEqual(reinterpret_cast<timestamp_t*>(left.overflowPtr)[i],
                    reinterpret_cast<timestamp_t*>(right.overflowPtr)[i], *leftDataType.childType,
                    *rightDataType.childType)) {
                return false;
            }
        } break;
        case INTERVAL: {
            if (!isValueEqual(reinterpret_cast<interval_t*>(left.overflowPtr)[i],
                    reinterpret_cast<interval_t*>(right.overflowPtr)[i], *leftDataType.childType,
                    *rightDataType.childType)) {
                return false;
            }
        } break;
        case LIST: {
            if (!isValueEqual(reinterpret_cast<ku_list_t*>(left.overflowPtr)[i],
                    reinterpret_cast<ku_list_t*>(right.overflowPtr)[i], *leftDataType.childType,
                    *rightDataType.childType)) {
                return false;
            }
        } break;
        default: {
            assert(false);
        }
        }
    }
    return true;
}

} // namespace common
} // namespace kuzu
