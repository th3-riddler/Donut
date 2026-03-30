#include "../nnue/probe.h"
#include "../nnue/position.h"
#include "../nnue/evaluate.h"
#include "../Chessboard/chessboard.hpp"
#include "../Search/search.hpp"
#include "nnueEval.hpp"

namespace NNUE {

    thread_local Stockfish::Position nnuePos;
    thread_local Stockfish::StateInfo nnueStates[256]; 
    thread_local int nnuePly = 0;

    const Stockfish::Piece pieceMapping[14] = {
        Stockfish::W_PAWN, Stockfish::W_KNIGHT, Stockfish::W_BISHOP, Stockfish::W_ROOK, Stockfish::W_QUEEN, Stockfish::W_KING,
        Stockfish::B_PAWN, Stockfish::B_KNIGHT, Stockfish::B_BISHOP, Stockfish::B_ROOK, Stockfish::B_QUEEN, Stockfish::B_KING,
        Stockfish::NO_PIECE, Stockfish::NO_PIECE
    };

    void init(const char *bigNetPath, const char *smallNetPath) {
        Stockfish::Probe::init(bigNetPath, smallNetPath);
    }

    void reset_state() {
        nnuePly = 0;
    }

    void set_state_from_pieces(const int pieces[], const int squares[], int pieceAmount, bool side, int rule50) {
        nnuePly = 0;
        
        int mappedPieces[32];
        for (int i = 0; i < pieceAmount; i++) {
            Stockfish::Piece pc = pieceMapping[pieces[i]];
            mappedPieces[i] = pc;
        }
        nnuePos.set(mappedPieces, squares, pieceAmount, side, rule50, &nnueStates[0]);
    }

    void push_move(int move, int capturedPiece) {
        int source = getMoveSource(move);
        int target = getMoveTarget(move);
        int piece = getMovePiece(move);
        int promotedPiece = getMovePromoted(move);
        bool isCastling = getMoveCastling(move);
        bool isEnPassant = getMoveEnPassant(move);

        Stockfish::StateInfo* prevSt = &nnueStates[nnuePly];
        nnuePly++;
        Stockfish::StateInfo* st = &nnueStates[nnuePly];

        std::memcpy(st, prevSt, sizeof(Stockfish::StateInfo));
        st->previous = prevSt;
        st->dirtyPiece.dirty_num = 0;
        
        // Very important for Accumulator to trigger evaluation
        st->accumulatorBig.computed[Stockfish::WHITE] = false;
        st->accumulatorBig.computed[Stockfish::BLACK] = false;
        st->accumulatorSmall.computed[Stockfish::WHITE] = false;
        st->accumulatorSmall.computed[Stockfish::BLACK] = false;

        // Update nonPawnMaterial and rule50_count
        st->rule50 = Search::fifty;
        if (Stockfish::type_of(pieceMapping[piece]) == Stockfish::PAWN || capturedPiece != 13) {
            st->rule50 = 0;
        }
        
        auto add_dirty = [&](Stockfish::Piece pc, Stockfish::Square from, Stockfish::Square to) {
            st->dirtyPiece.piece[st->dirtyPiece.dirty_num] = pc;
            st->dirtyPiece.from[st->dirtyPiece.dirty_num] = from;
            st->dirtyPiece.to[st->dirtyPiece.dirty_num] = to;
            st->dirtyPiece.dirty_num++;
        };

        Stockfish::Piece sfPiece = pieceMapping[piece];
        Stockfish::Square sfFrom = static_cast<Stockfish::Square>(source);
        Stockfish::Square sfTo = static_cast<Stockfish::Square>(target);

        nnuePos.remove_piece(sfFrom);

        Stockfish::Square capSq = sfTo;
        if (isEnPassant) {
            capSq = static_cast<Stockfish::Square>(piece == Chessboard::P ? target - 8 : target + 8);
        }
        
        if (capturedPiece != 13) {
            Stockfish::Piece sfCaptured = pieceMapping[capturedPiece];
            if (Stockfish::type_of(sfCaptured) != Stockfish::PAWN && Stockfish::type_of(sfCaptured) != Stockfish::KING) {
                st->nonPawnMaterial[Stockfish::color_of(sfCaptured)] -= Stockfish::PieceValue[sfCaptured];
            }
            nnuePos.remove_piece(capSq);
        }
        
        if (promotedPiece) {
            Stockfish::Piece sfPromoted = pieceMapping[promotedPiece];
            if (Stockfish::type_of(sfPromoted) != Stockfish::PAWN && Stockfish::type_of(sfPromoted) != Stockfish::KING) {
                st->nonPawnMaterial[Stockfish::color_of(sfPromoted)] += Stockfish::PieceValue[sfPromoted];
            }
            add_dirty(sfPiece, sfFrom, Stockfish::SQ_NONE);
            add_dirty(sfPromoted, Stockfish::SQ_NONE, sfTo);
            nnuePos.put_piece(sfPromoted, sfTo);
        } else {
            add_dirty(sfPiece, sfFrom, sfTo);
            nnuePos.put_piece(sfPiece, sfTo);
        }

        if (capturedPiece != 13) {
            Stockfish::Piece sfCaptured = pieceMapping[capturedPiece];
            add_dirty(sfCaptured, capSq, Stockfish::SQ_NONE);
        }

        if (isCastling) {
            Stockfish::Piece sfRook = pieceMapping[piece == Chessboard::K ? Chessboard::R : Chessboard::r];
            Stockfish::Square rookFrom, rookTo;
            if (target == Chessboard::g1) { rookFrom = static_cast<Stockfish::Square>(Chessboard::h1); rookTo = static_cast<Stockfish::Square>(Chessboard::f1); }
            else if (target == Chessboard::c1) { rookFrom = static_cast<Stockfish::Square>(Chessboard::a1); rookTo = static_cast<Stockfish::Square>(Chessboard::d1); }
            else if (target == Chessboard::g8) { rookFrom = static_cast<Stockfish::Square>(Chessboard::h8); rookTo = static_cast<Stockfish::Square>(Chessboard::f8); }
            else if (target == Chessboard::c8) { rookFrom = static_cast<Stockfish::Square>(Chessboard::a8); rookTo = static_cast<Stockfish::Square>(Chessboard::d8); }
            
            add_dirty(sfRook, rookFrom, rookTo);
            nnuePos.remove_piece(rookFrom);
            nnuePos.put_piece(sfRook, rookTo);
        }
        
        nnuePos.sideToMove = (nnuePos.sideToMove == Stockfish::WHITE ? Stockfish::BLACK : Stockfish::WHITE);
        nnuePos.st = st;
    }

