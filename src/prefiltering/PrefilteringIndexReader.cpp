#include "PrefilteringIndexReader.h"
#include "DBWriter.h"
#include "Prefiltering.h"

const char*  PrefilteringIndexReader::CURRENT_VERSION="2.0.1";
unsigned int PrefilteringIndexReader::VERSION = 0;
unsigned int PrefilteringIndexReader::ENTRIES = 1;
unsigned int PrefilteringIndexReader::ENTRIESIZES = 2;
unsigned int PrefilteringIndexReader::ENTRIESNUM = 3;
unsigned int PrefilteringIndexReader::SEQCOUNT = 4;
unsigned int PrefilteringIndexReader::META = 5;
unsigned int PrefilteringIndexReader::SEQINDEXDATA = 6;
unsigned int PrefilteringIndexReader::SEQINDEXDATASIZE = 7;
unsigned int PrefilteringIndexReader::SEQINDEXSEQSIZE = 8;
bool PrefilteringIndexReader::checkIfIndexFile(DBReader<unsigned int>* reader) {
    char * version = reader->getDataByDBKey(VERSION);
    if(version == NULL){
        return false;
    }
    return (strncmp(version, CURRENT_VERSION, strlen(CURRENT_VERSION)) == 0 ) ? true : false;
}

void PrefilteringIndexReader::createIndexFile(std::string outDB, std::string outDBIndex,
                                             DBReader<unsigned int> *dbr, Sequence *seq, int split,
                                             int alphabetSize, int kmerSize, bool hasSpacedKmer,
                                             int searchMode) {
    DBWriter writer(outDB.c_str(), outDBIndex.c_str(), DBWriter::BINARY_MODE);
    writer.open();
    int stepCnt = split;

    for (int step = 0; step < stepCnt; step++) {
        size_t splitStart = 0;
        size_t splitSize  = 0;
        Util::decomposeDomainByAminoaAcid(dbr->getAminoAcidDBSize(), dbr->getSeqLens(), dbr->getSize(),
                step, stepCnt, &splitStart, &splitSize);
        IndexTable *indexTable;
        if (searchMode == Parameters::SEARCH_LOCAL || searchMode == Parameters::SEARCH_LOCAL_FAST) {
            indexTable = new IndexTable(alphabetSize, kmerSize, true);
        }else{
            Debug(Debug::ERROR) << "Seach mode is not valid.\n";
            EXIT(EXIT_FAILURE);
        }

        Prefiltering::fillDatabase(dbr, seq, indexTable, splitStart, splitStart + splitSize);

        // save the entries
        std::string entries_key = SSTR(Util::concatenate(ENTRIES, step));
        Debug(Debug::WARNING) << "Write " << entries_key << "\n";
        char *entries = (char *) indexTable->getEntries();
        writer.write(entries,
                indexTable->getTableEntriesNum() * indexTable->getSizeOfEntry(),
                (char *) entries_key.c_str(), 0);
        indexTable->deleteEntries();

        // save the size
        std::string entriesizes_key = SSTR(Util::concatenate(ENTRIESIZES, step));
        Debug(Debug::WARNING) << "Write " << entriesizes_key << "\n";

        char **sizes = indexTable->getTable();
        size_t *size = new size_t[indexTable->getTableSize()];
        for (size_t i = 0; i < indexTable->getTableSize(); i++) {
            const ptrdiff_t diff =  (sizes[i + 1] - sizes[i]) / indexTable->getSizeOfEntry();
            size[i] = (size_t) diff;
        }
        writer.write((char *) size, indexTable->getTableSize() * sizeof(size_t), (char *) entriesizes_key.c_str(), 0);
        delete[] size;

        SequenceLookup *lookup = indexTable->getSequenceLookup();
        std::string seqindexdata_key = SSTR(Util::concatenate(SEQINDEXDATA, step));
        Debug(Debug::WARNING) << "Write " << seqindexdata_key << "\n";
        writer.write(lookup->getData(), lookup->getDataSize(), seqindexdata_key.c_str(), 0);

        std::string seqindex_datasize_key = SSTR(Util::concatenate(SEQINDEXDATASIZE, step));
        Debug(Debug::WARNING) << "Write " << seqindex_datasize_key << "\n";
        int64_t seqindexDataSize = lookup->getDataSize();
        char *seqindexDataSizePtr = (char *) &seqindexDataSize;
        writer.write(seqindexDataSizePtr, 1 * sizeof(int64_t), (char *) seqindex_datasize_key.c_str(), 0);

        unsigned int *sequenceSizes = new unsigned int[lookup->getSequenceCount()];
        for (size_t i = 0; i < lookup->getSequenceCount(); i++) {
            unsigned int size = lookup->getSequence(i).second;
            sequenceSizes[i] = size;
        }
        std::string seqindex_seqsize = SSTR(Util::concatenate(SEQINDEXSEQSIZE, step));
        Debug(Debug::WARNING) << "Write " << seqindex_seqsize << "\n";
        writer.write((char *) sequenceSizes, lookup->getSequenceCount() * sizeof(unsigned int), (char *) seqindex_seqsize.c_str(), 0);
        delete[] sequenceSizes;

        // meta data
        // ENTRIESNUM
        std::string entriesnum_key = SSTR(Util::concatenate(ENTRIESNUM, step));
        Debug(Debug::WARNING) << "Write " << entriesnum_key << "\n";
        int64_t entriesNum = indexTable->getTableEntriesNum();
        char *entriesNumPtr = (char *) &entriesNum;
        writer.write(entriesNumPtr, 1 * sizeof(int64_t), (char *) entriesnum_key.c_str(), 0);
        // SEQCOUNT
        std::string tablesize_key = SSTR(Util::concatenate(SEQCOUNT, step));
        Debug(Debug::WARNING) << "Write " << tablesize_key << "\n";
        size_t tablesize = {indexTable->getSize()};
        char *tablesizePtr = (char *) &tablesize;
        writer.write(tablesizePtr, 1 * sizeof(size_t), (char *) tablesize_key.c_str(), 0);

        delete indexTable;
    }
    Debug(Debug::WARNING) << "Write " << META << "\n";
    int local = (searchMode) ? 1 : 0;
    int spacedKmer = (hasSpacedKmer) ? 1 : 0;
    int metadata[] = {kmerSize, alphabetSize, 0, split, local, spacedKmer};
    char *metadataptr = (char *) &metadata;
    writer.write(metadataptr, 6 * sizeof(int), SSTR(META).c_str(), 0);

    Debug(Debug::WARNING) << "Write " << VERSION << "\n";
    writer.write((char*)CURRENT_VERSION, strlen(CURRENT_VERSION) * sizeof(char), SSTR(VERSION).c_str(), 0);

    Debug(Debug::WARNING) << "Write MMSEQSFFINDEX \n";
    std::ifstream src(dbr->getIndexFileName(), std::ios::binary);
    std::ofstream dst(std::string(outDB + ".mmseqsindex").c_str(), std::ios::binary);
    dst << src.rdbuf();
    src.close();
    dst.close();
    writer.close();
    Debug(Debug::WARNING) << "Done. \n";
}

