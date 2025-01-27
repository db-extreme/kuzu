#include <numeric>
#include <variant>
#include <vector>

#include "gtest/gtest.h"

#include "src/common/include/assert.h"
#include "src/common/include/configs.h"
#include "src/common/include/data_chunk/data_chunk.h"
#include "src/common/types/include/value.h"
#include "src/processor/operator/order_by/include/order_by_key_encoder.h"
#include "src/processor/operator/order_by/include/radix_sort.h"

using ::testing::Test;
using namespace kuzu::processor;
using namespace std;

class RadixSortTest : public Test {

public:
    void SetUp() override {
        bufferManager =
            make_unique<BufferManager>(StorageConfig::DEFAULT_BUFFER_POOL_SIZE_FOR_TESTING);
        memoryManager = make_unique<MemoryManager>(bufferManager.get());
    }

public:
    unique_ptr<BufferManager> bufferManager;
    unique_ptr<MemoryManager> memoryManager;
    const uint8_t factorizedTableIdx = 9;
    const uint32_t numTuplesPerBlockInFT = LARGE_PAGE_SIZE / 8;

    void checkTupleIdxesAndFactorizedTableIdxes(uint8_t* keyBlockPtr, const uint64_t entrySize,
        const vector<uint64_t>& expectedFTBlockOffsetOrder) {
        for (auto expectedFTBlockOffset : expectedFTBlockOffsetOrder) {
            auto tupleInfoPtr = keyBlockPtr + entrySize - 8;
            ASSERT_EQ(OrderByKeyEncoder::getEncodedFTIdx(tupleInfoPtr), factorizedTableIdx);
            ASSERT_EQ(OrderByKeyEncoder::getEncodedFTBlockIdx(tupleInfoPtr), 0);
            auto encodedFTBlockOffset = OrderByKeyEncoder::getEncodedFTBlockOffset(tupleInfoPtr);
            if (expectedFTBlockOffset != -1) {
                ASSERT_EQ(encodedFTBlockOffset, expectedFTBlockOffset);
            } else {
                // For tuples with the same value, we just need to check the tuple id is valid and
                // in the range of [0, expectedFTBlockOffsetOrder.size()).
                ASSERT_EQ((0 <= encodedFTBlockOffset) &&
                              (encodedFTBlockOffset < expectedFTBlockOffsetOrder.size()),
                    true);
            }
            keyBlockPtr += entrySize;
        }
    }

    void sortAllKeyBlocks(OrderByKeyEncoder& orderByKeyEncoder, RadixSort& radixSort) {
        for (auto& keyBlock : orderByKeyEncoder.getKeyBlocks()) {
            radixSort.sortSingleKeyBlock(*keyBlock);
        }
    }

