#include "src/processor/include/physical_plan/operator/intersect/intersect.h"

#include <algorithm>

namespace graphflow {
namespace processor {

void Intersect::initResultSet(const shared_ptr<ResultSet>& resultSet) {
    PhysicalOperator::initResultSet(resultSet);
    leftDataChunk = this->resultSet->dataChunks[leftDataPos.dataChunkPos];
    leftValueVector = leftDataChunk->valueVectors[leftDataPos.valueVectorPos];
    rightDataChunk = this->resultSet->dataChunks[rightDataPos.dataChunkPos];
    rightValueVector = rightDataChunk->valueVectors[rightDataPos.valueVectorPos];
}

static void sortSelectedPos(const shared_ptr<ValueVector>& nodeIDVector) {
    auto selectedPos = nodeIDVector->state->selectedPositionsBuffer.get();
    auto size = nodeIDVector->state->selectedSize;
    sort(selectedPos, selectedPos + size, [nodeIDVector](sel_t left, sel_t right) {
        return nodeIDVector->readNodeOffset(left) < nodeIDVector->readNodeOffset(right);
    });
}

void Intersect::getNextTuples() {
    auto numSelectedValues = 0u;
    do {
        restoreDataChunkSelectorState(leftDataChunk);
        if (rightDataChunk->state->selectedSize == rightDataChunk->state->currIdx + 1ul) {
            rightDataChunk->state->currIdx = -1;
            prevOperator->getNextTuples();
            sortSelectedPos(leftValueVector);
            sortSelectedPos(rightValueVector);
            leftIdx = 0;
            if (leftDataChunk->state->selectedSize == 0) {
                break;
            }
        }
        // Flatten right dataChunk
        rightDataChunk->state->currIdx++;
        saveDataChunkSelectorState(leftDataChunk);
        auto rightPos = rightDataChunk->state->getPositionOfCurrIdx();
        auto rightNodeOffset = rightValueVector->readNodeOffset(rightPos);
        numSelectedValues = 0u;
        while (leftIdx < leftDataChunk->state->selectedSize) {
            auto leftPos = leftDataChunk->state->selectedPositions[leftIdx];
            auto leftNodeOffset = leftValueVector->readNodeOffset(leftPos);
            if (leftNodeOffset > rightNodeOffset) {
                break;
            } else if (leftNodeOffset == rightNodeOffset) {
                leftDataChunk->state->selectedPositionsBuffer[numSelectedValues++] = leftPos;
            }
            leftIdx++;
        }
        leftIdx -= numSelectedValues; // restore leftIdx to the first match
        leftDataChunk->state->selectedSize = numSelectedValues;
        if (leftDataChunk->state->isUnfiltered()) {
            leftDataChunk->state->resetSelectorToValuePosBuffer();
        }
    } while (numSelectedValues == 0u);
}

} // namespace processor
} // namespace graphflow