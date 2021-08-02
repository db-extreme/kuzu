#include "src/processor/include/physical_plan/operator/multiplicity_reducer/multiplicity_reducer.h"

namespace graphflow {
namespace processor {

MultiplicityReducer::MultiplicityReducer(
    unique_ptr<PhysicalOperator> prevOperator, ExecutionContext& context, uint32_t id)
    : PhysicalOperator{move(prevOperator), MULTIPLICITY_REDUCER, context, id},
      prevMultiplicity{1}, numRepeat{0} {
    resultSet = this->prevOperator->getResultSet();
}

void MultiplicityReducer::getNextTuples() {
    metrics->executionTime.start();
    if (numRepeat == 0) {
        restoreMultiplicity();
        prevOperator->getNextTuples();
        if (resultSet->getNumTuples() == 0) {
            return;
        }
        saveMultiplicity();
        resultSet->multiplicity = 1;
    }
    numRepeat++;
    if (numRepeat == prevMultiplicity) {
        numRepeat = 0;
    }
    metrics->executionTime.stop();
}

} // namespace processor
} // namespace graphflow