    template<typename T>
    void singleOrderByColTest(const vector<T>& sortingData, const vector<bool>& nullMasks,
        const vector<uint64_t>& expectedBlockOffsetOrder, const DataTypeID dataTypeID,
        const bool isAsc, bool hasPayLoadCol) {
        KU_ASSERT(sortingData.size() == nullMasks.size());
        KU_ASSERT(sortingData.size() == expectedBlockOffsetOrder.size());
        auto dataChunk = make_shared<DataChunk>(hasPayLoadCol ? 2 : 1);
        dataChunk->state->selVector->selectedSize = sortingData.size();
        auto valueVector = make_shared<ValueVector>(dataTypeID, memoryManager.get());
        auto values = (T*)valueVector->values;
        for (auto i = 0u; i < dataChunk->state->selVector->selectedSize; i++) {
            if (nullMasks[i]) {
                valueVector->setNull(i, true);
            } else if constexpr (is_same<T, string>::value) {
                valueVector->addString(i, sortingData[i]);
            } else {
                values[i] = sortingData[i];
            }
        }
        dataChunk->insert(0, valueVector);
        vector<shared_ptr<ValueVector>> orderByVectors{
            valueVector}; // only contains orderBy columns
        vector<shared_ptr<ValueVector>> allVectors{
            valueVector}; // all columns including orderBy and payload columns
        vector<bool> isAscOrder{isAsc};

        unique_ptr<FactorizedTableSchema> tableSchema = make_unique<FactorizedTableSchema>();
        tableSchema->appendColumn(make_unique<ColumnSchema>(
            false /* isUnflat */, 0 /* dataChunkPos */, Types::getDataTypeSize(dataTypeID)));
        vector<StringAndUnstructuredKeyColInfo> stringAndUnstructuredKeyColInfo;

        if (hasPayLoadCol) {
            // Create a new payloadValueVector for the payload column.
            auto payloadValueVector = make_shared<ValueVector>(STRING, memoryManager.get());
            for (auto i = 0u; i < dataChunk->state->selVector->selectedSize; i++) {
                payloadValueVector->addString(i, to_string(i));
            }
            dataChunk->insert(1, payloadValueVector);
            // To test whether the orderByCol -> ftIdx works properly, we put the
            // payload column at index 0, and the orderByCol at index 1.
            allVectors.insert(allVectors.begin(), payloadValueVector);
            tableSchema->appendColumn(make_unique<ColumnSchema>(
                false /* isUnflat */, 0 /* dataChunkPos */, Types::getDataTypeSize(dataTypeID)));
            stringAndUnstructuredKeyColInfo.emplace_back(
                StringAndUnstructuredKeyColInfo(tableSchema->getColOffset(1) /* colOffsetInFT */,
                    0 /* colOffsetInEncodedKeyBlock */, isAsc,
                    is_same<T, string>::value /* isStrCol */));
        } else if constexpr (is_same<T, string>::value || is_same<T, Value>::value) {
            // If this is a string or unstructured column and has no payload column, then the
            // factorizedTable offset is just 0.
            stringAndUnstructuredKeyColInfo.emplace_back(
                StringAndUnstructuredKeyColInfo(tableSchema->getColOffset(0) /* colOffsetInFT */,
                    0 /* colOffsetInEncodedKeyBlock */, isAsc,
                    is_same<T, string>::value /* isStrCol */));
        }

        FactorizedTable factorizedTable(memoryManager.get(), move(tableSchema));
        factorizedTable.append(allVectors);

        auto orderByKeyEncoder = OrderByKeyEncoder(orderByVectors, isAscOrder, memoryManager.get(),
            factorizedTableIdx, numTuplesPerBlockInFT);
        orderByKeyEncoder.encodeKeys();

        RadixSort radixSort = RadixSort(memoryManager.get(), factorizedTable, orderByKeyEncoder,
            stringAndUnstructuredKeyColInfo);
        sortAllKeyBlocks(orderByKeyEncoder, radixSort);

        checkTupleIdxesAndFactorizedTableIdxes(orderByKeyEncoder.getKeyBlocks()[0]->getData(),
            orderByKeyEncoder.getNumBytesPerTuple(), expectedBlockOffsetOrder);
    }

    void multipleOrderByColSolveTieTest(vector<bool>& isAscOrder,
        vector<uint64_t>& expectedBlockOffsetOrder, vector<vector<string>>& stringValues) {
        vector<shared_ptr<ValueVector>> orderByVectors;
        auto mockDataChunk = make_shared<DataChunk>(stringValues.size());
        mockDataChunk->state->currIdx = 0;
        unique_ptr<FactorizedTableSchema> tableSchema = make_unique<FactorizedTableSchema>();
        vector<StringAndUnstructuredKeyColInfo> stringAndUnstructuredKeyColInfo;
        for (auto i = 0; i < stringValues.size(); i++) {
            auto stringValueVector = make_shared<ValueVector>(STRING, memoryManager.get());
            tableSchema->appendColumn(make_unique<ColumnSchema>(
                false /* isUnflat */, 0 /* dataChunkPos */, sizeof(ku_string_t)));
            stringAndUnstructuredKeyColInfo.push_back(StringAndUnstructuredKeyColInfo(
                tableSchema->getColOffset(stringAndUnstructuredKeyColInfo.size()),
                stringAndUnstructuredKeyColInfo.size() *
                    OrderByKeyEncoder::getEncodingSize(stringValueVector->dataType),
                isAscOrder[i], true /* isStrCol */));
            mockDataChunk->insert(i, stringValueVector);
            for (auto j = 0u; j < stringValues[i].size(); j++) {
                stringValueVector->addString(j, stringValues[i][j]);
            }
            orderByVectors.emplace_back(stringValueVector);
        }

        FactorizedTable factorizedTable(memoryManager.get(), move(tableSchema));

        auto orderByKeyEncoder = OrderByKeyEncoder(orderByVectors, isAscOrder, memoryManager.get(),
            factorizedTableIdx, numTuplesPerBlockInFT);
        for (auto i = 0u; i < expectedBlockOffsetOrder.size(); i++) {
            factorizedTable.append(orderByVectors);
            orderByKeyEncoder.encodeKeys();
            mockDataChunk->state->currIdx++;
        }

        auto radixSort = RadixSort(memoryManager.get(), factorizedTable, orderByKeyEncoder,
            stringAndUnstructuredKeyColInfo);
        sortAllKeyBlocks(orderByKeyEncoder, radixSort);

        checkTupleIdxesAndFactorizedTableIdxes(orderByKeyEncoder.getKeyBlocks()[0]->getData(),
            orderByKeyEncoder.getNumBytesPerTuple(), expectedBlockOffsetOrder);
    }
};

