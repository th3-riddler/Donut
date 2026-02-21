#include <iostream>

#include "search.hpp"

thread_local int Search::ply;

thread_local int Search::killerMoves[2][64];
thread_local int Search::historyMoves[12][64];
thread_local int Search::pvLength[64];
thread_local int Search::pvTable[64][64];
thread_local bool Search::followPv;
thread_local bool Search::scorePv;
const int Search::fullDepthMoves = 4;
const int Search::reductionLimit = 3;
int Search::hashEntries = 0;
tt* Search::transpositionTable = NULL;

thread_local uint64_t Search::repetitionTable[1000];
thread_local int Search::repetitionIndex;

thread_local int Search::fifty;

inline void Search::enablePvScore(moves *moveList) {
    followPv = false;

    for (int count = 0; count < moveList->count; count++) {
        if (pvTable[0][ply] == moveList->moves[count]) {
            scorePv = true;
            followPv = true;
        }
    }
}

void Search::clearTranspositionTable() {
    tt *hashEntry;
    for (hashEntry = transpositionTable; hashEntry < transpositionTable + hashEntries; hashEntry++) {
        hashEntry->hashKey = 0;
        hashEntry->depth = 0;
        hashEntry->flags = 0;
        hashEntry->score = 0;
    }
}

void Search::initHashTable(int mb) {
    int hashSize = 0x100000 * mb;

    hashEntries = hashSize / sizeof(tt);

    if (transpositionTable != NULL) {
        // std::cout << "  Clearing hash memory..." << std::endl;
        free(transpositionTable);
    }

    transpositionTable = (tt *) malloc(hashEntries * sizeof(tt));

    if (transpositionTable == NULL) {
        // std::cout << "  Couldn't allocate memory for hash table! Trying with " << mb / 2 << "MB..." << std::endl;

        initHashTable(mb / 2);
    }
    else {
        clearTranspositionTable();
        // std::cout << "  Hash Table has been initialized with " << hashEntries << " entries." << std::endl;
    }
}

int Search::readHashEntry(int alpha, int beta, int* bestMove, int depth) {
    tt *hashEntry = &transpositionTable[Chessboard::hashKey % hashEntries];

    if (hashEntry->hashKey == Chessboard::hashKey) {
        if (hashEntry->depth >= depth) {

            int score = hashEntry->score;

            if (score < -mateScore) { score += ply; }
            if (score > mateScore) { score -= ply; }

            if (hashEntry->flags == hashFlagExact) {
                return score;
            }
            if ((hashEntry->flags == hashFlagAlpha) && (score <= alpha)) {
                return alpha;
            }
            if ((hashEntry->flags == hashFlagBeta) && (score >= beta)) {
                return beta;
            }
        }
        *bestMove = hashEntry->bestMove;
    }

    return noHashEntry;
}

void Search::writeHashEntry(int score, int bestMove, int depth, int flag) {
    tt *hashEntry = &transpositionTable[Chessboard::hashKey % hashEntries];

    if (score < -mateScore) { score -= ply; }
    if (score > mateScore) { score += ply; }

    hashEntry->hashKey = Chessboard::hashKey;
    hashEntry->score = score;
    hashEntry->flags = flag;
    hashEntry->depth = depth;
    hashEntry->bestMove = bestMove;
}


inline bool Search::isRepetition() {

    for (int index = 0; index < repetitionIndex; index++) {
        if (repetitionTable[index] == Chessboard::hashKey) {
            return true;
        }
    }

    return false;
}


int Search::quiescenceSearch(int alpha, int beta) {

    if ((Chessboard::nodes & 2047) == 0) {
        Chessboard::communicate();
    }

    Chessboard::nodes++;
    
    if (ply > maxPly - 1) {
        return Evaluation::evaluate();
    }

    int evaluation = Evaluation::evaluate();

    // Fail hard beta-cutoff
    if (evaluation >= beta) {
        return beta;
    }

    if (evaluation > alpha) {
        alpha = evaluation;
    }

    
    moves moveList[1];
    Chessboard::generateMoves(moveList);

    Move::sortMoves(moveList, 0);

    for (int count = 0; count < moveList->count; count++) {
        // SEE Pruning in Quiescence Search
        if (Evaluation::see(moveList->moves[count]) < 0) {
            continue;
        }

        copyBoard();

        ply++;

        repetitionIndex++;
        repetitionTable[repetitionIndex] = Chessboard::hashKey;

        if(Chessboard::makeMove(moveList->moves[count], Chessboard::capturesOnly) == 0) {
            ply--;

            repetitionIndex--;

            continue;
        }

        int score = -quiescenceSearch(-beta, -alpha);

        ply--;

        repetitionIndex--;

        takeBack();

        if (Chessboard::stopped) {
            return 0;
        }

        if (score > alpha) {
            alpha = score;

            // Fail hard beta-cutoff
            if (score >= beta) {
                return beta;
            }
        }
    }

    return alpha;
}

