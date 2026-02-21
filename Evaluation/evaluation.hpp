#ifndef EVALUATION_HPP
#define EVALUATION_HPP

#include <cstdint>

#include "../Chessboard/chessboard.hpp"
#include "../nnueEval/nnueEval.hpp"

class Evaluation {
    public:
        static const int materialScore[2][12];
        static const int openingPhaseScore;
        static const int endgamePhaseScore;
        static const int positionalScore[2][6][64];

        static const int pawnScore[64];
        static const int knightScore[64];
        static const int bishopScore[64];
        static const int rookScore[64];
        static const int queenScore[64];
        static const int kingScore[64];
        static const int mirrorPieceScore[128];
        static const int mvvLva[12][12];

        static uint64_t fileMasks[64];
        static uint64_t rankMasks[64];
        static uint64_t isolatedMasks[64];
        static uint64_t whitePassedMasks[64];
        static uint64_t blackPassedMasks[64];
        static const int getRank[64];

        static const int doublePawnPenaltyOpening;
        static const int doublePawnPenaltyEndgame;

        static const int isolatedPawnPenaltyOpening;
        static const int isolatedPawnPenaltyEndgame;

        static const int passedPawnBonus[8];
        static const int semiOpenFileBonus;
        static const int openFileBonus;

        static const int bishopUnit;
        static const int queenUnit;

        static const int bishopMobilityOpening;
        static const int bishopMobilityEndgame;
        static const int queenMobilityOpening;
        static const int queenMobilityEndgame;

        static const int kingShieldBonus;

        static int evaluate();
        static int scoreMove(int move);
        static uint64_t setFileRankMask(int fileNumber, int rankNumber);
        static void initEvalMasks();
        static inline int getGamePhaseScore();

        static void nnueInput(int *pieces, int *squares);

        static int nnuePieces[12];
        static int nnueSquares[64];
        
        static const int pieceValues[14];
        static uint64_t getAttackers(int targetSquare, uint64_t occupied);
        static int getLeastValuableAttacker(uint64_t attackers, int side, int& pieceType);
        static int see(int move);

        enum { opening, endgame, middlegame };
        enum { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING };
};




#endif // EVALUATION_HPP