TEST_F(RadixSortTest, singleOrderByColInt64Test) {
    vector<int64_t> sortingData = {73 /* positive 1 byte number */, 0 /* NULL */,
        -132 /* negative 1 byte number */, -5242 /* negative 2 bytes number */, INT64_MAX,
        INT64_MIN, 210042 /* positive 2 bytes number */};
    vector<bool> nullMasks = {false, true, false, false, false, false, false};
    vector<uint64_t> expectedFTBlockOffsetOrder = {5, 3, 2, 0, 6, 4, 1};
    singleOrderByColTest(sortingData, nullMasks, expectedFTBlockOffsetOrder, INT64,
        true /* isAsc */, false /* hasPayLoadCol */);
}

TEST_F(RadixSortTest, singleOrderByColNoNullInt64Test) {
    vector<int64_t> sortingData = {48 /* positive 1 byte number */,
        39842 /* positive 2 bytes number */, -1 /* negative 1 byte number */,
        -819321 /* negative 2 bytes number */, INT64_MAX, INT64_MIN};
    vector<bool> nullMasks(6, false);
    vector<uint64_t> expectedFTBlockOffsetOrder = {4, 1, 0, 2, 3, 5};
    singleOrderByColTest(sortingData, nullMasks, expectedFTBlockOffsetOrder, INT64,
        false /* isAsc */, false /* hasPayLoadCol */);
}

TEST_F(RadixSortTest, singleOrderByColLargeInputInt64Test) {
    // 240 is the maximum number of tuples we can put into a memory block
    // since: 4096 / (9 + 8) = 240.
    vector<int64_t> sortingData(240);
    iota(sortingData.begin(), sortingData.end(), 0);
    reverse(sortingData.begin(), sortingData.end());
    vector<bool> nullMasks(240, false);
    vector<uint64_t> expectedFTBlockOffsetOrder(240);
    iota(expectedFTBlockOffsetOrder.begin(), expectedFTBlockOffsetOrder.end(), 0);
    reverse(expectedFTBlockOffsetOrder.begin(), expectedFTBlockOffsetOrder.end());
    singleOrderByColTest(sortingData, nullMasks, expectedFTBlockOffsetOrder, INT64,
        true /* isAsc */, false /* hasPayLoadCol */);
}

TEST_F(RadixSortTest, singleOrderByColBoolTest) {
    vector<int64_t> sortingData = {true, false, false /* NULL */};
    vector<bool> nullMasks = {false, false, true};
    vector<uint64_t> expectedFTBlockOffsetOrder = {2, 0, 1};
    singleOrderByColTest(sortingData, nullMasks, expectedFTBlockOffsetOrder, BOOL,
        false /* isAsc */, false /* hasPayLoadCol */);
}

