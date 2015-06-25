#include "TimeTest.h"
#include "QueryTemplateMatcherGlobal.h"
#include "QueryTemplateMatcherLocal.h"

TimeTest::TimeTest(std::string queryDB,
        std::string queryDBIndex,
        std::string targetDB,
        std::string targetDBIndex,
        std::string scoringMatrixFile,
        size_t maxSeqLen,
        std::string logFile){

    std::cout << "Init data structures...\n";
    BUFFER_SIZE = 1000000;

    this->logFile = logFile;

    this->maxSeqLen = maxSeqLen;

    this->threads = 1;
#ifdef OPENMP
    threads = omp_get_max_threads();
    std::cout << "Using " << threads << " threads.\n";
#endif
    std::cout << "\n";

//    this->qdbr = new DBReader(queryDB.c_str(), queryDBIndex.c_str());
//    qdbr->open(DBReader::NOSORT);
    this->targetDB = targetDB;
    this->targetDBIndex = targetDBIndex;

//    std::cout << "Query database: " << queryDB << "(size=" << qdbr->getSize() << ")\n";

    this->seqs = new Sequence*[threads];

    this->scoringMatrixFile = scoringMatrixFile;

    std::cout << "Init done.\n\n";
}

TimeTest::~TimeTest(){
    delete[] seqs;

}

