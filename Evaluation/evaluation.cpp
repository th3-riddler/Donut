#include <iostream>

#include "evaluation.hpp"

// int Evaluation::nnuePieces[12] = { 6, 5, 4, 3, 2, 1, 12, 11, 10, 9, 8, 7 };
int Evaluation::nnuePieces[12] = { 1, 2, 3, 4, 5, 6, 9, 10, 11, 12, 13, 14 };

const int Evaluation::mvvLva[12][12] = {
    105, 205, 305, 405, 505, 605,  105, 205, 305, 405, 505, 605,
	104, 204, 304, 404, 504, 604,  104, 204, 304, 404, 504, 604,
	103, 203, 303, 403, 503, 603,  103, 203, 303, 403, 503, 603,
	102, 202, 302, 402, 502, 602,  102, 202, 302, 402, 502, 602,
	101, 201, 301, 401, 501, 601,  101, 201, 301, 401, 501, 601,
	100, 200, 300, 400, 500, 600,  100, 200, 300, 400, 500, 600,

	105, 205, 305, 405, 505, 605,  105, 205, 305, 405, 505, 605,
	104, 204, 304, 404, 504, 604,  104, 204, 304, 404, 504, 604,
	103, 203, 303, 403, 503, 603,  103, 203, 303, 403, 503, 603,
	102, 202, 302, 402, 502, 602,  102, 202, 302, 402, 502, 602,
	101, 201, 301, 401, 501, 601,  101, 201, 301, 401, 501, 601,
	100, 200, 300, 400, 500, 600,  100, 200, 300, 400, 500, 600
};

const int Evaluation::pieceValues[14] = {
    // P, N, B, R, Q, K, p, n, b, r, q, k, empty, empty
    100, 300, 300, 500, 900, 20000,
    100, 300, 300, 500, 900, 20000,
    0, 0
};

uint64_t Evaluation::getAttackers(int targetSquare, uint64_t occupied) {
    uint64_t attackers = 0ULL;
    
    // White pawns attacking targetSquare
    attackers |= Move::pawnAttacks[Chessboard::black][targetSquare] & Chessboard::bitboard.bitboards[Chessboard::P];
    // Black pawns attacking targetSquare
    attackers |= Move::pawnAttacks[Chessboard::white][targetSquare] & Chessboard::bitboard.bitboards[Chessboard::p];
    
    // Knights
    attackers |= Move::knightAttacks[targetSquare] & (Chessboard::bitboard.bitboards[Chessboard::N] | Chessboard::bitboard.bitboards[Chessboard::n]);
    
    // Kings
    attackers |= Move::kingAttacks[targetSquare] & (Chessboard::bitboard.bitboards[Chessboard::K] | Chessboard::bitboard.bitboards[Chessboard::k]);
    
    // Bishops and Queens (diagonal)
    uint64_t bishopsQueens = Chessboard::bitboard.bitboards[Chessboard::B] | Chessboard::bitboard.bitboards[Chessboard::b] | Chessboard::bitboard.bitboards[Chessboard::Q] | Chessboard::bitboard.bitboards[Chessboard::q];
    attackers |= Move::getBishopAttacks(targetSquare, occupied) & bishopsQueens;
    
    // Rooks and Queens (orthogonal)
    uint64_t rooksQueens = Chessboard::bitboard.bitboards[Chessboard::R] | Chessboard::bitboard.bitboards[Chessboard::r] | Chessboard::bitboard.bitboards[Chessboard::Q] | Chessboard::bitboard.bitboards[Chessboard::q];
    attackers |= Move::getRookAttacks(targetSquare, occupied) & rooksQueens;
    
    return attackers;
}

int Evaluation::getLeastValuableAttacker(uint64_t attackers, int side, int& pieceType) {
    int startPiece = (side == Chessboard::white) ? Chessboard::P : Chessboard::p;
    int endPiece = (side == Chessboard::white) ? Chessboard::K : Chessboard::k;
    
    for (int piece = startPiece; piece <= endPiece; piece++) {
        uint64_t subset = attackers & Chessboard::bitboard.bitboards[piece];
        if (subset) {
            pieceType = piece;
            return Chessboard::getLSBIndex(subset);
        }
    }
    
    return -1;
}