TEST_F(RadixSortTest, singleOrderByColDateTest) {
    vector<date_t> sortingData = {
        Date::FromCString("1970-01-01", strlen("1970-01-01")) /* days=0 */,
        Date::FromCString("1970-01-02", strlen("1970-01-02")) /* positive days */,
        Date::FromCString("2003-10-12", strlen("2003-10-12")) /* large positive days */,
        Date::FromCString("1968-12-21", strlen("1968-12-21")) /* negative days */,
        date_t(0) /*NULL*/};
    vector<bool> nullMasks = {false, false, false, false, true};
    vector<uint64_t> expectedFTBlockOffsetOrder = {3, 0, 1, 2, 4};
    singleOrderByColTest(sortingData, nullMasks, expectedFTBlockOffsetOrder, DATE, true /* isAsc */,
        false /* hasPayLoadCol */);
}

TEST_F(RadixSortTest, singleOrderByColTimestampTest) {
    vector<timestamp_t> sortingData = {
        Timestamp::FromCString("1970-01-01 00:00:00", strlen("1970-01-01 00:00:00")) /* micros=0 */,
        Timestamp::FromCString(
            "1970-01-02 14:21:11", strlen("1970-01-02 14:21:11")) /* positive micros */,
        timestamp_t(0) /*NULL*/,
        Timestamp::FromCString(
            "2003-10-12 08:21:10", strlen("2003-10-12 08:21:10")) /* large positive micros */,
        Timestamp::FromCString(
            "1959-03-20 11:12:13.500", strlen("1959-03-20 11:12:13.500")) /* negative micros */
    };

    vector<bool> nullMasks = {false, false, true, false, false};
    vector<uint64_t> expectedFTBlockOffsetOrder = {2, 3, 1, 0, 4};
    singleOrderByColTest(sortingData, nullMasks, expectedFTBlockOffsetOrder, TIMESTAMP,
        false /* isAsc */, false /* hasPayLoadCol */);
}

TEST_F(RadixSortTest, singleOrderByColIntervalTest) {
    // We need to normalize days and micros in intervals.
    vector<interval_t> sortingData = {
        interval_t(0, 0, 0) /* NULL */,
        Interval::FromCString(
            "100 days 3 years 2 hours 178 minutes", strlen("100 days 3 years 2 hours 178 minutes")),
        Interval::FromCString("2 years 466 days 20 minutes",
            strlen("2 years 466 days 20 minutes")) /* =3 years 106 days 20 minutes */,
        Interval::FromCString("3 years 99 days 200 hours 100 minutes",
            strlen("3 years 99 days 100 hours 100 minutes")) /* =3 years 107 days 8 hours 100
                                                                minutes */
        ,
    };

    vector<bool> nullMasks = {true, false, false, false};
    vector<uint64_t> expectedFTBlockOffsetOrder = {0, 3, 2, 1};
    singleOrderByColTest(sortingData, nullMasks, expectedFTBlockOffsetOrder, INTERVAL,
        false /* isAsc */, false /* hasPayLoadCol */);
}

TEST_F(RadixSortTest, singleOrderByColDoubleTest) {
    vector<double> sortingData = {0.0123 /* small positive number */,
        -0.90123 /* small negative number */, 95152 /* large positive number */,
        -76123 /* large negative number */, 0, 0 /* NULL */};
    vector<bool> nullMasks = {false, false, false, false, false, true};
    vector<uint64_t> expectedFTBlockOffsetOrder = {5, 2, 0, 4, 1, 3};
    singleOrderByColTest(sortingData, nullMasks, expectedFTBlockOffsetOrder, DOUBLE,
        false /* isAsc */, false /* hasPayLoadCol */);
}

TEST_F(RadixSortTest, singleOrderByColUnstrSameDataTypeTest) {
    // SortingData contains unstructured value with the same datatype.
    vector<Value> sortingData = {Value(4.7), Value(-0.5), Value(10.52), Value(double(0)) /* NULL */,
        Value(double(0)) /* NULL */};
    vector<bool> nullMasks = {false, false, false, true, true};
    vector<uint64_t> expectedFTBlockOffsetOrder = {1, 0, 2, 4, 3};
    singleOrderByColTest(sortingData, nullMasks, expectedFTBlockOffsetOrder, UNSTRUCTURED,
        true /* isAsc */, false /* hasPayLoadCol */);
}

