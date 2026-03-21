#include <iostream>
#include <cmath>
#include <algorithm>

#include "search.hpp"
#include "../Evaluation/evaluation.hpp"
#include "../nnueEval/nnueEval.hpp"

thread_local int Search::ply;

thread_local int Search::killerMoves[2][64];
thread_local int Search::historyMoves[12][64];
thread_local int Search::pvLength[64];
thread_local int Search::pvTable[64][64];
thread_local int Search::playedMoves[maxPly];
thread_local int Search::counterMoves[12][64];
thread_local int Search::followUpMoves[12][64][12][64];
thread_local int Search::captureHistory[12][64][14];
thread_local bool Search::followPv;
thread_local bool Search::scorePv;
const int Search::fullDepthMoves = 4;
const int Search::reductionLimit = 3;
int Search::hashEntries = 0;
tt* Search::transpositionTable = NULL;

int Search::lmrTable[64][64];

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
    for (int hashEntry = 0; hashEntry < hashEntries; hashEntry++) {
        transpositionTable[hashEntry].key = 0;
        transpositionTable[hashEntry].data = 0;
    }
}

void Search::initLMRTable() {
    for (int depth = 0; depth < 64; depth++) {
        for (int movesSearched = 0; movesSearched < 64; movesSearched++) {
            if (depth > 0 && movesSearched > 0) {
                Search::lmrTable[depth][movesSearched] = 1 + log(depth) * log(movesSearched) / 2.25;
            } else {
                Search::lmrTable[depth][movesSearched] = 0;
            }
        }
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
    uint64_t read_data = hashEntry->data;
    uint64_t read_key = hashEntry->key;

    if ((read_key ^ read_data) == Chessboard::hashKey) {
        int retrieved_depth = (read_data >> 46) & 0xFF;
        int retrieved_flags = (read_data >> 54) & 0xF;
        int retrieved_score = ((int)((read_data >> 28) & 0x3FFFF)) - 65000;
        *bestMove = (int)(read_data & 0xFFFFFFF);

        if (retrieved_depth >= depth) {
            int score = retrieved_score;

            if (score < -mateScore) { score += ply; }
            if (score > mateScore) { score -= ply; }

            if (retrieved_flags == hashFlagExact) {
                return score;
            }
            if ((retrieved_flags == hashFlagAlpha) && (score <= alpha)) {
                return alpha;
            }
            if ((retrieved_flags == hashFlagBeta) && (score >= beta)) {
                return beta;
            }
        }
    }

    return noHashEntry;
}

void Search::writeHashEntry(int score, int bestMove, int depth, int flag) {
    tt *hashEntry = &transpositionTable[Chessboard::hashKey % hashEntries];

    if (score < -mateScore) { score -= ply; }
    if (score > mateScore) { score += ply; }

    uint64_t data = 0;
    data |= (uint64_t)(bestMove & 0xFFFFFFF);
    data |= ((uint64_t)((score + 65000) & 0x3FFFF)) << 28;
    data |= ((uint64_t)(depth & 0xFF)) << 46;
    data |= ((uint64_t)(flag & 0xF)) << 54;
    
    hashEntry->data = data;
    hashEntry->key = Chessboard::hashKey ^ data;
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

    if ((Chessboard::threadStats[Chessboard::threadId].nodes & 2047) == 0) {
        Chessboard::communicate();
    }

    if (Chessboard::stopped) {
        return 0;
    }

    Chessboard::threadStats[Chessboard::threadId].nodes++;
    
    if (ply > maxPly - 1) {
        return Evaluation::evaluate();
    }

    // Se mangi il re, questo nodo è invalido e cut-off.
    // Dovrebbe già essere scremato da makeMove, ma come fail-safe o per i nodi root:
    if (Chessboard::getNodes() > 0) {
        int kingCount = Chessboard::countBits(Chessboard::bitboard.bitboards[Chessboard::K]) + Chessboard::countBits(Chessboard::bitboard.bitboards[Chessboard::k]);
        if (kingCount != 2) return mateValue - ply; 
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
    Chessboard::generateMoves(moveList, true);

    Move::sortMoves(moveList, 0);

    for (int count = 0; count < moveList->count; count++) {
        int captured = getMoveCapture(moveList->moves[count]);
        if (captured != Chessboard::K && captured != Chessboard::k) {
            if (Evaluation::see(moveList->moves[count]) < 0) {
                continue;
            }
        }

        Chessboard::UndoInfo undo;

        ply++;

        repetitionIndex++;
        repetitionTable[repetitionIndex] = Chessboard::hashKey;

        if(Chessboard::makeMove(moveList->moves[count], Chessboard::capturesOnly, undo) == 0) {
            ply--;

            repetitionIndex--;

            continue;
        }

        NNUE::push_move(moveList->moves[count], undo.capturedPiece);

        int score = -quiescenceSearch(-beta, -alpha);

        NNUE::pop_move(moveList->moves[count]);

        ply--;

        repetitionIndex--;

        Chessboard::unmakeMove(moveList->moves[count], undo);

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

    if ((Chessboard::threadStats[Chessboard::threadId].nodes & 2047) == 0) {
        Chessboard::communicate();
    }

    if (Chessboard::stopped) {
        return 0;
    }

    if (depth == 0) {
        return quiescenceSearch(alpha, beta);
    }

    if (ply > maxPly - 1) {
        return Evaluation::evaluate();
    }

    Chessboard::threadStats[Chessboard::threadId].nodes++;

    int inCheck = Chessboard::isSquareAttacked(Chessboard::getLSBIndex((Chessboard::bitboard.sideToMove == Chessboard::white ? 
                                                                        Chessboard::bitboard.bitboards[Chessboard::K] : Chessboard::bitboard.bitboards[Chessboard::k])), 
                                                                        Chessboard::bitboard.sideToMove ^ 1);

    if (inCheck) {
        depth++;
    }

    int legalMoves = 0;

    // Fail-safe per evitare di valutare su posizioni con re mancanti
    int kingCount = Chessboard::countBits(Chessboard::bitboard.bitboards[Chessboard::K]) + Chessboard::countBits(Chessboard::bitboard.bitboards[Chessboard::k]);
    if (kingCount != 2) return mateValue - ply;

    // Evaluation Pruning
    int staticEval = Evaluation::evaluate();

    if (depth < 3 && !pvNode && !inCheck && abs(beta - 1) > -infinity + 100) {
        int evalMargin = 120 * depth;

        if (staticEval - evalMargin >= beta) {
            return staticEval - evalMargin;
        }
    }

    // Null Move Pruning
    if (depth >= 3 && !inCheck && ply && abs(staticEval) < 4000) {
        Chessboard::UndoInfo nullUndo;
        nullUndo.enPassantSquare = Chessboard::bitboard.enPassantSquare;
        nullUndo.hashKey = Chessboard::hashKey;

        ply++;

        repetitionIndex++;
        repetitionTable[repetitionIndex] = Chessboard::hashKey;

        if (Chessboard::bitboard.enPassantSquare != Chessboard::noSquare) {
            Chessboard::hashKey ^= Chessboard::enPassantKeys[Chessboard::bitboard.enPassantSquare];
        }
        Chessboard::bitboard.enPassantSquare = Chessboard::noSquare;

        Chessboard::bitboard.sideToMove ^= 1;
        Chessboard::hashKey ^= Chessboard::sideKey;

        NNUE::push_null_move();

        // Dynamic Null Move Pruning (Asymptotic Formula)
        int R = 3 + depth / 6;
        int reducedDepth = std::max(0, depth - 1 - R);
        score = -negamax(-beta, -beta + 1, reducedDepth, 0);

        NNUE::pop_null_move();

        ply--;

        repetitionIndex--;

        Chessboard::bitboard.sideToMove ^= 1;
        Chessboard::bitboard.enPassantSquare = nullUndo.enPassantSquare;
        Chessboard::hashKey = nullUndo.hashKey;

        if (Chessboard::stopped) {
            return 0;
        }

        if (score >= beta) {
            return beta;
        }
    }

    // Razoring
    if (!pvNode && !inCheck && depth <= 3 && abs(staticEval) < 4000) {
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
        uint64_t read_data = hashEntry->data;
        uint64_t read_key = hashEntry->key;
        
        if ((read_key ^ read_data) == Chessboard::hashKey) {
            int retrieved_depth = (read_data >> 46) & 0xFF;
            int retrieved_flags = (read_data >> 54) & 0xF;
            int retrieved_score = ((int)((read_data >> 28) & 0x3FFFF)) - 65000;
            
            if (retrieved_depth >= depth - 3 && retrieved_flags != hashFlagAlpha && abs(retrieved_score) < mateScore) {
                int singularBeta = retrieved_score - depth * 2;
                int singularDepth = (depth - 1) / 2;
                int singularScore = negamax(singularBeta - 1, singularBeta, singularDepth, bestMove);
                if (singularScore < singularBeta) {
                    extension = 1;
                }
            }
        }
    }

    for (int count = 0; count < moveList->count; count++) {
        
        if (moveList->moves[count] == excludedMove) continue;

        Chessboard::UndoInfo undo;

        ply++;

        repetitionIndex++;
        repetitionTable[repetitionIndex] = Chessboard::hashKey;

        if(Chessboard::makeMove(moveList->moves[count], Chessboard::allMoves, undo) == 0) {
            ply--;

            repetitionIndex--;

            continue;
        }

        NNUE::push_move(moveList->moves[count], undo.capturedPiece);

        legalMoves++;
        playedMoves[ply] = moveList->moves[count];

        if (movesSearched == 0) {
            int moveExtension = (moveList->moves[count] == bestMove) ? extension : 0;
            score = -negamax(-beta, -alpha, depth - 1 + moveExtension, 0);
        }
        else {
            // Late Move Reduction (LMR) Table-Driven
            if (movesSearched >= fullDepthMoves && depth >= reductionLimit && !inCheck && (getMoveCapture(moveList->moves[count]) == 13) && getMovePromoted(moveList->moves[count]) == 0 && abs(staticEval) < 4000) {
                int reduction = lmrTable[std::min(depth, 63)][std::min(movesSearched, 63)];
                int reducedDepth = std::max(0, depth - 1 - reduction);
                score = -negamax(-alpha - 1, -alpha, reducedDepth, 0);
            }
            else {
                score = alpha + 1; // Forza il PVS
            }
            
            // Principal Variation Search (PVS) col check del re-search
            if (score > alpha) {
                score = -negamax(-alpha - 1, -alpha, depth - 1, 0);
            
                if ((score > alpha) && (score < beta)) {
                    score = -negamax(-beta, -alpha, depth - 1, 0);
                }
            }
        }

        NNUE::pop_move(moveList->moves[count]);

        ply--;

        repetitionIndex--;

        Chessboard::unmakeMove(moveList->moves[count], undo);

        if (Chessboard::stopped) {
            return 0;
        }

        movesSearched++;

        if (score > alpha) {
            hashFlag = hashFlagExact;

            bestMove = moveList->moves[count];

            if (getMoveCapture(moveList->moves[count]) == 13) {
                // Aggiornamento Esponenziale History Table Normale (Square-Piece)
                historyMoves[getMovePiece(moveList->moves[count])][getMoveTarget(moveList->moves[count])] += depth * depth;
                
                // Generazione della Memoria Lunga: Follow-Up History
                if (ply > 1 && playedMoves[ply - 2] != 0) {
                    int prevPrevMove = playedMoves[ply - 2];
                    followUpMoves[getMovePiece(prevPrevMove)][getMoveTarget(prevPrevMove)][getMovePiece(moveList->moves[count])][getMoveTarget(moveList->moves[count])] += depth * depth;
                }
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
                    if (ply > 0 && playedMoves[ply - 1] != 0) {
                        int prevMove = playedMoves[ply - 1];
                        counterMoves[getMovePiece(prevMove)][getMoveTarget(prevMove)] = moveList->moves[count];
                    }
                } else {
                    // Capture History update for cutoffs
                    captureHistory[getMovePiece(moveList->moves[count])][getMoveTarget(moveList->moves[count])][getMoveCapture(moveList->moves[count])] += depth * depth;
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
    for (int i = 0; i < Chessboard::threadCount; i++) {
        Chessboard::threadStats[i].nodes = 0;
    }
    Chessboard::stopped = false;
    followPv = false;
    scorePv = false;

    memset(killerMoves, 0, sizeof(killerMoves));
    memset(historyMoves, 0, sizeof(historyMoves));
    memset(playedMoves, 0, sizeof(playedMoves));
    memset(counterMoves, 0, sizeof(counterMoves));
    memset(followUpMoves, 0, sizeof(followUpMoves));
    memset(captureHistory, 0, sizeof(captureHistory));
    memset(pvTable, 0, sizeof(pvTable));
    memset(pvLength, 0, sizeof(pvLength));
    
    int alpha = -infinity;
    int beta = infinity;
    int bestMoveSoFar = 0;

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

    // Init NNUE state from scratch
    int pieceAmount = 0;
    int pieces[32];
    int squares[32];
    int index = 0;

    for (int square = 0; square < 64; square++) {
        for (int piece = Chessboard::P; piece <= Chessboard::k; piece++) {
            if (!GET_BIT(Chessboard::bitboard.bitboards[piece], square)) continue;
            pieces[index] = piece;
            squares[index] = square;
            index++;
            pieceAmount++;
        }
    }
    NNUE::set_state_from_pieces(pieces, squares, pieceAmount, Chessboard::bitboard.sideToMove == Chessboard::white ? true : false, Search::fifty);

    // Iterative Deepening
    for (int currentDepth = 1; currentDepth <= depth; currentDepth++) {
        Chessboard::useBook = false;
        if (currentDepth > 1 && Chessboard::stopped) {
            break;
        }

        if (Chessboard::timeSet) {
            int elapsed = Chessboard::getTimeMs() - start;
            if (Chessboard::optTime == Chessboard::maxTime) {
                if (elapsed > Chessboard::optTime) {
                    break;
                }
            } else {
                if (elapsed * 2 > Chessboard::optTime) {
                    break;
                }
            }
        }

        followPv = true;

        score = negamax(alpha, beta, currentDepth);

        // Aspiration Windows
        if ((score <= alpha) || (score >= beta)) {
            alpha = -infinity;
            beta = infinity;
            currentDepth--;
            continue;
        }
        alpha = score - 50;
        beta = score + 50;

        if (pvLength[0]) {
            bestMoveSoFar = pvTable[0][0];
            if (threadId == 0) {
                if (score > -mateValue && score < -mateScore) {
                    std::cout << "info score mate " << -(score + mateValue) / 2 - 1 << " depth " << currentDepth << " nodes " << Chessboard::getNodes() << " time " << Chessboard::getTimeMs() - start << " pv ";
                }
                else if (score > mateScore && score < mateValue) {
                    std::cout << "info score mate " << (mateValue - score) / 2 + 1 << " depth " << currentDepth << " nodes " << Chessboard::getNodes() << " time " << Chessboard::getTimeMs() - start << " pv ";
                }
                else {
                    std::cout << "info score cp " << score << " depth " << currentDepth << " nodes " << Chessboard::getNodes() << " time " << Chessboard::getTimeMs() - Chessboard::startTime << " pv ";
                }

                for (int count = 0; count < pvLength[0]; count++) {
                    Move::printMove(pvTable[0][count]);
                    std::cout << " ";
                }
                std::cout << std::endl;
            }
        }

        if (score > mateScore && score < mateValue) {
            break;
        }

        // --- Early exit for Forced Negative Checkmate ---
        // Se non c'è modo di evitare il matto, giochiamo la mossa migliore 
        // trovata subito senza perdere tempo prezioso di riflessione.
        if (score < -mateScore && score > -mateValue) {
            break;
        }

        // End loop
    }

    if (threadId == 0) {
        Chessboard::stopped = true; // Signal all worker threads to stop and prevent hangs
        
        if (pvTable[0][0]) {
            std::cout << "bestmove ";
            Move::printMove(pvTable[0][0]);
            std::cout << std::endl;
        }
        else if (bestMoveSoFar) {
            std::cout << "bestmove ";
            Move::printMove(bestMoveSoFar);
            std::cout << std::endl;
        }
        else {
            std::cout << "(none)" << std::endl;
        }
    }
    // std::cout << std::endl;
}