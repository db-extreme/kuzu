#pragma once

#include <functional>

#include "src/common/include/configs.h"
#include "src/common/types/include/types.h"

namespace kuzu {
namespace storage {

using namespace kuzu::common;

struct UnstructuredPropertyKeyDataType {
    uint32_t keyIdx;
    DataTypeID dataTypeID;
};

struct UnstrPropListWrapper {

    UnstrPropListWrapper(unique_ptr<uint8_t[]> data, uint64_t size, uint64_t capacity)
        : data{move(data)}, size{size}, capacity{capacity} {}

    unique_ptr<uint8_t[]> data;
    uint64_t size;
    uint64_t capacity;

    inline void clear() { size = 0; }

    inline void incrementSize(uint64_t insertedDataSize) { size += insertedDataSize; }

    void increaseCapacityIfNeeded(uint64_t requiredCapacity);

    // Warning: This is a very slow operation that slides the entire list after the offset to left.
    void removePropertyAtOffset(uint64_t offset, uint64_t dataTypeSize);
};

class UnstrPropListIterator {

public:
    UnstrPropListIterator() : UnstrPropListIterator(nullptr) {}
    UnstrPropListIterator(UnstrPropListWrapper* unstrPropListWrapper)
        : unstrPropListWrapper{unstrPropListWrapper}, curOff{0} {}

    inline bool hasNext() { return curOff < unstrPropListWrapper->size; }

    UnstructuredPropertyKeyDataType& readNextPropKeyValue();

    void skipValue();

    inline uint64_t getCurOff() { return curOff; }

    inline uint64_t getDataTypeSizeOfCurrProp() {
        assert(propKeyDataTypeForRetVal.keyIdx != UINT32_MAX);
        return Types::getDataTypeSize(propKeyDataTypeForRetVal.dataTypeID);
    }

    inline uint64_t getOffsetAtBeginningOfCurrProp() {
        assert(propKeyDataTypeForRetVal.keyIdx != UINT32_MAX);
        return curOff - StorageConfig::UNSTR_PROP_HEADER_LEN;
    }

    void copyValueOfCurrentProp(uint8_t* dst) {
        memcpy(dst, unstrPropListWrapper->data.get() + curOff, getDataTypeSizeOfCurrProp());
    }

private:
    UnstrPropListWrapper* unstrPropListWrapper;
    UnstructuredPropertyKeyDataType propKeyDataTypeForRetVal;
    uint64_t curOff;
};

class UnstrPropListUtils {
public:
    static bool findKeyPropertyAndPerformOp(UnstrPropListWrapper* updatedList, uint32_t propertyKey,
        std::function<void(UnstrPropListIterator&)> opToPerform);
    //  void (*func)(UnstrPropListIterator& itr)
};

} // namespace storage
} // namespace kuzu