    void pop_move(int move) {
        Stockfish::StateInfo* st = &nnueStates[nnuePly];
        
        for (int i = 0; i < st->dirtyPiece.dirty_num; i++) {
            Stockfish::Piece pc = st->dirtyPiece.piece[i];
            Stockfish::Square from = st->dirtyPiece.from[i];
            Stockfish::Square to = st->dirtyPiece.to[i];
            
            if (to != Stockfish::SQ_NONE) {
                nnuePos.remove_piece(to);
            }
            if (from != Stockfish::SQ_NONE) {
                nnuePos.put_piece(pc, from);
            }
        }

        nnuePly--;
        nnuePos.st = &nnueStates[nnuePly];
        nnuePos.sideToMove = (nnuePos.sideToMove == Stockfish::WHITE ? Stockfish::BLACK : Stockfish::WHITE);
    }

    void push_null_move() {
        Stockfish::StateInfo* prevSt = &nnueStates[nnuePly];
        nnuePly++;
        Stockfish::StateInfo* st = &nnueStates[nnuePly];
        std::memcpy(st, prevSt, sizeof(Stockfish::StateInfo));
        st->previous = prevSt;
        st->dirtyPiece.dirty_num = 0;
        st->accumulatorBig.computed[Stockfish::WHITE] = false;
        st->accumulatorBig.computed[Stockfish::BLACK] = false;
        st->accumulatorSmall.computed[Stockfish::WHITE] = false;
        st->accumulatorSmall.computed[Stockfish::BLACK] = false;
        st->rule50 = Search::fifty;
        
        nnuePos.sideToMove = (nnuePos.sideToMove == Stockfish::WHITE ? Stockfish::BLACK : Stockfish::WHITE);
        nnuePos.st = st;
    }

    void pop_null_move() {
        nnuePly--;
        nnuePos.st = &nnueStates[nnuePly];
        nnuePos.sideToMove = (nnuePos.sideToMove == Stockfish::WHITE ? Stockfish::BLACK : Stockfish::WHITE);
    }

    int evaluate(bool side, int rule50) {
        nnuePos.st->rule50 = rule50;
        return Stockfish::Eval::evaluate(nnuePos);
    }
}