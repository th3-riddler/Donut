#ifndef SEARCH_HPP
#define SEARCH_HPP

#include "../Evaluation/evaluation.hpp"
#include "../Chessboard/chessboard.hpp"
#include "../nnueEval/nnueEval.hpp"
#include "../Macros/macros.hpp"
#include "../Move/move.hpp"
#include "../Reader/reader.hpp"

struct moves;

struct tt {
    uint64_t key;
    uint64_t data;
};

class Search {
    public:
        static int negamax(int alpha, int beta, int depth, int excludedMove = 0);
        static int quiescenceSearch(int alpha, int beta);

        static void searchPosition(int depth, int threadId = 0);
        static inline void enablePvScore(moves *moveList);
        static void clearTranspositionTable();
        static int readHashEntry(int alpha, int beta, int* bestMove, int depth);
        static void writeHashEntry(int score, int bestMove, int depth, int flag);
        static inline bool isRepetition();
        static void initHashTable(int mb);
        static void initLMRTable();
        static int lmrTable[64][64];

        static thread_local int ply;

        static thread_local int killerMoves[2][maxPly];
        static thread_local int historyMoves[12][64];
        static thread_local int pvLength[maxPly];
        static thread_local int pvTable[maxPly][maxPly];
        static thread_local bool followPv;
        static thread_local bool scorePv;
        static thread_local int fifty;

        static thread_local int playedMoves[maxPly];
        static thread_local int counterMoves[12][64];
        static thread_local int followUpMoves[12][64][12][64];
        static thread_local int captureHistory[12][64][14];

        static int hashEntries;

        static const int fullDepthMoves;
        static const int reductionLimit;
        static tt *transpositionTable;

        static thread_local int repetitionIndex;
        static thread_local uint64_t repetitionTable[1000];
};







#endif // SEARCH_HPP