void TimeTest::runTimeTest (){

    int QUERY_SET_SIZE = 50000;

    std::ofstream logFileStream;
    logFileStream.open(logFile.c_str());

    QueryTemplateMatcher** matchers = new QueryTemplateMatcher *[threads];
    DBReader * tdbr = new DBReader(targetDB.c_str(), targetDBIndex.c_str());
    tdbr->open(DBReader::SORT);
    size_t targetSeqLenSum = 0;
    for (size_t i = 0; i < tdbr->getSize(); i++)
        targetSeqLenSum += tdbr->getSeqLens()[i];

    // generate a small random sequence set for testing
    size_t querySetSize = tdbr->getSize();
    if (querySetSize > QUERY_SET_SIZE)
        querySetSize = QUERY_SET_SIZE;

    int* querySeqs = new int[querySetSize];
    srand(1);
    size_t querySeqLenSum = 0;
    for (int i = 0; i < querySetSize; i++){
        querySeqs[i] = rand() % tdbr->getSize();
        querySeqLenSum += tdbr->getSeqLens()[querySeqs[i]];
    }

    short kmerThrPerPosMin = 1;
    short kmerThrPerPosMax = 25;

    // adjust k-mer list length threshold
    for (int alphabetSize = 21; alphabetSize <= 21; alphabetSize += 4){



        std::cout << "Target database: " << targetDB << "(size=" << tdbr->getSize() << ")\n";

        BaseMatrix* m = new SubstitutionMatrix (scoringMatrixFile.c_str(), 8.0);
        BaseMatrix* subMat;
        if (alphabetSize < 21)
            subMat = new ReducedMatrix(m->probMatrix, m->subMatrixPseudoCounts, alphabetSize, 8.0);
        else
            subMat = m;

        ExtendedSubstitutionMatrix* _2merSubMatrix = new ExtendedSubstitutionMatrix(subMat->subMatrix, 2, alphabetSize);
        ExtendedSubstitutionMatrix* _3merSubMatrix = new ExtendedSubstitutionMatrix(subMat->subMatrix, 3, alphabetSize);


        for(int isSpaced = 1; isSpaced < 2; isSpaced++ ){
            for(int isLocal = 1; isLocal < 2; isLocal++ ){
                for (int kmerSize = 6; kmerSize <= 7; kmerSize++){
#pragma omp parallel for schedule(static)
                    for (int i = 0; i < threads; i++){
                        int thread_idx = 0;
#ifdef OPENMP
            thread_idx = omp_get_thread_num();
#endif
                        seqs[thread_idx] = new Sequence(maxSeqLen, subMat->aa2int, subMat->int2aa, Sequence::AMINO_ACIDS, kmerSize, isSpaced);
                    }

                    short kmerThrMin = (short)((float)(kmerThrPerPosMin * kmerSize) * (pow(( (float)alphabetSize / 21.0 ), 2.0)));
                    int kmerThrMax = kmerThrPerPosMax * kmerSize;

                    std::cout << "------------------ a = " << alphabetSize << ",  k = " << kmerSize << " -----------------------------\n";
                    IndexTable* indexTable = Prefiltering::generateIndexTable(tdbr, seqs[0], alphabetSize, kmerSize, 0, tdbr->getSize(), isLocal);

                    short decr = 1;
                    if (kmerSize == 6 || kmerSize == 7)
                        decr = 2;
                    std::cout << "Omitting runs with too short running time...\n";
                    for (short kmerThr = kmerThrMax; kmerThr >= kmerThrMin; kmerThr -= decr){
                        size_t dbMatchesSum = 0;
                        size_t doubleMatches = 0;
                        double kmersPerPos = 0.0;
                        double kmerMatchProb = 0.0;

                        // determine k-mer match probability and k-mer list length for kmerThr
#pragma omp parallel for schedule(static)
                        for (int i = 0; i < threads; i++){
                            int thread_idx = 0;
#ifdef OPENMP
                        thread_idx = omp_get_thread_num();
#endif
                            // set a current k-mer list length threshold and a high prefitlering threshold (we don't need the prefiltering results in this test run)
                            if(isLocal == 1){
                                matchers[thread_idx] = new QueryTemplateMatcherLocal(subMat, indexTable, tdbr->getSeqLens(), kmerThr, 1.0, kmerSize, seqs[0]->getEffectiveKmerSize(), tdbr->getSize(), false, maxSeqLen, 300);
                            }
                            else{
                                matchers[thread_idx] = new QueryTemplateMatcherGlobal(subMat, indexTable, tdbr->getSeqLens(), kmerThr, 1.0, kmerSize, seqs[0]->getEffectiveKmerSize(), tdbr->getSize(), false, maxSeqLen, 500.0);
                            }
                            matchers[thread_idx]->setSubstitutionMatrix(_3merSubMatrix->scoreMatrix, _2merSubMatrix->scoreMatrix );
                        }

                        struct timeval start, end;
                        gettimeofday(&start, NULL);

#pragma omp parallel for schedule(dynamic, 10) reduction (+: dbMatchesSum, kmersPerPos, doubleMatches)
                        for (int i = 0; i < querySetSize; i++){
                            int id = querySeqs[i];

                            int thread_idx = 0;
#ifdef OPENMP
                    thread_idx = omp_get_thread_num();
#endif
                            char* seqData = tdbr->getData(id);
                            seqs[thread_idx]->mapSequence(id, tdbr->getDbKey(id), seqData);

                            matchers[thread_idx]->matchQuery(seqs[thread_idx], UINT_MAX);

                            kmersPerPos += matchers[thread_idx]->getStatistics()->kmersPerPos;
                            dbMatchesSum += matchers[thread_idx]->getStatistics()->dbMatches;
                            doubleMatches += matchers[thread_idx]->getStatistics()->doubleMatches;
                        }
                        gettimeofday(&end, NULL);
                        int sec = end.tv_sec - start.tv_sec;

                        // too short running time is not recorded
                        if (sec <= 2){
                            //std::cout << "Time <= 2 sec, not counting this step.\n\n";
                            continue;
                        }
                        std::cout << "spaced = " << isSpaced << ", local = " << isLocal << ", k = " << kmerSize << ", a = " << alphabetSize << "\n";
                        std::cout << "k-mer threshold = " << kmerThr << "\n";
                        std::cout << "Time: " << (sec / 3600) << " h " << (sec % 3600 / 60) << " m " << (sec % 60) << "s\n";

                        kmersPerPos /= (double)querySetSize;
                        if(isLocal == 1){
                            kmerMatchProb = (((double) doubleMatches) / ((double) (querySeqLenSum * targetSeqLenSum)))/256;
                        }else {
                            kmerMatchProb = ((double) dbMatchesSum) / ((double) (querySeqLenSum * targetSeqLenSum));
                        }

                        std::cout << "kmerPerPos: " << kmersPerPos << "\n";
                        std::cout << "k-mer match probability: " << kmerMatchProb << "\n";
                        std::cout << "dbMatch: " << dbMatchesSum << "\n";
                        std::cout << "doubleMatches: " << doubleMatches << "\n";
                        std::cout << "querySeqLenSum: " << querySeqLenSum << "\n";
                        std::cout << "targetSeqLenSum: " << targetSeqLenSum << "\n\n";


                        logFileStream << kmersPerPos << "\t" << kmerMatchProb << "\t" <<  dbMatchesSum << "\t" << doubleMatches << "\t" << kmerSize << "\t" << alphabetSize << "\t" << isSpaced << "\t" << isLocal << "\t"  << sec << "\n";

                        // running time for the next step will be too long
                        if (sec >= 1200){
                            std::cout << "Time >= 300 sec, going to the next parameter combination.\n";
                            break;
                        }

                        for (int j = 0; j < threads; j++){
                            delete matchers[j];
                        }
                    }
                    delete indexTable;

                }
            }
        }
        for (int i = 0; i < threads; i++){
            delete seqs[i];
        }

        delete m;
        if (alphabetSize < 21)
            delete subMat;
        delete _2merSubMatrix;
        delete _3merSubMatrix;
    }

    tdbr->close();
    delete tdbr;
    logFileStream.close();
    delete[] querySeqs;
    delete[] matchers;
}

