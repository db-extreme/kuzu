#pragma once

#include <iostream>

#include "src/storage/buffer_manager/include/versioned_file_handle.h"

namespace graphflow {
namespace storage {

static constexpr uint64_t NUM_PAGE_IDXS_PER_PIP =
    (DEFAULT_PAGE_SIZE - sizeof(page_idx_t)) / sizeof(page_idx_t);

/**
 * Header page of a disk array.
 */
struct DiskArrayHeader {
    DiskArrayHeader(){};

    DiskArrayHeader(uint64_t elementSize, uint64_t numElements, uint64_t firstPIPPageIdx);

    void saveToDisk(FileHandle& fileHandle, uint64_t headerPageIdx);

    void readFromFile(FileHandle& fileHandle, uint64_t headerPageIdx);

    // TODO(Semih): This is only for debugging purposes. Will be removed.
    void print();

    // We do not need to store numElementsPerPageLog2, elementPageOffsetMask, and numArrayPages or
    // save them on disk as they are functions of elementSize and numElements but we
    // nonetheless store them (and save them to disk) for simplicity.
    uint64_t elementSize;
    uint64_t numElements;
    uint64_t firstPIPPageIdx;
    uint64_t numElementsPerPageLog2;
    uint64_t elementPageOffsetMask;
    uint64_t numArrayPages;
};

struct PIP {
    PIP() : nextPipPageIdx{PAGE_IDX_MAX} {}

    // TODO(Semih): This is only for debugging purposes. Will be removed.
    void print();

    page_idx_t nextPipPageIdx;
    page_idx_t pageIdxs[NUM_PAGE_IDXS_PER_PIP];
};

struct PIPWrapper {
    PIPWrapper(shared_ptr<FileHandle> fileHandle, page_idx_t pipPageIdx) : pipPageIdx(pipPageIdx) {
        fileHandle->readPage(reinterpret_cast<uint8_t*>(&pipContents), pipPageIdx);
    }

    PIPWrapper(page_idx_t pipPageIdx) : pipPageIdx(pipPageIdx) {}

    // TODO(Semih): This is only for debugging purposes. Will be removed.
    void print() {
        cout << "Printing PIPWrapper pipPageIdx: " << pipPageIdx << endl;
        pipContents.print();
    }

    page_idx_t pipPageIdx;
    PIP pipContents;
};

/**
 * DiskArray stores a disk-based array in a file. The array is broken down into a predefined and
 * stable header page, i.e., the header page of the array is always in a pre-allocated page in the
 * file. The header page contains the pointer to the first ``page indices page'' (pip). Each pip
 * stores a list of page indices that store the ``array pages''. Each pip also stores the pageIdx of
 * the next pip if one exists (or we use PAGE_IDX_MAX as null). Array pages store the actual data in
 * the array.
 *
 * Storage structures can use multiple disk arrays in a single file by giving each one a different
 * pre-allocated stable header pageIdxs.
 */
class BaseDiskArray {
public:
    // Every DiskArray needs to have a pre-allocated header page, so we can know where to start to
    // read the array (its pips and array pages).
    BaseDiskArray(shared_ptr<FileHandle> fileHandle, page_idx_t headerPageIdx, uint64_t elementSize,
        uint64_t numElements);

    BaseDiskArray(shared_ptr<FileHandle> fileHandle, page_idx_t headerPageIdx);

    virtual ~BaseDiskArray() {}

    // This function does division and mod and should not be used in performance critical code
    page_idx_t getArrayFilePageIdx(page_idx_t arrayPageIdx);

    virtual void saveToDisk();

    // TODO(Semih): This is only for debugging purposes. Will be removed.
    void print();

public:
    DiskArrayHeader header;

protected:
    shared_ptr<FileHandle> fileHandle;
    page_idx_t headerPageIdx;
    vector<PIPWrapper> pips;
};

/**
 * Stores an array of type U's page by page in memory, using OS memory and not the buffer manager.
 * Designed currently to be used by lists headers and metadata, where we want to avoid using
 * pins/unpins when accessing data through the buffer manager.
 */
template<class U>
class InMemDiskArray : public BaseDiskArray {
public:
    InMemDiskArray(
        shared_ptr<FileHandle> fileHandle, page_idx_t headerPageIdx, uint64_t numElements);

    InMemDiskArray(shared_ptr<FileHandle> fileHandle, page_idx_t headerPageIdx);

    // [] operator to be used when building an InMemDiskArray without transactional updates. This
    // changes the contents directly in memory and not on disk (nor on the wal)
    U& operator[](uint64_t idx);

    void saveToDisk() override;

    // TODO(Semih): This is only for debugging purposes. Will be removed.
    void print();

private:
    vector<unique_ptr<U[]>> dataPages;
};

} // namespace storage
} // namespace graphflow