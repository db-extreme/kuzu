#include "src/binder/bound_ddl/include/bound_create_node_clause.h"
#include "src/binder/bound_ddl/include/bound_create_rel_clause.h"
#include "src/binder/bound_ddl/include/bound_drop_table.h"
#include "src/binder/include/binder.h"
#include "src/parser/ddl/include/create_node_clause.h"
#include "src/parser/ddl/include/drop_table.h"

namespace kuzu {
namespace binder {

unique_ptr<BoundStatement> Binder::bindCreateNodeClause(const Statement& statement) {
    auto& createNodeClause = (CreateNodeClause&)statement;
    auto tableName = createNodeClause.getTableName();
    if (catalog.getReadOnlyVersion()->containNodeTable(tableName)) {
        throw BinderException("Node " + tableName + " already exists.");
    }
    auto boundPropertyNameDataTypes =
        bindPropertyNameDataTypes(createNodeClause.getPropertyNameDataTypes());
    auto primaryKeyIdx = bindPrimaryKey(
        createNodeClause.getPKColName(), createNodeClause.getPropertyNameDataTypes());
    return make_unique<BoundCreateNodeClause>(
        tableName, move(boundPropertyNameDataTypes), primaryKeyIdx);
}

unique_ptr<BoundStatement> Binder::bindCreateRelClause(const Statement& statement) {
    auto& createRelClause = (CreateRelClause&)statement;
    auto tableName = createRelClause.getTableName();
    if (catalog.getReadOnlyVersion()->containRelTable(tableName)) {
        throw BinderException("Rel " + tableName + " already exists.");
    }
    auto propertyNameDataTypes =
        bindPropertyNameDataTypes(createRelClause.getPropertyNameDataTypes());
    auto relMultiplicity = getRelMultiplicityFromString(createRelClause.getRelMultiplicity());
    unordered_set<table_id_t> srcTableIDs, dstTableIDs;
    for (auto& srcTableName : createRelClause.getRelConnection().srcTableNames) {
        for (auto& dstTableName : createRelClause.getRelConnection().dstTableNames) {
            srcTableIDs.insert(bindNodeTable(srcTableName));
            dstTableIDs.insert(bindNodeTable(dstTableName));
        }
    }
    return make_unique<BoundCreateRelClause>(tableName, move(propertyNameDataTypes),
        relMultiplicity, SrcDstTableIDs(move(srcTableIDs), move(dstTableIDs)));
}

unique_ptr<BoundStatement> Binder::bindDropTable(const Statement& statement) {
    auto& dropTable = (DropTable&)statement;
    auto tableName = dropTable.getTableName();
    validateTableExist(catalog, tableName);
    auto catalogContent = catalog.getReadOnlyVersion();
    auto isNodeTable = catalogContent->containNodeTable(tableName);
    auto tableID = isNodeTable ? catalogContent->getNodeTableIDFromName(tableName) :
                                 catalogContent->getRelTableIDFromName(tableName);
    if (isNodeTable) {
        validateNodeTableHasNoEdge(catalog, tableID);
    }
    return make_unique<BoundDropTable>(
        isNodeTable ? (TableSchema*)catalogContent->getNodeTableSchema(tableID) :
                      (TableSchema*)catalogContent->getRelTableSchema(tableID));
}

vector<PropertyNameDataType> Binder::bindPropertyNameDataTypes(
    vector<pair<string, string>> propertyNameDataTypes) {
    vector<PropertyNameDataType> boundPropertyNameDataTypes;
    unordered_set<string> boundPropertyNames;
    for (auto& propertyNameDataType : propertyNameDataTypes) {
        if (boundPropertyNames.contains(propertyNameDataType.first)) {
            throw BinderException(StringUtils::string_format(
                "Duplicated column name: %s, column name must be unique.",
                propertyNameDataType.first.c_str()));
        } else if (TableSchema::isReservedPropertyName(propertyNameDataType.first)) {
            throw BinderException(
                StringUtils::string_format("PropertyName: %s is an internal reserved propertyName.",
                    propertyNameDataType.first.c_str()));
        }
        StringUtils::toUpper(propertyNameDataType.second);
        boundPropertyNameDataTypes.emplace_back(
            propertyNameDataType.first, Types::dataTypeFromString(propertyNameDataType.second));
        boundPropertyNames.emplace(propertyNameDataType.first);
    }
    return boundPropertyNameDataTypes;
}

uint32_t Binder::bindPrimaryKey(
    string pkColName, vector<pair<string, string>> propertyNameDataTypes) {
    uint32_t primaryKeyIdx = UINT32_MAX;
    for (auto i = 0u; i < propertyNameDataTypes.size(); i++) {
        if (propertyNameDataTypes[i].first == pkColName) {
            primaryKeyIdx = i;
        }
    }
    if (primaryKeyIdx == UINT32_MAX) {
        throw BinderException(
            "Primary key " + pkColName + " does not match any of the predefined node properties.");
    }
    auto primaryKey = propertyNameDataTypes[primaryKeyIdx];
    StringUtils::toUpper(primaryKey.second);
    // We only support INT64 and STRING column as the primary key.
    if ((primaryKey.second != string("INT64")) && (primaryKey.second != string("STRING"))) {
        throw BinderException("Invalid primary key type: " + primaryKey.second + ".");
    }
    return primaryKeyIdx;
}

} // namespace binder
} // namespace kuzu
