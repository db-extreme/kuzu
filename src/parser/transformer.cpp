#include "src/parser/include/transformer.h"

#include "expression/include/parsed_function_expression.h"
#include "expression/include/parsed_literal_expression.h"
#include "expression/include/parsed_property_expression.h"
#include "expression/include/parsed_subquery_expression.h"
#include "expression/include/parsed_variable_expression.h"

#include "src/common/include/utils.h"

namespace graphflow {
namespace parser {

unique_ptr<RegularQuery> Transformer::transform() {
    auto regularQuery = transformQuery(*root.oC_Statement()->oC_Query());
    if (root.oC_AnyCypherOption()) {
        auto cypherOption = root.oC_AnyCypherOption();
        regularQuery->setEnableExplain(cypherOption->oC_Explain() != nullptr);
        regularQuery->setEnableProfile(cypherOption->oC_Profile() != nullptr);
    }
    return regularQuery;
}

unique_ptr<RegularQuery> Transformer::transformQuery(CypherParser::OC_QueryContext& ctx) {
    return transformRegularQuery(*ctx.oC_RegularQuery());
}

unique_ptr<RegularQuery> Transformer::transformRegularQuery(
    CypherParser::OC_RegularQueryContext& ctx) {
    auto regularQuery = make_unique<RegularQuery>(transformSingleQuery(*ctx.oC_SingleQuery()));
    for (auto unionClause : ctx.oC_Union()) {
        regularQuery->addSingleQuery(
            transformSingleQuery(*unionClause->oC_SingleQuery()), unionClause->ALL());
    }
    return regularQuery;
}

unique_ptr<SingleQuery> Transformer::transformSingleQuery(
    CypherParser::OC_SingleQueryContext& ctx) {
    auto singleQuery =
        ctx.oC_MultiPartQuery() ?
            transformSinglePartQuery(*ctx.oC_MultiPartQuery()->oC_SinglePartQuery()) :
            transformSinglePartQuery(*ctx.oC_SinglePartQuery());
    if (ctx.oC_MultiPartQuery()) {
        for (auto queryPart : ctx.oC_MultiPartQuery()->gF_QueryPart()) {
            singleQuery->addQueryPart(transformQueryPart(*queryPart));
        }
    }
    return singleQuery;
}

unique_ptr<SingleQuery> Transformer::transformSinglePartQuery(
    CypherParser::OC_SinglePartQueryContext& ctx) {
    auto singleQuery = make_unique<SingleQuery>(transformReturn(*ctx.oC_Return()));
    for (auto& readingClause : ctx.oC_ReadingClause()) {
        singleQuery->addMatchClause(transformReadingClause(*readingClause));
    }
    return singleQuery;
}

unique_ptr<QueryPart> Transformer::transformQueryPart(CypherParser::GF_QueryPartContext& ctx) {
    auto queryPart = make_unique<QueryPart>(transformWith(*ctx.oC_With()));
    for (auto& readingClause : ctx.oC_ReadingClause()) {
        queryPart->addMatchClause(transformReadingClause(*readingClause));
    }
    return queryPart;
}

unique_ptr<MatchClause> Transformer::transformReadingClause(
    CypherParser::OC_ReadingClauseContext& ctx) {
    return transformMatch(*ctx.oC_Match());
}

unique_ptr<MatchClause> Transformer::transformMatch(CypherParser::OC_MatchContext& ctx) {
    auto matchClause =
        make_unique<MatchClause>(transformPattern(*ctx.oC_Pattern()), ctx.OPTIONAL());
    if (ctx.oC_Where()) {
        matchClause->setWhereClause(transformWhere(*ctx.oC_Where()));
    }
    return matchClause;
}

unique_ptr<WithClause> Transformer::transformWith(CypherParser::OC_WithContext& ctx) {
    auto withClause = make_unique<WithClause>(transformProjectionBody(*ctx.oC_ProjectionBody()));
    if (ctx.oC_Where()) {
        withClause->setWhereExpression(transformWhere(*ctx.oC_Where()));
    }
    return withClause;
}

unique_ptr<ReturnClause> Transformer::transformReturn(CypherParser::OC_ReturnContext& ctx) {
    return make_unique<ReturnClause>(transformProjectionBody(*ctx.oC_ProjectionBody()));
}

unique_ptr<ProjectionBody> Transformer::transformProjectionBody(
    CypherParser::OC_ProjectionBodyContext& ctx) {
    auto projectionBody = make_unique<ProjectionBody>(nullptr != ctx.DISTINCT(),
        nullptr != ctx.oC_ProjectionItems()->STAR(),
        transformProjectionItems(*ctx.oC_ProjectionItems()));
    if (ctx.oC_Order()) {
        vector<unique_ptr<ParsedExpression>> orderByExpressions;
        vector<bool> isAscOrders;
        for (auto& sortItem : ctx.oC_Order()->oC_SortItem()) {
            orderByExpressions.push_back(transformExpression(*sortItem->oC_Expression()));
            isAscOrders.push_back(!(sortItem->DESC() || sortItem->DESCENDING()));
        }
        projectionBody->setOrderByExpressions(move(orderByExpressions), move(isAscOrders));
    }
    if (ctx.oC_Skip()) {
        projectionBody->setSkipExpression(transformExpression(*ctx.oC_Skip()->oC_Expression()));
    }
    if (ctx.oC_Limit()) {
        projectionBody->setLimitExpression(transformExpression(*ctx.oC_Limit()->oC_Expression()));
    }
    return projectionBody;
}

vector<unique_ptr<ParsedExpression>> Transformer::transformProjectionItems(
    CypherParser::OC_ProjectionItemsContext& ctx) {
    vector<unique_ptr<ParsedExpression>> projectionExpressions;
    for (auto& projectionItem : ctx.oC_ProjectionItem()) {
        projectionExpressions.push_back(transformProjectionItem(*projectionItem));
    }
    return projectionExpressions;
}

unique_ptr<ParsedExpression> Transformer::transformProjectionItem(
    CypherParser::OC_ProjectionItemContext& ctx) {
    auto expression = transformExpression(*ctx.oC_Expression());
    if (ctx.AS()) {
        expression->setAlias(transformVariable(*ctx.oC_Variable()));
    }
    return expression;
}

unique_ptr<ParsedExpression> Transformer::transformWhere(CypherParser::OC_WhereContext& ctx) {
    return transformExpression(*ctx.oC_Expression());
}

vector<unique_ptr<PatternElement>> Transformer::transformPattern(
    CypherParser::OC_PatternContext& ctx) {
    vector<unique_ptr<PatternElement>> pattern;
    for (auto& patternPart : ctx.oC_PatternPart()) {
        pattern.push_back(transformPatternPart(*patternPart));
    }
    return pattern;
}

unique_ptr<PatternElement> Transformer::transformPatternPart(
    CypherParser::OC_PatternPartContext& ctx) {
    return transformAnonymousPatternPart(*ctx.oC_AnonymousPatternPart());
}

unique_ptr<PatternElement> Transformer::transformAnonymousPatternPart(
    CypherParser::OC_AnonymousPatternPartContext& ctx) {
    return transformPatternElement(*ctx.oC_PatternElement());
}

unique_ptr<PatternElement> Transformer::transformPatternElement(
    CypherParser::OC_PatternElementContext& ctx) {
    if (ctx.oC_PatternElement()) { // parenthesized pattern element
        return transformPatternElement(*ctx.oC_PatternElement());
    }
    auto patternElement = make_unique<PatternElement>(transformNodePattern(*ctx.oC_NodePattern()));
    if (!ctx.oC_PatternElementChain().empty()) {
        for (auto& patternElementChain : ctx.oC_PatternElementChain()) {
            patternElement->addPatternElementChain(
                transformPatternElementChain(*patternElementChain));
        }
    }
    return patternElement;
}

unique_ptr<NodePattern> Transformer::transformNodePattern(
    CypherParser::OC_NodePatternContext& ctx) {
    return make_unique<NodePattern>(
        ctx.oC_Variable() ? transformVariable(*ctx.oC_Variable()) : string(),
        ctx.oC_NodeLabel() ? transformNodeLabel(*ctx.oC_NodeLabel()) : string());
}

unique_ptr<PatternElementChain> Transformer::transformPatternElementChain(
    CypherParser::OC_PatternElementChainContext& ctx) {
    return make_unique<PatternElementChain>(
        transformRelationshipPattern(*ctx.oC_RelationshipPattern()),
        transformNodePattern(*ctx.oC_NodePattern()));
}

unique_ptr<RelPattern> Transformer::transformRelationshipPattern(
    CypherParser::OC_RelationshipPatternContext& ctx) {
    auto relDetail = ctx.oC_RelationshipDetail();
    string lowerBound = "1";
    string upperBound = "1";
    if (relDetail->oC_RangeLiteral()) {
        lowerBound = relDetail->oC_RangeLiteral()->oC_IntegerLiteral()[0]->getText();
        upperBound = relDetail->oC_RangeLiteral()->oC_IntegerLiteral()[1]->getText();
    }
    return make_unique<RelPattern>(
        relDetail->oC_Variable() ? transformVariable(*relDetail->oC_Variable()) : string(),
        relDetail->oC_RelTypeName() ? transformRelTypeName(*relDetail->oC_RelTypeName()) : string(),
        lowerBound, upperBound,
        ctx.oC_LeftArrowHead() ? ArrowDirection::LEFT : ArrowDirection::RIGHT);
}

string Transformer::transformNodeLabel(CypherParser::OC_NodeLabelContext& ctx) {
    return transformLabelName(*ctx.oC_LabelName());
}

string Transformer::transformLabelName(CypherParser::OC_LabelNameContext& ctx) {
    return transformSchemaName(*ctx.oC_SchemaName());
}

string Transformer::transformRelTypeName(CypherParser::OC_RelTypeNameContext& ctx) {
    return transformSchemaName(*ctx.oC_SchemaName());
}

unique_ptr<ParsedExpression> Transformer::transformExpression(
    CypherParser::OC_ExpressionContext& ctx) {
    return transformOrExpression(*ctx.oC_OrExpression());
}

unique_ptr<ParsedExpression> Transformer::transformOrExpression(
    CypherParser::OC_OrExpressionContext& ctx) {
    unique_ptr<ParsedExpression> expression;
    for (auto& xorExpression : ctx.oC_XorExpression()) {
        auto next = transformXorExpression(*xorExpression);
        if (!expression) {
            expression = move(next);
        } else {
            auto rawName = expression->getRawName() + " OR " + next->getRawName();
            expression = make_unique<ParsedExpression>(OR, move(expression), move(next), rawName);
        }
    }
    return expression;
}

unique_ptr<ParsedExpression> Transformer::transformXorExpression(
    CypherParser::OC_XorExpressionContext& ctx) {
    unique_ptr<ParsedExpression> expression;
    for (auto& andExpression : ctx.oC_AndExpression()) {
        auto next = transformAndExpression(*andExpression);
        if (!expression) {
            expression = move(next);
        } else {
            auto rawName = expression->getRawName() + " XOR " + next->getRawName();
            expression = make_unique<ParsedExpression>(XOR, move(expression), move(next), rawName);
        }
    }
    return expression;
}

unique_ptr<ParsedExpression> Transformer::transformAndExpression(
    CypherParser::OC_AndExpressionContext& ctx) {
    unique_ptr<ParsedExpression> expression;
    for (auto& notExpression : ctx.oC_NotExpression()) {
        auto next = transformNotExpression(*notExpression);
        if (!expression) {
            expression = move(next);
        } else {
            auto rawName = expression->getRawName() + " AND " + next->getRawName();
            expression = make_unique<ParsedExpression>(AND, move(expression), move(next), rawName);
        }
    }
    return expression;
}

unique_ptr<ParsedExpression> Transformer::transformNotExpression(
    CypherParser::OC_NotExpressionContext& ctx) {
    if (ctx.NOT()) {
        return make_unique<ParsedExpression>(
            NOT, transformComparisonExpression(*ctx.oC_ComparisonExpression()), ctx.getText());
    }
    return transformComparisonExpression(*ctx.oC_ComparisonExpression());
}

unique_ptr<ParsedExpression> Transformer::transformComparisonExpression(
    CypherParser::OC_ComparisonExpressionContext& ctx) {
    if (1 == ctx.oC_AddOrSubtractExpression().size()) {
        return transformAddOrSubtractExpression(*ctx.oC_AddOrSubtractExpression(0));
    }
    unique_ptr<ParsedExpression> expression;
    // Antlr parser throws error for conjunctive comparison.
    // Transformer should only handle the case of single comparison operator.
    assert(ctx.gF_ComparisonOperator().size() == 1);
    auto left = transformAddOrSubtractExpression(*ctx.oC_AddOrSubtractExpression(0));
    auto right = transformAddOrSubtractExpression(*ctx.oC_AddOrSubtractExpression(1));
    auto comparisonOperator = ctx.gF_ComparisonOperator()[0]->getText();
    if (comparisonOperator == "=") {
        expression = make_unique<ParsedExpression>(EQUALS, move(left), move(right), ctx.getText());
    } else if (comparisonOperator == "<>") {
        expression =
            make_unique<ParsedExpression>(NOT_EQUALS, move(left), move(right), ctx.getText());
    } else if (comparisonOperator == ">") {
        expression =
            make_unique<ParsedExpression>(GREATER_THAN, move(left), move(right), ctx.getText());
    } else if (comparisonOperator == ">=") {
        expression = make_unique<ParsedExpression>(
            GREATER_THAN_EQUALS, move(left), move(right), ctx.getText());
    } else if (comparisonOperator == "<") {
        expression =
            make_unique<ParsedExpression>(LESS_THAN, move(left), move(right), ctx.getText());
    } else if (comparisonOperator == "<=") {
        expression =
            make_unique<ParsedExpression>(LESS_THAN_EQUALS, move(left), move(right), ctx.getText());
    } else {
        throw invalid_argument("Unable to parse ComparisonExpressionContext.");
    }
    return expression;
}

unique_ptr<ParsedExpression> Transformer::transformAddOrSubtractExpression(
    CypherParser::OC_AddOrSubtractExpressionContext& ctx) {
    unique_ptr<ParsedExpression> expression;
    for (auto i = 0ul; i < ctx.oC_MultiplyDivideModuloExpression().size(); ++i) {
        auto next =
            transformMultiplyDivideModuloExpression(*ctx.oC_MultiplyDivideModuloExpression(i));
        if (!expression) {
            expression = move(next);
        } else {
            auto arithmeticOperator = ctx.gF_AddOrSubtractOperator(i - 1)->getText();
            auto rawName =
                expression->getRawName() + " " + arithmeticOperator + " " + next->getRawName();
            if (arithmeticOperator == "+") {
                expression =
                    make_unique<ParsedExpression>(ADD, move(expression), move(next), rawName);
            } else if (arithmeticOperator == "-") {
                expression =
                    make_unique<ParsedExpression>(SUBTRACT, move(expression), move(next), rawName);
            } else {
                throw invalid_argument("Unable to parse AddOrSubtractExpressionContext");
            }
        }
    }
    return expression;
}

unique_ptr<ParsedExpression> Transformer::transformMultiplyDivideModuloExpression(
    CypherParser::OC_MultiplyDivideModuloExpressionContext& ctx) {
    unique_ptr<ParsedExpression> expression;
    for (auto i = 0ul; i < ctx.oC_PowerOfExpression().size(); i++) {
        auto next = transformPowerOfExpression(*ctx.oC_PowerOfExpression(i));
        if (!expression) {
            expression = move(next);
        } else {
            auto arithmeticOperator = ctx.gF_MultiplyDivideModuloOperator(i - 1)->getText();
            auto rawName =
                expression->getRawName() + " " + arithmeticOperator + " " + next->getRawName();
            if (arithmeticOperator == "*") {
                expression =
                    make_unique<ParsedExpression>(MULTIPLY, move(expression), move(next), rawName);
            } else if (arithmeticOperator == "/") {
                expression =
                    make_unique<ParsedExpression>(DIVIDE, move(expression), move(next), rawName);
            } else if (arithmeticOperator == "%") {
                expression =
                    make_unique<ParsedExpression>(MODULO, move(expression), move(next), rawName);
            } else {
                throw invalid_argument("Unable to parse MultiplyDivideModuloExpressionContext.");
            }
        }
    }
    return expression;
}

unique_ptr<ParsedExpression> Transformer::transformPowerOfExpression(
    CypherParser::OC_PowerOfExpressionContext& ctx) {
    unique_ptr<ParsedExpression> expression;
    for (auto& unaryAddOrSubtractExpression : ctx.oC_UnaryAddOrSubtractExpression()) {
        auto next = transformUnaryAddOrSubtractExpression(*unaryAddOrSubtractExpression);
        if (!expression) {
            expression = move(next);
        } else {
            auto rawName = expression->getRawName() + " ^ " + next->getRawName();
            expression =
                make_unique<ParsedExpression>(POWER, move(expression), move(next), rawName);
        }
    }
    return expression;
}

unique_ptr<ParsedExpression> Transformer::transformUnaryAddOrSubtractExpression(
    CypherParser::OC_UnaryAddOrSubtractExpressionContext& ctx) {
    if (ctx.MINUS()) {
        return make_unique<ParsedExpression>(NEGATE,
            transformStringListNullOperatorExpression(*ctx.oC_StringListNullOperatorExpression()),
            ctx.getText());
    }
    return transformStringListNullOperatorExpression(*ctx.oC_StringListNullOperatorExpression());
}

unique_ptr<ParsedExpression> Transformer::transformStringListNullOperatorExpression(
    CypherParser::OC_StringListNullOperatorExpressionContext& ctx) {
    auto propertyExpression =
        transformPropertyOrLabelsExpression(*ctx.oC_PropertyOrLabelsExpression());
    if (ctx.oC_NullOperatorExpression()) {
        return transformNullOperatorExpression(
            *ctx.oC_NullOperatorExpression(), move(propertyExpression));
    }
    if (ctx.oC_ListOperatorExpression()) {
        return transformListOperatorExpression(
            *ctx.oC_ListOperatorExpression(), move(propertyExpression));
    }
    if (ctx.oC_StringOperatorExpression()) {
        return transformStringOperatorExpression(
            *ctx.oC_StringOperatorExpression(), move(propertyExpression));
    }
    return propertyExpression;
}

unique_ptr<ParsedExpression> Transformer::transformStringOperatorExpression(
    CypherParser::OC_StringOperatorExpressionContext& ctx,
    unique_ptr<ParsedExpression> propertyExpression) {
    unique_ptr<ParsedExpression> expression;
    auto rawExpression = propertyExpression->getRawName() + " " + ctx.getText();
    auto right = transformPropertyOrLabelsExpression(*ctx.oC_PropertyOrLabelsExpression());
    if (ctx.STARTS()) {
        expression = make_unique<ParsedExpression>(
            STARTS_WITH, move(propertyExpression), move(right), rawExpression);
    } else if (ctx.ENDS()) {
        expression = make_unique<ParsedExpression>(
            ENDS_WITH, move(propertyExpression), move(right), rawExpression);
    } else if (ctx.CONTAINS()) {
        expression = make_unique<ParsedExpression>(
            CONTAINS, move(propertyExpression), move(right), rawExpression);
    } else {
        throw invalid_argument("Unable to parse StringOperatorExpressionContext.");
    }
    return expression;
}

unique_ptr<ParsedExpression> Transformer::transformListOperatorExpression(
    CypherParser::OC_ListOperatorExpressionContext& ctx,
    unique_ptr<ParsedExpression> propertyExpression) {
    auto rawExpression = propertyExpression->getRawName() + " " + ctx.getText();
    auto listExtract =
        make_unique<ParsedFunctionExpression>(LIST_EXTRACT_FUNC_NAME, move(rawExpression));
    listExtract->addChild(transformExpression(*ctx.oC_Expression()));
    return listExtract;
}

unique_ptr<ParsedExpression> Transformer::transformNullOperatorExpression(
    CypherParser::OC_NullOperatorExpressionContext& ctx,
    unique_ptr<ParsedExpression> propertyExpression) {
    auto rawExpression = propertyExpression->getRawName() + " " + ctx.getText();
    if (ctx.IS() && ctx.NULL_()) {
        return ctx.NOT() ?
                   make_unique<ParsedExpression>(
                       IS_NOT_NULL, move(propertyExpression), rawExpression) :
                   make_unique<ParsedExpression>(IS_NULL, move(propertyExpression), rawExpression);
    } else {
        throw invalid_argument("Unable to parse NullOperatorExpressionContext.");
    }
}

unique_ptr<ParsedExpression> Transformer::transformPropertyOrLabelsExpression(
    CypherParser::OC_PropertyOrLabelsExpressionContext& ctx) {
    if (ctx.oC_PropertyLookup()) {
        return make_unique<ParsedPropertyExpression>(PROPERTY,
            transformPropertyLookup(*ctx.oC_PropertyLookup()), transformAtom(*ctx.oC_Atom()),
            ctx.getText());
    }
    return transformAtom(*ctx.oC_Atom());
}

unique_ptr<ParsedExpression> Transformer::transformAtom(CypherParser::OC_AtomContext& ctx) {
    if (ctx.oC_Literal()) {
        return transformLiteral(*ctx.oC_Literal());
    } else if (ctx.oC_ParenthesizedExpression()) {
        return transformParenthesizedExpression(*ctx.oC_ParenthesizedExpression());
    } else if (ctx.oC_FunctionInvocation()) {
        return transformFunctionInvocation(*ctx.oC_FunctionInvocation());
    } else if (ctx.oC_ExistentialSubquery()) {
        return transformExistentialSubquery(*ctx.oC_ExistentialSubquery());
    } else if (ctx.oC_Variable()) {
        return make_unique<ParsedVariableExpression>(
            transformVariable(*ctx.oC_Variable()), ctx.getText());
    } else {
        throw invalid_argument("Unable to parse AtomContext.");
    }
}

unique_ptr<ParsedExpression> Transformer::transformLiteral(CypherParser::OC_LiteralContext& ctx) {
    if (ctx.oC_NumberLiteral()) {
        return transformNumberLiteral(*ctx.oC_NumberLiteral());
    } else if (ctx.oC_BooleanLiteral()) {
        return transformBooleanLiteral(*ctx.oC_BooleanLiteral());
    } else if (ctx.StringLiteral()) {
        return make_unique<ParsedLiteralExpression>(
            LITERAL_STRING, ctx.StringLiteral()->getText(), ctx.getText());
    } else if (ctx.NULL_()) {
        return make_unique<ParsedLiteralExpression>(LITERAL_NULL, string(), ctx.getText());
    } else if (ctx.oC_ListLiteral()) {
        return transformListLiteral(*ctx.oC_ListLiteral());
    } else {
        throw invalid_argument("Unable to parse LiteralContext.");
    }
}

unique_ptr<ParsedExpression> Transformer::transformBooleanLiteral(
    CypherParser::OC_BooleanLiteralContext& ctx) {
    if (ctx.TRUE()) {
        return make_unique<ParsedLiteralExpression>(LITERAL_BOOLEAN, "true", ctx.getText());
    } else if (ctx.FALSE()) {
        return make_unique<ParsedLiteralExpression>(LITERAL_BOOLEAN, "false", ctx.getText());
    } else {
        throw invalid_argument("Unable to parse BooleanLiteralContext.");
    }
}

unique_ptr<ParsedExpression> Transformer::transformListLiteral(
    CypherParser::OC_ListLiteralContext& ctx) {
    auto listCreation =
        make_unique<ParsedFunctionExpression>(LIST_CREATION_FUNC_NAME, ctx.getText());
    for (auto& childExpr : ctx.oC_Expression()) {
        listCreation->addChild(transformExpression(*childExpr));
    }
    return listCreation;
}

unique_ptr<ParsedExpression> Transformer::transformParenthesizedExpression(
    CypherParser::OC_ParenthesizedExpressionContext& ctx) {
    return transformExpression(*ctx.oC_Expression());
}

unique_ptr<ParsedExpression> Transformer::transformFunctionInvocation(
    CypherParser::OC_FunctionInvocationContext& ctx) {
    auto functionName = transformFunctionName(*ctx.oC_FunctionName());
    if (ctx.STAR()) {
        StringUtils::toUpper(functionName);
        assert(functionName == "COUNT");
        return make_unique<ParsedFunctionExpression>(COUNT_STAR_FUNC_NAME, ctx.getText());
    }
    auto expression = make_unique<ParsedFunctionExpression>(
        functionName, ctx.getText(), ctx.DISTINCT() != nullptr);
    for (auto& childExpr : ctx.oC_Expression()) {
        expression->addChild(transformExpression(*childExpr));
    }
    return expression;
}

string Transformer::transformFunctionName(CypherParser::OC_FunctionNameContext& ctx) {
    return transformSymbolicName(*ctx.oC_SymbolicName());
}

unique_ptr<ParsedExpression> Transformer::transformExistentialSubquery(
    CypherParser::OC_ExistentialSubqueryContext& ctx) {
    return make_unique<ParsedSubqueryExpression>(
        EXISTENTIAL_SUBQUERY, transformSingleQuery(*ctx.oC_SingleQuery()), ctx.getText());
}

string Transformer::transformPropertyLookup(CypherParser::OC_PropertyLookupContext& ctx) {
    return transformPropertyKeyName(*ctx.oC_PropertyKeyName());
}

string Transformer::transformVariable(CypherParser::OC_VariableContext& ctx) {
    return transformSymbolicName(*ctx.oC_SymbolicName());
}

unique_ptr<ParsedExpression> Transformer::transformNumberLiteral(
    CypherParser::OC_NumberLiteralContext& ctx) {
    if (ctx.oC_IntegerLiteral()) {
        return transformIntegerLiteral(*ctx.oC_IntegerLiteral());
    } else if (ctx.oC_DoubleLiteral()) {
        return transformDoubleLiteral(*ctx.oC_DoubleLiteral());
    } else {
        throw invalid_argument("Unable to parse NumberLiteralContext.");
    }
}

string Transformer::transformPropertyKeyName(CypherParser::OC_PropertyKeyNameContext& ctx) {
    return transformSchemaName(*ctx.oC_SchemaName());
}

unique_ptr<ParsedExpression> Transformer::transformIntegerLiteral(
    CypherParser::OC_IntegerLiteralContext& ctx) {
    return make_unique<ParsedLiteralExpression>(
        LITERAL_INT, ctx.DecimalInteger()->getText(), ctx.getText());
}

unique_ptr<ParsedExpression> Transformer::transformDoubleLiteral(
    CypherParser::OC_DoubleLiteralContext& ctx) {
    return make_unique<ParsedLiteralExpression>(
        LITERAL_DOUBLE, ctx.RegularDecimalReal()->getText(), ctx.getText());
}

string Transformer::transformSchemaName(CypherParser::OC_SchemaNameContext& ctx) {
    return transformSymbolicName(*ctx.oC_SymbolicName());
}

string Transformer::transformSymbolicName(CypherParser::OC_SymbolicNameContext& ctx) {
    if (ctx.UnescapedSymbolicName()) {
        return ctx.UnescapedSymbolicName()->getText();
    } else if (ctx.EscapedSymbolicName()) {
        return ctx.EscapedSymbolicName()->getText();
    } else if (ctx.HexLetter()) {
        return ctx.HexLetter()->getText();
    } else {
        throw invalid_argument("Unable to parse SymbolicNameContext: " + ctx.getText());
    }
}

} // namespace parser
} // namespace graphflow