int Search::negamax(int alpha, int beta, int depth, int excludedMove) {

    pvLength[ply] = ply;

    int score;

    // The best move to store in the Transoposition Table
    int bestMove = 0;

    int hashFlag = hashFlagAlpha;

    if ((ply && isRepetition()) || fifty >= 100) {
        return 0;
    }

    bool pvNode = beta - alpha > 1;

    if (ply && ((score = readHashEntry(alpha, beta, &bestMove, depth)) != noHashEntry) && !pvNode) {
        return score;
    }

    if ((Chessboard::nodes & 2047) == 0) {
        Chessboard::communicate();
    }

    if (depth == 0) {
        return quiescenceSearch(alpha, beta);
    }

    if (ply > maxPly - 1) {
        return Evaluation::evaluate();
    }

    Chessboard::nodes++;

    int inCheck = Chessboard::isSquareAttacked(Chessboard::getLSBIndex((Chessboard::bitboard.sideToMove == Chessboard::white ? 
                                                                        Chessboard::bitboard.bitboards[Chessboard::K] : Chessboard::bitboard.bitboards[Chessboard::k])), 
                                                                        Chessboard::bitboard.sideToMove ^ 1);

    if (inCheck) {
        depth++;
    }

    int legalMoves = 0;

    // Evaluation Pruning
    int staticEval = Evaluation::evaluate();

    if (depth < 3 && !pvNode && !inCheck && abs(beta - 1) > -infinity + 100) {
        int evalMargin = 120 * depth;

        if (staticEval - evalMargin >= beta) {
            return staticEval - evalMargin;
        }
    }

    // Null Move Pruning
    if (depth >= 3 && !inCheck && ply) {
        copyBoard();

        ply++;

        repetitionIndex++;
        repetitionTable[repetitionIndex] = Chessboard::hashKey;

        if (Chessboard::bitboard.enPassantSquare != Chessboard::noSquare) {
            Chessboard::hashKey ^= Chessboard::enPassantKeys[Chessboard::bitboard.enPassantSquare];
        }
        Chessboard::bitboard.enPassantSquare = Chessboard::noSquare;

        Chessboard::bitboard.sideToMove ^= 1;
        Chessboard::hashKey ^= Chessboard::sideKey;

        // Search Moves with a reduced depth (depth - 1 - R) --> R is the reduction amount
        score = -negamax(-beta, -beta + 1, depth - 1 - 2, 0);

        ply--;

        repetitionIndex--;

        takeBack();

        if (Chessboard::stopped) {
            return 0;
        }

        if (score >= beta) {
            return beta;
        }
    }

    // Razoring
    if (!pvNode && !inCheck && depth <= 3) {
        score = staticEval + 125;

        int newScore;

        if (score < beta) {
            if (depth == 1) {
                newScore = quiescenceSearch(alpha, beta);

                return (newScore > score) ? newScore : score;
            }
            score += 175;

            if (score < beta && depth <= 2) {
                newScore = quiescenceSearch(alpha, beta);

                if (newScore < beta) {
                    return (newScore > score) ? newScore : score;
                }
            }
        }
    }

    moves moveList[1];
    Chessboard::generateMoves(moveList);

    if (followPv) {
        enablePvScore(moveList);
    }

    Move::sortMoves(moveList, bestMove);

    int movesSearched = 0;
    int extension = 0;

    // Singular Extension detection
    // Limitato a profondità avanzate (>= 8) ed esclusi i punteggi di matto in TT per tagliare i tempi morti
    if (depth >= 8 && excludedMove == 0 && !pvNode && bestMove != 0 && abs(beta) < mateScore) {
        tt *hashEntry = &transpositionTable[Chessboard::hashKey % hashEntries];
        if (hashEntry->hashKey == Chessboard::hashKey && hashEntry->depth >= depth - 3 && hashEntry->flags != hashFlagAlpha && abs(hashEntry->score) < mateScore) {
            int singularBeta = hashEntry->score - depth * 2;
            int singularDepth = (depth - 1) / 2;
            int singularScore = negamax(singularBeta - 1, singularBeta, singularDepth, bestMove);
            if (singularScore < singularBeta) {
                extension = 1;
            }
        }
    }

    for (int count = 0; count < moveList->count; count++) {
        
        if (moveList->moves[count] == excludedMove) continue;

        copyBoard();

        ply++;

        repetitionIndex++;
        repetitionTable[repetitionIndex] = Chessboard::hashKey;

        if(Chessboard::makeMove(moveList->moves[count], Chessboard::allMoves) == 0) {
            ply--;

            repetitionIndex--;

            continue;
        }

        legalMoves++;

        if (movesSearched == 0) {
            int moveExtension = (moveList->moves[count] == bestMove) ? extension : 0;
            score = -negamax(-beta, -alpha, depth - 1 + moveExtension, 0);
        }
        else {
            // Late Move Reduction (LMR)
            if (movesSearched >= fullDepthMoves && depth >= reductionLimit && !inCheck && (getMoveCapture(moveList->moves[count]) == 13) && getMovePromoted(moveList->moves[count]) == 0) {
                score = -negamax(-alpha - 1, -alpha, depth - 2, 0);
            }
            else {
                score = alpha + 1;
            }
            
            // Principal Variation Search (PVS)
            if (score > alpha) {
                score = -negamax(-alpha - 1, -alpha, depth - 1, 0);
            
                if ((score > alpha) && (score < beta)) {
                    score = -negamax(-beta, -alpha, depth - 1, 0);
                }
            }
        }

        ply--;

        repetitionIndex--;

        takeBack();

        if (Chessboard::stopped) {
            return 0;
        }

        movesSearched++;

        if (score > alpha) {
            hashFlag = hashFlagExact;

            bestMove = moveList->moves[count];

            if (getMoveCapture(moveList->moves[count]) == 13) {
                historyMoves[getMovePiece(moveList->moves[count])][getMoveTarget(moveList->moves[count])] += depth;
            }

            alpha = score;

            pvTable[ply][ply] = moveList->moves[count];
            for (int nextPly = ply + 1; nextPly < pvLength[ply + 1]; nextPly++) {
                pvTable[ply][nextPly] = pvTable[ply + 1][nextPly]; // Copy the PV line from the next ply to the current ply
            }
            pvLength[ply] = pvLength[ply + 1];

            // Fail hard beta-cutoff
            if (score >= beta) {
                if (excludedMove == 0) {
                    writeHashEntry(beta, bestMove, depth, hashFlagBeta);
                }
                if (getMoveCapture(moveList->moves[count]) == 13) {
                    killerMoves[1][ply] = killerMoves[0][ply];
                    killerMoves[0][ply] = moveList->moves[count];
                }
                return beta;
            }
        }
    }

    if (legalMoves == 0) {
        if (inCheck) {
            return -mateValue + ply;
        }
        else {
            return 0;
        }
    }

    if (excludedMove == 0) {
        writeHashEntry(alpha, bestMove, depth, hashFlag);
    }

    // Node fails low
    return alpha;
}

