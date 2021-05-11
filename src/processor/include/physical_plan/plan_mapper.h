#pragma once

#include "src/expression/include/logical/logical_expression.h"
#include "src/planner/include/logical_plan/logical_plan.h"
#include "src/processor/include/physical_plan/operator/physical_operator_info.h"
#include "src/processor/include/physical_plan/physical_plan.h"

using namespace graphflow::planner;
using namespace graphflow::expression;

namespace graphflow {
namespace processor {

class PlanMapper {

public:
    static unique_ptr<PhysicalPlan> mapToPhysical(
        unique_ptr<LogicalPlan> logicalPlan, const Graph& graph);
};

} // namespace processor
} // namespace graphflow