DBReader<unsigned int>*PrefilteringIndexReader::openNewReader(DBReader<unsigned int>*dbr) {
    std::string filePath(dbr->getDataFileName());
    std::string fullPath = filePath + std::string(".mmseqsindex");
    DBReader<unsigned int>*reader = new DBReader<unsigned int>("", fullPath.c_str(), DBReader<unsigned int>::INDEXONLY);
    reader->open(DBReader<unsigned int>::SORT);
    return reader;
}

IndexTable *PrefilteringIndexReader::generateIndexTable(DBReader<unsigned int>*dbr, int split, bool diagonalScoring) {
    int64_t   entriesNum    = *((int64_t *) dbr->getDataByDBKey(Util::concatenate(ENTRIESNUM, split)));
    size_t sequenceCount    = *((size_t *)dbr->getDataByDBKey(Util::concatenate(SEQCOUNT, split)));
    int64_t  seqDataSize    = *((int64_t *)dbr->getDataByDBKey(Util::concatenate(SEQINDEXDATASIZE, split)));
    PrefilteringIndexData data = getMetadata(dbr);
    dbr->unmapData();
    FILE * dataFile = dbr->getDatafile();
    size_t entriesOffset  = dbr->getDataOffset(Util::concatenate(ENTRIES, split));
    size_t entrieSizesOffset  = dbr->getDataOffset(Util::concatenate(ENTRIESIZES, split));
    size_t seqDataOffset  = dbr->getDataOffset(Util::concatenate(SEQINDEXDATA, split));
    size_t seqSizesOffset = dbr->getDataOffset(Util::concatenate(SEQINDEXSEQSIZE, split));
    IndexTable *retTable;
    if (data.local) {
        retTable = new IndexTable(data.alphabetSize, data.kmerSize, diagonalScoring);
    }else {
        Debug(Debug::ERROR) << "Seach mode is not valid.\n";
        EXIT(EXIT_FAILURE);
    }
    retTable->initTableByExternalData(dataFile, entriesNum,
                                      entrieSizesOffset, entriesOffset, sequenceCount,
                                      seqSizesOffset, seqDataOffset, seqDataSize);
    return retTable;
}


PrefilteringIndexData PrefilteringIndexReader::getMetadata(DBReader<unsigned int>*dbr) {
    PrefilteringIndexData prefData;
    int *version_tmp = (int *) dbr->getDataByDBKey(VERSION);
    Debug(Debug::WARNING) << "Index version: " << version_tmp[0] << "\n";
    int *metadata_tmp = (int *) dbr->getDataByDBKey(META);

    Debug(Debug::WARNING) << "KmerSize:     " << metadata_tmp[0] << "\n";
    Debug(Debug::WARNING) << "AlphabetSize: " << metadata_tmp[1] << "\n";
    Debug(Debug::WARNING) << "Skip:         " << metadata_tmp[2] << "\n";
    Debug(Debug::WARNING) << "Split:        " << metadata_tmp[3] << "\n";
    Debug(Debug::WARNING) << "Type:         " << metadata_tmp[4] << "\n";
    Debug(Debug::WARNING) << "Spaced:       " << metadata_tmp[5] << "\n";

    prefData.kmerSize = metadata_tmp[0];
    prefData.alphabetSize = metadata_tmp[1];
    prefData.skip = metadata_tmp[2];
    prefData.split = metadata_tmp[3];
    prefData.local = metadata_tmp[4];
    prefData.spacedKmer = metadata_tmp[5];

    return prefData;
}