TEST_F(RadixSortTest, singleOrderByColUnstrNumericalValTest) {
    // SortingData contains a mixture of INT64 and double.
    vector<Value> sortingData = {
        Value(4.7), Value(-0.5), Value(int64_t(8)), Value(int64_t(0)) /* NULL */, Value(-0.045)};
    vector<bool> nullMasks = {false, false, false, true, false};
    vector<uint64_t> expectedFTBlockOffsetOrder = {1, 4, 0, 2, 3};
    singleOrderByColTest(sortingData, nullMasks, expectedFTBlockOffsetOrder, UNSTRUCTURED,
        true /* isAsc */, false /* hasPayLoadCol */);
}

TEST_F(RadixSortTest, singleOrderByColUnstrTimeValTest) {
    // SortingData contains a mixture of timestamp and date.
    vector<Value> sortingData = {
        Value(Timestamp::FromCString("2003-10-12 08:21:10", strlen("2003-10-12 08:21:10"))),
        Value(Date::FromCString("2003-10-12", strlen("2003-10-12"))), Value(int64_t(0)) /* NULL */,
        Value(Date::FromCString("2003-10-13", strlen("2003-10-13"))),
        Value(Timestamp::FromCString("2003-10-13 01:02:03", strlen("2003-10-13 01:02:03")))};
    vector<bool> nullMasks = {false, false, true, false, false};
    vector<uint64_t> expectedFTBlockOffsetOrder = {1, 0, 3, 4, 2};
    singleOrderByColTest(sortingData, nullMasks, expectedFTBlockOffsetOrder, UNSTRUCTURED,
        true /* isAsc */, false /* hasPayLoadCol */);
}

TEST_F(RadixSortTest, singleOrderByColUnstrErrorTest) {
    // SortingData contains double and timestamp, the comparison function should throw an exception.
    vector<Value> sortingData = {
        Value(Timestamp::FromCString("2003-10-12 08:21:10", strlen("2003-10-12 08:21:10"))),
        Value(4.2)};
    vector<bool> nullMasks = {false, false};
    vector<uint64_t> expectedFTBlockOffsetOrder = {1, 0};
    try {
        singleOrderByColTest(sortingData, nullMasks, expectedFTBlockOffsetOrder, UNSTRUCTURED,
            true /* isAsc */, false /* hasPayLoadCol */);
        FAIL();
    } catch (exception& e) {
    } catch (std::exception& e) { FAIL(); }
}

TEST_F(RadixSortTest, singleOrderByColStringTest) {
    // Multiple groups of string with the same prefix generates multiple groups of ties during radix
    // sort.
    vector<string> sortingData = {"abcdef", "other common prefix test1", "another common prefix2",
        "common prefix rank1", "common prefix rank3", "common prefix rank2",
        "another common prefix1", "another short string", "" /*NULL*/};
    vector<bool> nullMasks = {false, false, false, false, false, false, false, false, true};
    vector<uint64_t> expectedFTBlockOffsetOrder = {0, 6, 2, 7, 3, 5, 4, 1, 8};
    singleOrderByColTest(sortingData, nullMasks, expectedFTBlockOffsetOrder, STRING,
        true /* isAsc */, false /* hasPayLoadCol */);
}

TEST_F(RadixSortTest, singleOrderByColNoNullStringTest) {
    // Multiple groups of string with the same prefix generates multiple groups of ties during radix
    // sort.
    vector<string> sortingData = {"simple short", "other common prefix test2",
        "another common prefix2", "common prefix rank1", "common prefix rank3",
        "common prefix rank2", "other common prefix test3", "another short string"};
    vector<bool> nullMasks(8, false);
    vector<uint64_t> expectedFTBlockOffsetOrder = {0, 6, 1, 4, 5, 3, 7, 2};
    singleOrderByColTest(sortingData, nullMasks, expectedFTBlockOffsetOrder, STRING,
        false /* is desc */, false /* hasPayLoadCol */);
}

TEST_F(RadixSortTest, singleOrderByColAllTiesStringTest) {
    // All the strings are the same, so there is a tie across all tuples and the tie can't be
    // solved. The tuple ordering depends on the c++ std::sort, so we just need to check that the
    // tupleIdx is valid and is in the range of [0~19).
    vector<string> sortingData(20, "same string for all tuples");
    vector<bool> nullMasks(20, false);
    vector<uint64_t> expectedFTBlockOffsetOrder(20, -1);
    singleOrderByColTest(sortingData, nullMasks, expectedFTBlockOffsetOrder, STRING,
        true /* isAsc */, false /* hasPayLoadCol */);
}

