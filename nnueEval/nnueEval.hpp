#ifndef NNUE_EVAL_HPP
#define NNUE_EVAL_HPP

namespace NNUE {
    void init(const char *bigNetPath, const char *smallNetPath);
    void reset_state();
    void set_state_from_pieces(const int pieces[], const int squares[], int pieceAmount, bool side, int rule50);
    void push_move(int move, int capturedPiece);
    void pop_move(int move);
    void push_null_move();
    void pop_null_move();
    int evaluate(bool side, int rule50);
}

#endif // NNUE_EVAL_HPP