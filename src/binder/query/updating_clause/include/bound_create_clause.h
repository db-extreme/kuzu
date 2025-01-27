#pragma once

#include "bound_updating_clause.h"

#include "src/binder/expression/include/rel_expression.h"

namespace kuzu {
namespace binder {

class BoundCreateNodeOrRel {
public:
    BoundCreateNodeOrRel(vector<expression_pair> setItems) : setItems{std::move(setItems)} {}
    virtual ~BoundCreateNodeOrRel() = default;

    inline vector<expression_pair> getSetItems() const { return setItems; }

protected:
    vector<expression_pair> setItems;
};

class BoundCreateNode : public BoundCreateNodeOrRel {
public:
    BoundCreateNode(shared_ptr<NodeExpression> node, shared_ptr<Expression> primaryKeyExpression,
        vector<expression_pair> setItems)
        : BoundCreateNodeOrRel{std::move(setItems)}, node{std::move(node)},
          primaryKeyExpression{std::move(primaryKeyExpression)} {}

    inline shared_ptr<NodeExpression> getNode() const { return node; }
    inline shared_ptr<Expression> getPrimaryKeyExpression() const { return primaryKeyExpression; }

    inline unique_ptr<BoundCreateNode> copy() const {
        return make_unique<BoundCreateNode>(node, primaryKeyExpression, setItems);
    }

private:
    shared_ptr<NodeExpression> node;
    shared_ptr<Expression> primaryKeyExpression;
};

class BoundCreateRel : public BoundCreateNodeOrRel {
public:
    BoundCreateRel(shared_ptr<RelExpression> rel, vector<expression_pair> setItems)
        : BoundCreateNodeOrRel{std::move(setItems)}, rel{std::move(rel)} {}

    inline shared_ptr<RelExpression> getRel() const { return rel; }

    inline unique_ptr<BoundCreateRel> copy() const {
        return make_unique<BoundCreateRel>(rel, setItems);
    }

private:
    shared_ptr<RelExpression> rel;
};

class BoundCreateClause : public BoundUpdatingClause {
public:
    BoundCreateClause(vector<unique_ptr<BoundCreateNode>> createNodes,
        vector<unique_ptr<BoundCreateRel>> createRels)
        : BoundUpdatingClause{ClauseType::CREATE}, createNodes{std::move(createNodes)},
          createRels{std::move(createRels)} {};
    ~BoundCreateClause() override = default;

    inline bool hasCreateNode() const { return !createNodes.empty(); }
    inline uint32_t getNumCreateNodes() const { return createNodes.size(); }
    inline BoundCreateNode* getCreateNode(uint32_t idx) const { return createNodes[idx].get(); }

    inline bool hasCreateRel() const { return !createRels.empty(); }
    inline uint32_t getNumCreateRels() const { return createRels.size(); }
    inline BoundCreateRel* getCreateRel(uint32_t idx) const { return createRels[idx].get(); }

    vector<expression_pair> getAllSetItems() const;
    expression_vector getPropertiesToRead() const override;

    unique_ptr<BoundUpdatingClause> copy() override;

private:
    vector<unique_ptr<BoundCreateNode>> createNodes;
    vector<unique_ptr<BoundCreateRel>> createRels;
};

} // namespace binder
} // namespace kuzu