TEST_F(RadixSortTest, singleOrderByColWithPayloadTest) {
    // The first column is a payload column and the second column is an orderBy column. The radix
    // sort needs to use the factorizedTableColIdx to correctly read the strings.
    vector<string> sortingData = {"string column with payload col test5",
        "string column with payload col test3", "string 1",
        "string column with payload col long long", "very long long long string"};
    vector<bool> nullMasks(5, false);
    vector<uint64_t> expectedFTBlockOffsetOrder = {2, 3, 1, 0, 4};
    singleOrderByColTest(sortingData, nullMasks, expectedFTBlockOffsetOrder, STRING,
        true /* isAsc */, true /* hasPayLoadCol */);
}

TEST_F(RadixSortTest, multipleOrderByColNoTieTest) {
    vector<bool> isAscOrder = {true, false, true, false, false};
    auto intFlatValueVector = make_shared<ValueVector>(INT64, memoryManager.get());
    auto doubleFlatValueVector = make_shared<ValueVector>(DOUBLE, memoryManager.get());
    auto stringFlatValueVector = make_shared<ValueVector>(STRING, memoryManager.get());
    auto timestampFlatValueVector = make_shared<ValueVector>(TIMESTAMP, memoryManager.get());
    auto dateFlatValueVector = make_shared<ValueVector>(DATE, memoryManager.get());

    auto mockDataChunk = make_shared<DataChunk>(5);
    mockDataChunk->insert(0, intFlatValueVector);
    mockDataChunk->insert(1, doubleFlatValueVector);
    mockDataChunk->insert(2, stringFlatValueVector);
    mockDataChunk->insert(3, timestampFlatValueVector);
    mockDataChunk->insert(4, dateFlatValueVector);

    auto intValues = (int64_t*)intFlatValueVector->values;
    auto doubleValues = (double*)doubleFlatValueVector->values;
    auto timestampValues = (timestamp_t*)timestampFlatValueVector->values;
    auto dateValues = (date_t*)dateFlatValueVector->values;

    intFlatValueVector->state->currIdx = 0;
    doubleFlatValueVector->state->currIdx = 0;
    stringFlatValueVector->state->currIdx = 0;
    timestampFlatValueVector->state->currIdx = 0;
    dateFlatValueVector->state->currIdx = 0;

    vector<shared_ptr<ValueVector>> orderByVectors{intFlatValueVector, doubleFlatValueVector,
        stringFlatValueVector, timestampFlatValueVector, dateFlatValueVector};
    intValues[0] = 41;
    intValues[1] = -132;
    intValues[2] = 41;
    intFlatValueVector->setNull(3, true);
    intValues[4] = 0;
    doubleValues[0] = 453.421;
    doubleValues[1] = -415.23;
    doubleValues[2] = -0.00421;
    doubleValues[3] = 0;
    doubleValues[4] = 0.0121;
    stringFlatValueVector->addString(0, "common prefix2");
    stringFlatValueVector->addString(1, "common prefix1");
    stringFlatValueVector->addString(2, "common prefix");
    stringFlatValueVector->setNull(3, true);
    stringFlatValueVector->addString(4, "short str");
    timestampValues[0] =
        Timestamp::FromCString("1970-01-01 00:00:00", strlen("1970-01-01 00:00:00"));
    timestampValues[1] =
        Timestamp::FromCString("1962-04-07 14:11:23", strlen("1962-04-07 14:11:23"));
    timestampValues[2] =
        Timestamp::FromCString("1970-01-01 01:00:00", strlen("1970-01-01 01:00:00"));
    timestampValues[3] =
        Timestamp::FromCString("1953-01-12 21:12:00", strlen("2053-01-12 21:12:00"));
    timestampFlatValueVector->setNull(4, true);
    dateValues[0] = Date::FromCString("1978-09-12", strlen("1978-09-12"));
    dateValues[1] = Date::FromCString("2035-07-04", strlen("2035-07-04"));
    dateFlatValueVector->setNull(2, true);
    dateValues[3] = Date::FromCString("1964-01-21", strlen("1964-01-21"));
    dateValues[4] = Date::FromCString("2000-11-13", strlen("2000-11-13"));

    unique_ptr<FactorizedTableSchema> tableSchema = make_unique<FactorizedTableSchema>();
    tableSchema->appendColumn(make_unique<ColumnSchema>(
        false /* isUnflat */, 0 /* dataChunkPos */, Types::getDataTypeSize(INT64)));
    tableSchema->appendColumn(make_unique<ColumnSchema>(
        false /* isUnflat */, 0 /* dataChunkPos */, Types::getDataTypeSize(DOUBLE)));
    tableSchema->appendColumn(make_unique<ColumnSchema>(
        false /* isUnflat */, 0 /* dataChunkPos */, Types::getDataTypeSize(STRING)));
    tableSchema->appendColumn(make_unique<ColumnSchema>(
        false /* isUnflat */, 0 /* dataChunkPos */, Types::getDataTypeSize(TIMESTAMP)));
    tableSchema->appendColumn(make_unique<ColumnSchema>(
        false /* isUnflat */, 0 /* dataChunkPos */, Types::getDataTypeSize(DATE)));

    FactorizedTable factorizedTable(memoryManager.get(), move(tableSchema));
    vector<StringAndUnstructuredKeyColInfo> stringAndUnstructuredKeyColInfo = {
        StringAndUnstructuredKeyColInfo(16 /* colOffsetInFT */,
            OrderByKeyEncoder::getEncodingSize(DataType(INT64)) +
                OrderByKeyEncoder::getEncodingSize(DataType(DOUBLE)),
            true /* isAscOrder */, true /* isStrCol */)};

    auto orderByKeyEncoder = OrderByKeyEncoder(
        orderByVectors, isAscOrder, memoryManager.get(), factorizedTableIdx, numTuplesPerBlockInFT);
    for (auto i = 0u; i < 5; i++) {
        orderByKeyEncoder.encodeKeys();
        factorizedTable.append(orderByVectors);
        mockDataChunk->state->currIdx++;
    }

    RadixSort radixSort = RadixSort(
        memoryManager.get(), factorizedTable, orderByKeyEncoder, stringAndUnstructuredKeyColInfo);
    sortAllKeyBlocks(orderByKeyEncoder, radixSort);

    vector<uint64_t> expectedFTBlockOffsetOrder = {1, 4, 0, 2, 3};
    checkTupleIdxesAndFactorizedTableIdxes(orderByKeyEncoder.getKeyBlocks()[0]->getData(),
        orderByKeyEncoder.getNumBytesPerTuple(), expectedFTBlockOffsetOrder);
}

