#include "src/storage/include/storage_structure/column.h"

#include <iostream>

namespace graphflow {
namespace storage {

void Column::readValues(
    const shared_ptr<ValueVector>& nodeIDVector, const shared_ptr<ValueVector>& valueVector) {
    assert(nodeIDVector->dataType == NODE);
    if (nodeIDVector->state->isFlat()) {
        auto pos = nodeIDVector->state->getPositionOfCurrIdx();
        readForSingleNodeIDPosition(pos, nodeIDVector, valueVector);
    } else {
        for (auto i = 0ul; i < nodeIDVector->state->selectedSize; i++) {
            auto pos = nodeIDVector->state->selectedPositions[i];
            readForSingleNodeIDPosition(pos, nodeIDVector, valueVector);
        }
    }
}

Literal Column::readValue(node_offset_t offset) {
    auto cursor = PageUtils::getPageElementCursorForOffset(offset, numElementsPerPage);
    Literal retVal;
    auto frame = bufferManager.pin(fileHandle, cursor.idx);
    memcpy(&retVal.val, frame + mapElementPosToByteOffset(cursor.pos), elementSize);
    bufferManager.unpin(fileHandle, cursor.idx);
    return retVal;
}

void Column::readForSingleNodeIDPosition(uint32_t pos, const shared_ptr<ValueVector>& nodeIDVector,
    const shared_ptr<ValueVector>& resultVector) {
    if (nodeIDVector->isNull(pos)) {
        resultVector->setNull(pos, true);
        return;
    }
    auto pageCursor = PageUtils::getPageElementCursorForOffset(
        nodeIDVector->readNodeOffset(pos), numElementsPerPage);
    auto frame = bufferManager.pin(fileHandle, pageCursor.idx);
    memcpy(resultVector->values + pos * elementSize,
        frame + mapElementPosToByteOffset(pageCursor.pos), elementSize);
    setNULLBitsForAPos(resultVector, frame, pageCursor.pos, pos);
    bufferManager.unpin(fileHandle, pageCursor.idx);
}

void StringPropertyColumn::readValues(
    const shared_ptr<ValueVector>& nodeIDVector, const shared_ptr<ValueVector>& valueVector) {
    Column::readValues(nodeIDVector, valueVector);
    stringOverflowPages.readStringsToVector(*valueVector);
}

Literal StringPropertyColumn::readValue(node_offset_t offset) {
    auto cursor = PageUtils::getPageElementCursorForOffset(offset, numElementsPerPage);
    gf_string_t gfString;
    auto frame = bufferManager.pin(fileHandle, cursor.idx);
    memcpy(&gfString, frame + mapElementPosToByteOffset(cursor.pos), sizeof(gf_string_t));
    bufferManager.unpin(fileHandle, cursor.idx);
    Literal retVal;
    retVal.strVal = stringOverflowPages.readString(gfString);
    return retVal;
}

void AdjColumn::readForSingleNodeIDPosition(uint32_t pos,
    const shared_ptr<ValueVector>& nodeIDVector, const shared_ptr<ValueVector>& resultVector) {
    if (nodeIDVector->isNull(pos)) {
        resultVector->setNull(pos, true);
        return;
    }
    auto pageCursor = PageUtils::getPageElementCursorForOffset(
        nodeIDVector->readNodeOffset(pos), numElementsPerPage);
    readNodeIDsFromAPage(resultVector, pos, pageCursor.idx, pageCursor.pos, 1 /* numValuesToCopy */,
        nodeIDCompressionScheme, false /*isAdjLists*/);
}

} // namespace storage
} // namespace graphflow