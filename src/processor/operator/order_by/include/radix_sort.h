#pragma once

#include <queue>

#include "src/common/include/configs.h"
#include "src/common/include/vector/value_vector.h"
#include "src/processor/operator/order_by/include/key_block_merger.h"
#include "src/processor/operator/order_by/include/order_by_key_encoder.h"
#include "src/processor/result/include/factorized_table.h"

using namespace kuzu::common;

namespace kuzu {
namespace processor {

struct TieRange {
public:
    uint32_t startingTupleIdx;
    uint32_t endingTupleIdx;
    inline uint32_t getNumTuples() { return endingTupleIdx - startingTupleIdx + 1; }
    explicit TieRange(uint32_t startingTupleIdx, uint32_t endingTupleIdx)
        : startingTupleIdx{startingTupleIdx}, endingTupleIdx{endingTupleIdx} {}
};

// RadixSort sorts a block of binary strings using the radixSort and quickSort (only for comparing
// string overflow pointers). The algorithm loops through each column of the orderByVectors. If it
// sees a column with string, which is variable length, or unstructured type, which may be variable
// length, it will call radixSort to sort the columns seen so far. If there are tie tuples, it will
// compare the overflow ptr of strings or the actual values of unstructured data. For subsequent
// columns, the algorithm only calls radixSort on tie tuples.
class RadixSort {
public:
    RadixSort(MemoryManager* memoryManager, FactorizedTable& factorizedTable,
        OrderByKeyEncoder& orderByKeyEncoder,
        vector<StringAndUnstructuredKeyColInfo> stringAndUnstructuredKeyColInfo)
        : tmpSortingResultBlock{make_unique<DataBlock>(memoryManager)},
          tmpTuplePtrSortingBlock{make_unique<DataBlock>(memoryManager)},
          factorizedTable{factorizedTable},
          stringAndUnstructuredKeyColInfo{stringAndUnstructuredKeyColInfo},
          numBytesPerTuple{orderByKeyEncoder.getNumBytesPerTuple()}, numBytesToRadixSort{
                                                                         numBytesPerTuple - 8} {}

    void sortSingleKeyBlock(const DataBlock& keyBlock);

private:
    void radixSort(uint8_t* keyBlockPtr, uint32_t numTuplesToSort, uint32_t numBytesSorted,
        uint32_t numBytesToSort);

    vector<TieRange> findTies(uint8_t* keyBlockPtr, uint32_t numTuplesToFindTies,
        uint32_t numBytesToSort, uint32_t baseTupleIdx);

    void fillTmpTuplePtrSortingBlock(TieRange& keyBlockTie, uint8_t* keyBlockPtr);

    void reOrderKeyBlock(TieRange& keyBlockTie, uint8_t* keyBlockPtr);

    // Some ties can't be solved in quicksort, just add them to ties.
    template<typename TYPE>
    void findStringAndUnstructuredTies(TieRange& keyBlockTie, uint8_t* keyBlockPtr,
        queue<TieRange>& ties, StringAndUnstructuredKeyColInfo& keyColInfo);

    void solveStringAndUnstructuredTies(TieRange& keyBlockTie, uint8_t* keyBlockPtr,
        queue<TieRange>& ties, StringAndUnstructuredKeyColInfo& keyColInfo);

private:
    unique_ptr<DataBlock> tmpSortingResultBlock;
    // Since we do radix sort on each dataBlock at a time, the maxNumber of tuples in the dataBlock
    // is: LARGE_PAGE_SIZE / numBytesPerTuple.
    // The size of tmpTuplePtrSortingBlock should be larger than:
    // sizeof(uint8_t*) * MaxNumOfTuplePointers=(LARGE_PAGE_SIZE / numBytesPerTuple).
    // Since we know: numBytesPerTuple >= sizeof(uint8_t*) (note: we put the
    // tupleIdx/FactorizedTableIdx at the end of each row in dataBlock), sizeof(uint8_t*) *
    // MaxNumOfTuplePointers=(LARGE_PAGE_SIZE / numBytesPerTuple) <= LARGE_PAGE_SIZE. As a result,
    // we only need one dataBlock to store the tuplePointers while solving the string ties.
    unique_ptr<DataBlock> tmpTuplePtrSortingBlock;
    // factorizedTable stores all columns in the tuples that will be sorted, including the order by
    // key columns. RadixSort uses factorizedTable to access the full contents of the string and
    // unstructured columns when resolving ties.
    FactorizedTable& factorizedTable;
    vector<StringAndUnstructuredKeyColInfo> stringAndUnstructuredKeyColInfo;
    uint32_t numBytesPerTuple;
    uint32_t numBytesToRadixSort;
};

} // namespace processor
} // namespace kuzu