TEST_F(RadixSortTest, multipleOrderByColSolvableTieTest) {
    vector<bool> isAscOrder = {false, true};
    vector<uint64_t> expectedFTBlockOffsetOrder = {
        4,
        0,
        1,
        3,
        2,
    };
    // The first column has ties, need to compare the second column to solve the tie. However there
    // are still some ties that are not solvable.
    vector<vector<string>> stringValues = {
        {"same common prefix different1", "same common prefix different",
            "same common prefix different", "same common prefix different",
            "same common prefix different1"},
        {"second same common prefix2", "second same common prefix0", "second same common prefix3",
            "second same common prefix2", "second same common prefix1"}};
    multipleOrderByColSolveTieTest(isAscOrder, expectedFTBlockOffsetOrder, stringValues);
}

TEST_F(RadixSortTest, multipleOrderByColUnSolvableTieTest) {
    vector<bool> isAscOrder = {true, true};
    vector<uint64_t> expectedFTBlockOffsetOrder = {1, 3, 2, 0, 4};
    // The first column has ties, need to compare the second column to solve the tie. However there
    // are still some ties that are not solvable.
    vector<vector<string>> stringValues = {
        {"same common prefix different1", "same common prefix different",
            "same common prefix different", "same common prefix different",
            "same common prefix different1"},
        {"second same common prefix2", "second same common prefix0", "second same common prefix3",
            "second same common prefix0", "second same common prefix2"}};
    multipleOrderByColSolveTieTest(isAscOrder, expectedFTBlockOffsetOrder, stringValues);
}