int Evaluation::see(int move) {
    int targetSquare = getMoveTarget(move);
    int startSquare = getMoveSource(move);
    int capturedPiece = getMoveCapture(move);
    int attackerPiece = getMovePiece(move);
    int attackerColor = Chessboard::bitboard.sideToMove;
    
    int gain[32];
    int depth = 0;
    
    gain[depth] = (capturedPiece == 13) ? 0 : pieceValues[capturedPiece];
    
    uint64_t occupied = Chessboard::bitboard.occupancies[Chessboard::both];
    uint64_t currentAttackers = getAttackers(targetSquare, occupied);
    
    CLEAR_BIT(occupied, startSquare);
    
    attackerColor ^= 1;
    
    while (depth < 31) {
        depth++;
        
        int nextAttackerSquare = getLeastValuableAttacker(currentAttackers, attackerColor, attackerPiece);
        
        if (nextAttackerSquare == -1) break;
        
        gain[depth] = pieceValues[attackerPiece] - gain[depth - 1];
        
        CLEAR_BIT(currentAttackers, nextAttackerSquare);
        CLEAR_BIT(occupied, nextAttackerSquare);
        
        // Add X-Ray attackers. Make sure they are not the same square we just removed!
        uint64_t bishopsQueens = Chessboard::bitboard.bitboards[Chessboard::B] | Chessboard::bitboard.bitboards[Chessboard::b] | Chessboard::bitboard.bitboards[Chessboard::Q] | Chessboard::bitboard.bitboards[Chessboard::q];
        uint64_t rooksQueens = Chessboard::bitboard.bitboards[Chessboard::R] | Chessboard::bitboard.bitboards[Chessboard::r] | Chessboard::bitboard.bitboards[Chessboard::Q] | Chessboard::bitboard.bitboards[Chessboard::q];
        
        uint64_t xRayDiagonal = Move::getBishopAttacks(targetSquare, occupied) & bishopsQueens;
        uint64_t xRayOrthogonal = Move::getRookAttacks(targetSquare, occupied) & rooksQueens;
        
        currentAttackers |= (xRayDiagonal | xRayOrthogonal) & occupied; // Solo pezzi ancora presenti!
        
        attackerColor ^= 1;
    }
    
    while (--depth) {
        gain[depth - 1] = -std::max(-gain[depth - 1], gain[depth]);
    }
    
    return gain[0];
}

/*  =======================
         Move ordering
    =======================
    
    1. PV move
    2. Captures in MVV/LVA
    3. 1st killer move
    4. 2nd killer move
    5. History moves
    6. Unsorted moves
*/

int Evaluation::scoreMove(int move) {
    if (Search::scorePv) {
        if (Search::pvTable[0][Search::ply] == move) {
            Search::scorePv = false;
            return 20000;
        }
    }

    if(getMoveCapture(move) != 13) {
        if (Evaluation::see(move) >= 0) {
            return mvvLva[getMovePiece(move)][getMoveCapture(move)] + 10000;
        } else {
            return mvvLva[getMovePiece(move)][getMoveCapture(move)] + 5000;
        }
    }
    else {
        if (Search::killerMoves[0][Search::ply] == move) {
            return 9000;
        }
        else if (Search::killerMoves[1][Search::ply] == move) {
            return 8000;
        }
        else {
            return Search::historyMoves[getMovePiece(move)][getMoveTarget(move)];
        }
    }
    return 0;
}

int Evaluation::evaluate() {
    
    int pieceAmount = 0;

    int pieces[32];
    int squares[32];
    int index = 0;

    for (int square = 0; square < 64; square++) {
        
        for (int piece = Chessboard::P; piece <= Chessboard::k; piece++) {
            if (!GET_BIT(Chessboard::bitboard.bitboards[piece], square)) {
                continue;
            }

            pieces[index] = nnuePieces[piece];
            squares[index] = square;
            index++;
            pieceAmount++;
        }
    }

    // int scoree = (evaluate_nnue(Chessboard::bitboard.sideToMove, pieces, squares) * (100 - Search::fifty) / 100);
    // std::cout << "Score Evalaute(): " << scoree << std::endl;

    // print pieces and squares
    // std::cout << sizeof(pieces) << std::endl;
    // for (int i = 0; i < pieceAmount; i++) {
    //     std::cout << pieces[i] << " ";
    // }
    // std::cout << std::endl;
    // for (int i = 0; i < pieceAmount; i++) {
    //     std::cout << squares[i] << " ";
    // }
    // std::cout << std::endl;
    // std::cout << "Piece Amount: " << pieceAmount << std::endl;

    // std::cout << "Fifty: " << Search::fifty << std::endl;
    bool side = Chessboard::bitboard.sideToMove ^ 1;
    // std::cout << "Score: " << Stockfish::Probe::eval(pieces, squares, pieceAmount, side, Search::fifty) << std::endl;
    return evaluate_nnue(pieces, squares, pieceAmount, side, Search::fifty);
}