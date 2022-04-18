#include "src/storage/include/store/rel.h"

#include <iostream>
using namespace graphflow::catalog;

namespace graphflow {
namespace storage {

Rel::Rel(const Catalog& catalog, label_t relLabel, const string& directory,
    BufferManager& bufferManager, bool isInMemoryMode)
    : logger{LoggerUtils::getOrCreateSpdLogger("storage")}, relLabel{relLabel} {
    initAdjColumnOrLists(catalog, directory, bufferManager, isInMemoryMode);
    initPropertyListsAndColumns(catalog, directory, bufferManager, isInMemoryMode);
}

void Rel::initAdjColumnOrLists(const Catalog& catalog, const string& directory,
    BufferManager& bufferManager, bool isInMemoryMode) {
    logger->info("Initializing AdjColumns and AdjLists for rel {}.", relLabel);
    for (auto relDirection : REL_DIRECTIONS) {
        const auto& nodeLabels = catalog.getNodeLabelsForRelLabelDirection(relLabel, relDirection);
        const auto& nbrNodeLabels =
            catalog.getNodeLabelsForRelLabelDirection(relLabel, !relDirection);
        for (auto nodeLabel : nodeLabels) {
            NodeIDCompressionScheme nodeIDCompressionScheme(
                nbrNodeLabels, catalog.getNumNodesPerLabel(), catalog.getNumNodeLabels());
            logger->debug("DIRECTION {} nodeLabel {} relLabel {} compressionScheme {},{},{}",
                relDirection, nodeLabel, relLabel, nodeIDCompressionScheme.getNumBytesForLabel(),
                nodeIDCompressionScheme.getNumBytesForOffset(),
                nodeIDCompressionScheme.getCommonLabel());
            if (catalog.isSingleMultiplicityInDirection(relLabel, relDirection)) {
                // Add adj column.
                auto fName =
                    StorageUtils::getAdjColumnFName(directory, relLabel, nodeLabel, relDirection);
                auto adjColumn = make_unique<AdjColumn>(
                    fName, bufferManager, nodeIDCompressionScheme, isInMemoryMode);
                adjColumns[relDirection].emplace(nodeLabel, move(adjColumn));
            } else {
                // Add adj list.
                auto fName =
                    StorageUtils::getAdjListsFName(directory, relLabel, nodeLabel, relDirection);
                auto adjList = make_unique<AdjLists>(
                    fName, bufferManager, nodeIDCompressionScheme, isInMemoryMode);
                adjLists[relDirection].emplace(nodeLabel, move(adjList));
            }
        }
    }
    logger->info("Initializing AdjColumns and AdjLists for rel {} done.", relLabel);
}

void Rel::initPropertyListsAndColumns(const Catalog& catalog, const string& directory,
    BufferManager& bufferManager, bool isInMemoryMode) {
    logger->info("Initializing PropertyLists and PropertyColumns for rel {}.", relLabel);
    if (!catalog.getRelProperties(relLabel).empty()) {
        if (catalog.isSingleMultiplicityInDirection(relLabel, FWD)) {
            initPropertyColumnsForRelLabel(catalog, directory, bufferManager, FWD, isInMemoryMode);
        } else if (catalog.isSingleMultiplicityInDirection(relLabel, BWD)) {
            initPropertyColumnsForRelLabel(catalog, directory, bufferManager, BWD, isInMemoryMode);
        } else {
            initPropertyListsForRelLabel(catalog, directory, bufferManager, isInMemoryMode);
        }
    }
    logger->info("Initializing PropertyLists and PropertyColumns for rel {} Done.", relLabel);
}

void Rel::initPropertyColumnsForRelLabel(const Catalog& catalog, const string& directory,
    BufferManager& bufferManager, const RelDirection& dir, bool isInMemoryMode) {
    logger->debug("Initializing PropertyColumns: relLabel {}", relLabel);
    for (auto& nodeLabel : catalog.getNodeLabelsForRelLabelDirection(relLabel, dir)) {
        auto& properties = catalog.getRelProperties(relLabel);
        propertyColumns[nodeLabel].resize(properties.size());
        for (auto& property : properties) {
            auto fName = StorageUtils::getRelPropertyColumnFName(
                directory, relLabel, nodeLabel, property.name);
            logger->debug("DIR {} nodeLabel {} propertyIdx {} type {} name `{}`", dir, nodeLabel,
                property.id, property.dataType.typeID, property.name);
            propertyColumns[nodeLabel][property.id] =
                ColumnFactory::getColumn(fName, property.dataType, bufferManager, isInMemoryMode);
        }
    }
    logger->debug("Initializing PropertyColumns done.");
}

void Rel::initPropertyListsForRelLabel(const Catalog& catalog, const string& directory,
    BufferManager& bufferManager, bool isInMemoryMode) {
    logger->debug("Initializing PropertyLists for rel {}", relLabel);
    for (auto relDirection : REL_DIRECTIONS) {
        for (auto& nodeLabel : catalog.getNodeLabelsForRelLabelDirection(relLabel, relDirection)) {
            auto& properties = catalog.getRelProperties(relLabel);
            auto adjListsHeaders = adjLists[relDirection].at(nodeLabel)->getHeaders();
            propertyLists[relDirection].emplace(
                nodeLabel, vector<unique_ptr<Lists>>(properties.size()));
            for (auto propertyIdx = 0u; propertyIdx < properties.size(); propertyIdx++) {
                auto fName = StorageUtils::getRelPropertyListsFName(
                    directory, relLabel, nodeLabel, relDirection, properties[propertyIdx].name);
                logger->debug("relDirection {} nodeLabel {} propertyIdx {} type {} name `{}`",
                    relDirection, nodeLabel, properties[propertyIdx].id,
                    properties[propertyIdx].dataType.typeID, properties[propertyIdx].name);
                auto propertyList = ListsFactory::getLists(fName, properties[propertyIdx].dataType,
                    adjListsHeaders, bufferManager, isInMemoryMode);
                propertyLists[relDirection].at(nodeLabel)[propertyIdx] = move(propertyList);
            }
        }
    }
    logger->debug("Initializing PropertyLists for rel {} done.", relLabel);
}

} // namespace storage
} // namespace graphflow