void Search::searchPosition(int depth, int threadId) {
    int start = Chessboard::getTimeMs();
    int score = 0;
    Chessboard::nodes = 0;
    Chessboard::stopped = false;
    followPv = false;
    scorePv = false;

    memset(killerMoves, 0, sizeof(killerMoves));
    memset(historyMoves, 0, sizeof(historyMoves));
    memset(pvTable, 0, sizeof(pvTable));
    memset(pvLength, 0, sizeof(pvLength));
    
    int alpha = -infinity;
    int beta = infinity;

    if (Chessboard::useBook) {
        Reader::BookMoves bookMoves = Chessboard::book.GetBookMoves(Chessboard::polyKeyFromBoard());

        if (bookMoves.size() > 0) {
            std::string move = Reader::ConvertBookMoveToUci(Reader::RandomBookMove(bookMoves));
            move = move == "e1h1" ? "e1g1" : move;
            move = move == "e8h8" ? "e8g8" : move;
            move = move == "e1a1" ? "e1c1" : move;
            move = move == "e8a8" ? "e8c8" : move;
            if (threadId == 0) {
                std::cout << "bestmove " << move << std::endl;
            }
            return;
        }
    }

    // Iterative Deepening
    for (int currentDepth = 1; currentDepth <= depth; currentDepth++) {
        Chessboard::useBook = false;
        if (Chessboard::stopped) {
            break;
        }

        followPv = true;

        score = negamax(alpha, beta, currentDepth);

        // Aspiration Windows
        if ((score <= alpha) || (score >= beta)) {
            alpha = -infinity;
            beta = infinity;
            continue;
        }
        alpha = score - 50;
        beta = score + 50;

        if (pvLength[0]) {
            if (threadId == 0) {
                if (score > -mateValue && score < -mateScore) {
                    std::cout << "info score mate " << -(score + mateValue) / 2 - 1 << " depth " << currentDepth << " nodes " << Chessboard::nodes << " time " << Chessboard::getTimeMs() - start << " pv ";
                }
                else if (score > mateScore && score < mateValue) {
                    std::cout << "info score mate " << (mateValue - score) / 2 + 1 << " depth " << currentDepth << " nodes " << Chessboard::nodes << " time " << Chessboard::getTimeMs() - start << " pv ";
                }
                else {
                    std::cout << "info score cp " << score << " depth " << currentDepth << " nodes " << Chessboard::nodes << " time " << Chessboard::getTimeMs() - Chessboard::startTime << " pv ";
                }

                for (int count = 0; count < pvLength[0]; count++) {
                    Move::printMove(pvTable[0][count]);
                    std::cout << " ";
                }
                std::cout << std::endl;
            }
        }
    }

    if (threadId == 0) {
        if (pvTable[0][0]) {
            std::cout << "bestmove ";
            Move::printMove(pvTable[0][0]);
            std::cout << std::endl;
        }
        else {
            std::cout << "(none)" << std::endl;
        }
    }
    // std::cout << std::endl;
}