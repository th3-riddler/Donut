#include "chessboard.hpp"

/*
TODO:
- Change the way occupancies bitboards are updated (change them in the same time the pieces are moved and not regenerating them every time)
- Change the generateMoves function to generate all legal moves in one go
*/

int const Chessboard::polyPieces[12] = { 1, 3, 5, 7, 9, 11, 0, 2, 4, 6, 8, 10 };

char Chessboard::asciiPieces[13] = "PNBRQKpnbrqk";
const char* Chessboard::squareToCoordinates[64] = {
    "a1", "b1", "c1", "d1", "e1", "f1", "g1", "h1",
    "a2", "b2", "c2", "d2", "e2", "f2", "g2", "h2",
    "a3", "b3", "c3", "d3", "e3", "f3", "g3", "h3",
    "a4", "b4", "c4", "d4", "e4", "f4", "g4", "h4",
    "a5", "b5", "c5", "d5", "e5", "f5", "g5", "h5",
    "a6", "b6", "c6", "d6", "e6", "f6", "g6", "h6",
    "a7", "b7", "c7", "d7", "e7", "f7", "g7", "h7",
    "a8", "b8", "c8", "d8", "e8", "f8", "g8", "h8"
};
const char* Chessboard::unicodePieces[13] = {
    "♙", "♘", "♗", "♖", "♕", "♔",
    "♟", "♞", "♝", "♜", "♛", "♚"
};

int Chessboard::charPieces[128] = {};
inline void Chessboard::initCharPieces() {
    charPieces['P'] = P;
    charPieces['N'] = N;
    charPieces['B'] = B;
    charPieces['R'] = R;
    charPieces['Q'] = Q;
    charPieces['K'] = K;
    charPieces['p'] = p;
    charPieces['n'] = n;
    charPieces['b'] = b;
    charPieces['r'] = r;
    charPieces['q'] = q;
    charPieces['k'] = k;
}

bool Chessboard::quit = false;
int Chessboard::movesToGo = 30;
int Chessboard::moveTime = -1;
int Chessboard::time = -1;
int Chessboard::inc = 0;
int Chessboard::startTime = 0;
int Chessboard::stopTime = 0;
int Chessboard::timeSet = 0;
int Chessboard::optTime = 0;
int Chessboard::maxTime = 0;
std::atomic<bool> Chessboard::stopped{false};
thread_local BitBoard Chessboard::bitboard;
Chessboard::ThreadStats Chessboard::threadStats[256];

uint64_t Chessboard::getNodes() {
    uint64_t sum = 0;
    for (int i = 0; i < threadCount; i++) {
        sum += threadStats[i].nodes;
    }
    return sum;
}

uint64_t Chessboard::pieceKeys[12][64];
uint64_t Chessboard::enPassantKeys[64];
uint64_t Chessboard::castleKeys[16];
uint64_t Chessboard::sideKey;
thread_local uint64_t Chessboard::hashKey = 0ULL;
thread_local int Chessboard::threadId = 0;
BitBoard Chessboard::rootBitboard;
uint64_t Chessboard::rootHashKey = 0ULL;

Reader::Book Chessboard::book;
bool Chessboard::useBook = true;

std::vector<Chessboard::Thread*> Chessboard::threads;
int Chessboard::threadCount = 1;

void Chessboard::copyState() {
    bitboard = rootBitboard;
    hashKey = rootHashKey;

    Search::fifty = 0;
    Search::repetitionIndex = 0;
    Search::ply = 0;

    memset(Search::killerMoves, 0, sizeof(Search::killerMoves));
    memset(Search::historyMoves, 0, sizeof(Search::historyMoves));
    memset(Search::pvTable, 0, sizeof(Search::pvTable));
    memset(Search::pvLength, 0, sizeof(Search::pvLength));
}

void Chessboard::searchWorker(Thread* thread) {
    Chessboard::threadId = thread->id;
    while (!thread->shouldQuit) {
        {
            std::unique_lock<std::mutex> lock(thread->mutex);
            thread->cv.wait(lock, [thread]{ return thread->isSearching || thread->shouldQuit; });

            if (thread->shouldQuit) {
                break;
            }
        }

        copyState();
        Search::searchPosition(thread->depth, thread->id);

        {
            std::unique_lock<std::mutex> lock(thread->mutex);
            thread->isSearching = false;
        }
        thread->cv.notify_one();
    }
}

void Chessboard::init() {

    initCharPieces();
    initRandomKeys();

    NNUE::init("nn-b1a57edbea57.nnue", "nn-baff1ede1f90.nnue");

    book.Load("cerebellum.bin");

    Search::initLMRTable();
    Search::initHashTable(64); // Default value of 64MB

    bool debug = false;

    if (debug) {
        parseFEN("rnbqkbnr/p1pppppp/8/8/P6P/R1p5/1P1PPPP1/1NBQKBNR b Kkq - 0 4");
        printBoard();
        // std::cout << Evaluation::evaluate() << std::endl;

        // int evalScore = evaluate_fen_nnue("rnbqkb1r/p3p1pp/5n2/8/2BPp3/8/1P3PPP/RNBQK2R w KQkq - 1 10");
        // std::cout << "Eval score: " << evalScore << std::endl;
    }
    else {
        uciLoop();
    }
    
}

bool Chessboard::canPawnEnPassant() {
    int targetPawn = bitboard.sideToMove == white ? P : p;
    if(bitboard.enPassantSquare != noSquare) {
        int squareWithPawn = bitboard.sideToMove == white ? bitboard.enPassantSquare - 8 : bitboard.enPassantSquare + 8;
        if (GET_BIT(bitboard.bitboards[targetPawn], (squareWithPawn + 1)) || GET_BIT(bitboard.bitboards[targetPawn], (squareWithPawn - 1))) {
            return true;
        }
    }
    return false;
}

uint64_t Chessboard::polyKeyFromBoard() {
    
    int rank, file, polyPiece, offset;
    uint64_t finalKey = 0ULL;

    for (int square = 0; square < 64; square++) {
        for (int piece = P; piece <= k; piece++) {
            if (!GET_BIT(bitboard.bitboards[piece], square)) {
                continue;
            }

            polyPiece = polyPieces[piece];
            rank = square / 8;
            file = square % 8;
            finalKey ^= Polyglot::Random64Poly[(64 * polyPiece) + (8 * rank) + file];
        }
    }

    // Castling Key
    offset = 768;
    if (bitboard.castlingRights & wk) finalKey ^= Polyglot::Random64Poly[offset + 0];
    if (bitboard.castlingRights & wq) finalKey ^= Polyglot::Random64Poly[offset + 1];
    if (bitboard.castlingRights & bk) finalKey ^= Polyglot::Random64Poly[offset + 2];
    if (bitboard.castlingRights & bq) finalKey ^= Polyglot::Random64Poly[offset + 3];

    // En passant Key
    offset = 772;
    if (canPawnEnPassant()) {
        file = bitboard.enPassantSquare % 8;
        finalKey ^= Polyglot::Random64Poly[offset + file];
    }

    // Side Key
    if (bitboard.sideToMove == white) {
        finalKey ^= Polyglot::Random64Poly[780];
    }
    
    return finalKey;
}

void Chessboard::initRandomKeys() {

    for (int piece = P; piece <= k; piece++) {
        for (int square = 0; square < 64; square++) {
            pieceKeys[piece][square] = Move::getRandomU64Number();
        }
    }

    for (int square = 0; square < 64; square++) {
        enPassantKeys[square] = Move::getRandomU64Number();
    }

    for (int index = 0; index < 16; index++) {
        castleKeys[index] = Move::getRandomU64Number();
    }

    sideKey = Move::getRandomU64Number();
}

uint64_t Chessboard::generateHashKey() {
    uint64_t finalKey = 0ULL;
    uint64_t tempBitboard;

    for (int piece = P; piece <= k; piece++) {
        tempBitboard = bitboard.bitboards[piece];

        while (tempBitboard) {
            int square = getLSBIndex(tempBitboard);

            finalKey ^= pieceKeys[piece][square];

            CLEAR_BIT(tempBitboard, square);
        }
    }

    if (bitboard.enPassantSquare != noSquare) {
        finalKey ^= enPassantKeys[bitboard.enPassantSquare];
    }

    finalKey ^= castleKeys[bitboard.castlingRights];

    if (bitboard.sideToMove == black) {
        finalKey ^= sideKey;
    }

    return finalKey;
}

void Chessboard::resetBoard() {
    // Reset board position and state variables
    memset(bitboard.bitboards, 0ULL, sizeof(bitboard.bitboards));
    memset(bitboard.occupancies, 0ULL, sizeof(bitboard.occupancies));

    bitboard.sideToMove = 0;
    bitboard.enPassantSquare = Chessboard::noSquare;
    bitboard.castlingRights = 0;

    Search::repetitionIndex = 0;

    Search::fifty = 0;

    memset(Search::repetitionTable, 0ULL, sizeof(Search::repetitionTable));
}

// Parse the FEN string
void Chessboard::parseFEN(char *fen) {

    resetBoard();

    for (int rank = 0; rank < 8; rank++){
        for (int file = 0; file < 8; file++){
            int square = (7 - rank) * 8 + file;

            if ((*fen >= 'a' && *fen <= 'z') || (*fen >= 'A' && *fen <= 'Z')) {
                int piece = charPieces[*fen];
                
                SET_BIT(bitboard.bitboards[piece], square);

                fen++;
            }

            if (*fen >= '0' && *fen <= '9') {
                int offset = *fen - '0'; // Convert the character '0' to integer 0

                int piece = -1;

                for (int bbPiece = P; bbPiece <= k; bbPiece++) {
                    if (GET_BIT(bitboard.bitboards[bbPiece], square)) {
                        piece = bbPiece;
                    }
                }

                if (piece == -1)
                    file--;

                file += offset;
                fen++;
            }

            if (*fen == '/') {
                fen++;
            }
        }
    }
    // Parse side to move
    fen++;
    bitboard.sideToMove = (*fen == 'w') ? white : black;

    // Parse castling rights
    fen += 2;
    while (*fen != ' ') {
        switch (*fen) {
            case 'K': bitboard.castlingRights |= wk; break;
            case 'Q': bitboard.castlingRights |= wq; break;
            case 'k': bitboard.castlingRights |= bk; break;
            case 'q': bitboard.castlingRights |= bq; break;
            case '-': break;
        }
        fen++;
    }

    // Parse en passant square
    fen++;

    if (*fen != '-') {
        int file = fen[0] - 'a';
        int rank = 8 - (fen[1] - '0');

        bitboard.enPassantSquare = (7 - rank) * 8 + file;
    }
    else {
        bitboard.enPassantSquare = Chessboard::noSquare;
    }

    fen++;

    Search::fifty = atoi(fen);

    // Initialize white occupancies
    for (int piece = P; piece <= K; piece++) {
        bitboard.occupancies[white] |= bitboard.bitboards[piece];
    }

    // Initialize black occupancies
    for (int piece = p; piece <= k; piece++) {
        bitboard.occupancies[black] |= bitboard.bitboards[piece];
    }

    // Initialize both occupancies
    bitboard.occupancies[both] = bitboard.occupancies[white] | bitboard.occupancies[black];

    // Initialize hash key
    hashKey = generateHashKey();
}

// Print the given bitboard
void Chessboard::printBitboards(uint64_t bitboard) {
    std::cout << std::endl;
    for (int rank = 0; rank < 8; rank++) {
        for (int file = 0; file < 8; file++) {
            int square = (7 - rank) * 8 + file; // Calcola la posizione del bit

            if (!file)
                std::cout << "  " << 8 - rank << "  ";
                
            std::cout << ((bitboard & (1ULL << square)) ? "1 " : ". ");
        }
        std::cout << std::endl;
    }
    std::cout << "\n     a b c d e f g h\n" << std::endl;

    std::cout << "Bitboard: " << bitboard << "\n" << std::endl;
}

// Print the current board position
void Chessboard::printBoard() {
    std::cout << std::endl;
    for (int rank = 0; rank < 8; rank++) {
        for (int file = 0; file < 8; file++) {
            int square = (7 - rank) * 8 + file;

            if (!file)
                std::cout << "  " << 8 - rank << "  ";

            int piece = -1;

            for (int bbPiece = P; bbPiece <= k; bbPiece++) {
                if (GET_BIT(bitboard.bitboards[bbPiece], square)) {
                    piece = bbPiece;
                }
            }

            if (piece == -1) {
                std::cout << ". ";
            }
            else {
                std::cout << unicodePieces[piece] << " ";
            }
        }
        std::cout << std::endl;
    }
    std::cout << "\n     a b c d e f g h\n" << std::endl;

    std::cout << "Side to move:         " << (!bitboard.sideToMove ? "White" : "Black") << std::endl;

    std::cout << "En passant square:    " << (bitboard.enPassantSquare != noSquare ? squareToCoordinates[bitboard.enPassantSquare] : "no") << std::endl;

    std::cout << "Castling:             " << (bitboard.castlingRights & wk ? "K" : "-") << (bitboard.castlingRights & wq ? "Q" : "-") << (bitboard.castlingRights & bk ? "k" : "-") << (bitboard.castlingRights & bq ? "q" : "-") << std::endl;

    std::cout << "Hash Key:             " << std::hex << hashKey << std::dec << std::endl;

    std::cout << "Polyglot Hash Key:    " << std::hex << polyKeyFromBoard() << std::dec << std::endl;

    std::cout << "Fifty:                " << Search::fifty << std::endl;

    std::cout << std::endl;
}


/*
               Binary Move Bits                                         Hexidecimal Constants                          

        000 0000 0000 0000 0000 0011 1111 --> source square                 0x3F
        000 0000 0000 0000 1111 1100 0000 --> target square                 0xFC0
        000 0000 0000 1111 0000 0000 0000 --> piece                         0xF000
        000 0000 1111 0000 0000 0000 0000 --> promoted piece                0xF0000
        000 1111 0000 0000 0000 0000 0000 --> captured piece                0xF00000
        001 0000 0000 0000 0000 0000 0000 --> double pawn push flag         0x1000000
        010 0000 0000 0000 0000 0000 0000 --> en passant flag               0x2000000
        100 0000 0000 0000 0000 0000 0000 --> castling flag                 0x4000000
*/

// Generate all moves
void Chessboard::generateMoves(moves *moveList, bool capturesOnly) {
    moveList->count = 0;

    int sourceSquare, targetSquare, targetPiece = -1, startPiece, endPiece;

    uint64_t bitboardCopy, attacks;

    int outerStart = (bitboard.sideToMove == white) ? P : p;
    int outerEnd = (bitboard.sideToMove == white) ? K : k;

    uint64_t targetOccupancy = capturesOnly ? 
        ((bitboard.sideToMove == white) ? bitboard.occupancies[black] : bitboard.occupancies[white]) :
        ((bitboard.sideToMove == white) ? ~bitboard.occupancies[white] : ~bitboard.occupancies[black]);

    for (int piece = outerStart; piece <= outerEnd; piece++) {
        bitboardCopy = bitboard.bitboards[piece];

        // Generate White Pawn Moves and White King Castling Moves
        if (bitboard.sideToMove == white) {
            if (piece == P) {
                while (bitboardCopy) {
                    sourceSquare = getLSBIndex(bitboardCopy);
                    targetSquare = sourceSquare + 8;

                    // Generate Quite Pawn Moves
                    if (!capturesOnly) {
                        if (!(targetSquare > h8) && !GET_BIT(bitboard.occupancies[both], targetSquare)) {
                            // Pawn Promotion
                            if (sourceSquare >= a7 && sourceSquare <= h7) {
                                Move::addMove(moveList, encodeMove(sourceSquare, targetSquare, piece, Q, 13, 0, 0, 0));
                                Move::addMove(moveList, encodeMove(sourceSquare, targetSquare, piece, R, 13, 0, 0, 0));
                                Move::addMove(moveList, encodeMove(sourceSquare, targetSquare, piece, B, 13, 0, 0, 0));
                                Move::addMove(moveList, encodeMove(sourceSquare, targetSquare, piece, N, 13, 0, 0, 0));
                            }
                            else {
                                // One square pawn advance
                                Move::addMove(moveList, encodeMove(sourceSquare, targetSquare, piece, 0, 13, 0, 0, 0));

                                // Double square pawn advance
                                if ((sourceSquare >= a2 && sourceSquare <= h2) && !GET_BIT(bitboard.occupancies[both], targetSquare + 8)) {
                                    Move::addMove(moveList, encodeMove(sourceSquare, targetSquare + 8, piece, 0, 13, 1, 0, 0));
                                }
                            }
                        }
                    }

                    // Generate Capture Pawn Moves
                    attacks = Move::pawnAttacks[white][sourceSquare] & bitboard.occupancies[black];

                    while (attacks) {
                        targetSquare = getLSBIndex(attacks);

                        for (int bbPiece = p; bbPiece <= k; bbPiece++) {
                            if (GET_BIT(Chessboard::bitboard.bitboards[bbPiece], targetSquare)) {
                                targetPiece = bbPiece;
                                break;
                            }
                        }

                        // Pawn Promotion-Capture
                        if (sourceSquare >= a7 && sourceSquare <= h7) {
                            Move::addMove(moveList, encodeMove(sourceSquare, targetSquare, piece, Q, targetPiece, 0, 0, 0));
                            Move::addMove(moveList, encodeMove(sourceSquare, targetSquare, piece, R, targetPiece, 0, 0, 0));
                            Move::addMove(moveList, encodeMove(sourceSquare, targetSquare, piece, B, targetPiece, 0, 0, 0));
                            Move::addMove(moveList, encodeMove(sourceSquare, targetSquare, piece, N, targetPiece, 0, 0, 0));
                        }
                        else {
                            Move::addMove(moveList, encodeMove(sourceSquare, targetSquare, piece, 0, targetPiece, 0, 0, 0));
                        }

                        CLEAR_BIT(attacks, targetSquare);
                    }

                    // En passant captures
                    if (bitboard.enPassantSquare != noSquare) {
                        uint64_t enPassantAttacks = Move::pawnAttacks[white][sourceSquare] & (1ULL << bitboard.enPassantSquare);

                        if (enPassantAttacks) {
                            int enPassantTarget = getLSBIndex(enPassantAttacks);
                            Move::addMove(moveList, encodeMove(sourceSquare, enPassantTarget, piece, 0, p, 0, 1, 0));
                        }
                    }

                    // Pop LSB from bitboardCopy
                    CLEAR_BIT(bitboardCopy, sourceSquare);
                }
            }

            // Castling Moves
            if (piece == K && !capturesOnly) {
                // King Side Castling
                if (bitboard.castlingRights & wk) {
                    // Check if the squares between the king and rook are empty
                    if (!GET_BIT(bitboard.occupancies[both], f1) && !GET_BIT(bitboard.occupancies[both], g1)) {
                        // Check if the squares the king moves through are not under attack
                        if (!isSquareAttacked(e1, black) && !isSquareAttacked(f1, black)) {
                            Move::addMove(moveList, encodeMove(e1, g1, piece, 0, 13, 0, 0, 1));
                        }
                    }
                }

                // Queen Side Castling
                if (bitboard.castlingRights & wq) {
                    // Check if the squares between the king and rook are empty
                    if (!GET_BIT(bitboard.occupancies[both], d1) && !GET_BIT(bitboard.occupancies[both], c1) && !GET_BIT(bitboard.occupancies[both], b1)) {
                        // Check if the squares the king moves through are not under attack
                        if (!isSquareAttacked(e1, black) && !isSquareAttacked(d1, black)) {
                            Move::addMove(moveList, encodeMove(e1, c1, piece, 0, 13, 0, 0, 1));
                        }
                    }
                }
            }
        }
        else { // Generate Black Pawn Moves and Black King Castling Moves
            if (piece == p) {
                while (bitboardCopy) {
                    sourceSquare = getLSBIndex(bitboardCopy);
                    targetSquare = sourceSquare - 8;

                    // Generate Quite Pawn Moves
                    if (!capturesOnly) {
                        if (!(targetSquare < a1) && !GET_BIT(bitboard.occupancies[both], targetSquare)) {
                            // Pawn Promotion
                            if (sourceSquare >= a2 && sourceSquare <= h2) {
                                Move::addMove(moveList, encodeMove(sourceSquare, targetSquare, piece, q, 13, 0, 0, 0));
                                Move::addMove(moveList, encodeMove(sourceSquare, targetSquare, piece, r, 13, 0, 0, 0));
                                Move::addMove(moveList, encodeMove(sourceSquare, targetSquare, piece, b, 13, 0, 0, 0));
                                Move::addMove(moveList, encodeMove(sourceSquare, targetSquare, piece, n, 13, 0, 0, 0));
                            }
                            else {
                                // One square pawn advance
                                Move::addMove(moveList, encodeMove(sourceSquare, targetSquare, piece, 0, 13, 0, 0, 0));

                                // Double square pawn advance
                                if ((sourceSquare >= a7 && sourceSquare <= h7) && !GET_BIT(bitboard.occupancies[both], targetSquare - 8)) {
                                    Move::addMove(moveList, encodeMove(sourceSquare, targetSquare - 8, piece, 0, 13, 1, 0, 0));
                                }
                            }
                        }
                    }

                    // Generate Capture Pawn Moves
                    attacks = Move::pawnAttacks[black][sourceSquare] & bitboard.occupancies[white];

                    while (attacks) {
                        targetSquare = getLSBIndex(attacks);

                        for (int bbPiece = P; bbPiece <= K; bbPiece++) {
                            if (GET_BIT(bitboard.bitboards[bbPiece], targetSquare)) {
                                targetPiece = bbPiece;
                                break;
                            }
                        }

                        // Pawn Promotion-Capture
                        if (sourceSquare >= a2 && sourceSquare <= h2) {
                            Move::addMove(moveList, encodeMove(sourceSquare, targetSquare, piece, q, targetPiece, 0, 0, 0));
                            Move::addMove(moveList, encodeMove(sourceSquare, targetSquare, piece, r, targetPiece, 0, 0, 0));
                            Move::addMove(moveList, encodeMove(sourceSquare, targetSquare, piece, b, targetPiece, 0, 0, 0));
                            Move::addMove(moveList, encodeMove(sourceSquare, targetSquare, piece, n, targetPiece, 0, 0, 0));
                        }
                        else {
                            Move::addMove(moveList, encodeMove(sourceSquare, targetSquare, piece, 0, targetPiece, 0, 0, 0));
                        }

                        CLEAR_BIT(attacks, targetSquare);
                    }

                    // En passant captures
                    if (bitboard.enPassantSquare != noSquare) {
                        uint64_t enPassantAttacks = Move::pawnAttacks[black][sourceSquare] & (1ULL << bitboard.enPassantSquare);

                        if (enPassantAttacks) {
                            int enPassantTarget = getLSBIndex(enPassantAttacks);
                            Move::addMove(moveList, encodeMove(sourceSquare, enPassantTarget, piece, 0, P, 0, 1, 0));
                        }
                    }

                    // Pop LSB from bitboardCopy
                    CLEAR_BIT(bitboardCopy, sourceSquare);
                }
            }

            // Castling Moves
            if (piece == k && !capturesOnly) {
                // King Side Castling
                if (bitboard.castlingRights & bk) {
                    // Check if the squares between the king and rook are empty
                    if (!GET_BIT(bitboard.occupancies[both], f8) && !GET_BIT(bitboard.occupancies[both], g8)) {
                        // Check if the squares the king moves through are not under attack
                        if (!isSquareAttacked(e8, white) && !isSquareAttacked(f8, white)) {
                            Move::addMove(moveList, encodeMove(e8, g8, piece, 0, 13, 0, 0, 1));
                        }
                    }
                }

                // Queen Side Castling
                if (bitboard.castlingRights & bq) {
                    // Check if the squares between the king and rook are empty
                    if (!GET_BIT(bitboard.occupancies[both], d8) && !GET_BIT(bitboard.occupancies[both], c8) && !GET_BIT(bitboard.occupancies[both], b8)) {
                        // Check if the squares the king moves through are not under attack
                        if (!isSquareAttacked(e8, white) && !isSquareAttacked(d8, white)) {
                            Move::addMove(moveList, encodeMove(e8, c8, piece, 0, 13, 0, 0, 1));
                        }
                    }
                }
            }
        }

        // Generate Knight Moves
        if (piece == N || piece == n) {
            if (bitboard.sideToMove == white) { startPiece = p; endPiece = k; }
            else { startPiece = P; endPiece = K; }
            
            while (bitboardCopy) {
                sourceSquare = getLSBIndex(bitboardCopy);

                attacks = Move::knightAttacks[sourceSquare] & targetOccupancy;

                while (attacks) {
                    targetSquare = getLSBIndex(attacks);
                    
                    if (GET_BIT(((bitboard.sideToMove == white) ? bitboard.occupancies[black] : bitboard.occupancies[white]), targetSquare)) {
                        for (int bbPiece = startPiece; bbPiece <= endPiece; bbPiece++) {
                            if (GET_BIT(bitboard.bitboards[bbPiece], targetSquare)) {
                                targetPiece = bbPiece;
                                break;
                            }
                        }
                        Move::addMove(moveList, encodeMove(sourceSquare, targetSquare, piece, 0, targetPiece, 0, 0, 0));
                    }
                    else {
                        Move::addMove(moveList, encodeMove(sourceSquare, targetSquare, piece, 0, 13, 0, 0, 0));
                    }

                    CLEAR_BIT(attacks, targetSquare);
                }

                CLEAR_BIT(bitboardCopy, sourceSquare);
            }
        }

        // Generate Bishop Moves
        if (piece == B || piece == b) {
            if (bitboard.sideToMove == white) { startPiece = p; endPiece = k; }
            else { startPiece = P; endPiece = K; }

            while (bitboardCopy) {
                sourceSquare = getLSBIndex(bitboardCopy);

                attacks = Move::getBishopAttacks(sourceSquare, bitboard.occupancies[both]) & targetOccupancy;

                while (attacks) {
                    targetSquare = getLSBIndex(attacks);

                    if (GET_BIT(((bitboard.sideToMove == white) ? bitboard.occupancies[black] : bitboard.occupancies[white]), targetSquare)) {
                        for (int bbPiece = startPiece; bbPiece <= endPiece; bbPiece++) {
                            if (GET_BIT(bitboard.bitboards[bbPiece], targetSquare)) {
                                targetPiece = bbPiece;
                                break;
                            }
                        }
                        Move::addMove(moveList, encodeMove(sourceSquare, targetSquare, piece, 0, targetPiece, 0, 0, 0));
                    }
                    else {
                        Move::addMove(moveList, encodeMove(sourceSquare, targetSquare, piece, 0, 13, 0, 0, 0));
                    }

                    CLEAR_BIT(attacks, targetSquare);
                }

                CLEAR_BIT(bitboardCopy, sourceSquare);
            }
        }

        // Generate Rook Moves
        if (piece == R || piece == r) {
            if (bitboard.sideToMove == white) { startPiece = p; endPiece = k; }
            else { startPiece = P; endPiece = K; }
            
            while (bitboardCopy) {
                sourceSquare = getLSBIndex(bitboardCopy);

                attacks = Move::getRookAttacks(sourceSquare, bitboard.occupancies[both]) & targetOccupancy;

                while (attacks) {
                    targetSquare = getLSBIndex(attacks);

                    if (GET_BIT(((bitboard.sideToMove == white) ? bitboard.occupancies[black] : bitboard.occupancies[white]), targetSquare)) {
                        for (int bbPiece = startPiece; bbPiece <= endPiece; bbPiece++) {
                            if (GET_BIT(bitboard.bitboards[bbPiece], targetSquare)) {
                                targetPiece = bbPiece;
                                break;
                            }
                        }
                        Move::addMove(moveList, encodeMove(sourceSquare, targetSquare, piece, 0, targetPiece, 0, 0, 0));
                    }
                    else {
                        Move::addMove(moveList, encodeMove(sourceSquare, targetSquare, piece, 0, 13, 0, 0, 0));
                    }

                    CLEAR_BIT(attacks, targetSquare);
                }

                CLEAR_BIT(bitboardCopy, sourceSquare);
            }
        }

        // Generate Queen Moves
        if (piece == Q || piece == q) {
            if (bitboard.sideToMove == white) { startPiece = p; endPiece = k; }
            else { startPiece = P; endPiece = K; }

            while (bitboardCopy) {
                sourceSquare = getLSBIndex(bitboardCopy);

                attacks = Move::getQueenAttacks(sourceSquare, bitboard.occupancies[both]) & targetOccupancy;

                while (attacks) {
                    targetSquare = getLSBIndex(attacks);

                    if (GET_BIT(((bitboard.sideToMove == white) ? bitboard.occupancies[black] : bitboard.occupancies[white]), targetSquare)) {
                        for (int bbPiece = startPiece; bbPiece <= endPiece; bbPiece++) {
                            if (GET_BIT(bitboard.bitboards[bbPiece], targetSquare)) {
                                targetPiece = bbPiece;
                                break;
                            }
                        }
                        Move::addMove(moveList, encodeMove(sourceSquare, targetSquare, piece, 0, targetPiece, 0, 0, 0));
                    }
                    else {
                        Move::addMove(moveList, encodeMove(sourceSquare, targetSquare, piece, 0, 13, 0, 0, 0));
                    }

                    CLEAR_BIT(attacks, targetSquare);
                }

                CLEAR_BIT(bitboardCopy, sourceSquare);
            }
        }

        // Generate King Moves
        if (piece == K || piece == k) {
            if (bitboard.sideToMove == white) { startPiece = p; endPiece = k; }
            else { startPiece = P; endPiece = K; }

            while (bitboardCopy) {
                sourceSquare = getLSBIndex(bitboardCopy);

                attacks = Move::kingAttacks[sourceSquare] & targetOccupancy;

                while (attacks) {
                    targetSquare = getLSBIndex(attacks);

                    if (GET_BIT(((bitboard.sideToMove == white) ? bitboard.occupancies[black] : bitboard.occupancies[white]), targetSquare)) {
                        for (int bbPiece = startPiece; bbPiece <= endPiece; bbPiece++) {
                            if (GET_BIT(bitboard.bitboards[bbPiece], targetSquare)) {
                                targetPiece = bbPiece;
                                break;
                            }
                        }
                        Move::addMove(moveList, encodeMove(sourceSquare, targetSquare, piece, 0, targetPiece, 0, 0, 0));
                    }
                    else {
                        Move::addMove(moveList, encodeMove(sourceSquare, targetSquare, piece, 0, 13, 0, 0, 0));
                    }

                    CLEAR_BIT(attacks, targetSquare);
                }

                CLEAR_BIT(bitboardCopy, sourceSquare);
            }
        }
    }
}

// Make a Move on the board
int Chessboard::makeMove(int move, int moveFlag, UndoInfo& undo) {
    // Quite Moves
    if (moveFlag == allMoves) {
        
        undo.enPassantSquare = bitboard.enPassantSquare;
        undo.castlingRights = bitboard.castlingRights;
        undo.fifty = Search::fifty;
        undo.hashKey = hashKey;
        undo.capturedPiece = 13;

        int sourceSquare = getMoveSource(move);
        int targetSquare = getMoveTarget(move);
        int piece = getMovePiece(move);
        int promotedPiece = getMovePromoted(move);

        CLEAR_BIT(bitboard.bitboards[piece], sourceSquare);
        SET_BIT(bitboard.bitboards[piece], targetSquare);

        CLEAR_BIT(bitboard.occupancies[bitboard.sideToMove], sourceSquare);
        CLEAR_BIT(bitboard.occupancies[both], sourceSquare);

        hashKey ^= pieceKeys[piece][sourceSquare] ^ pieceKeys[piece][targetSquare];

        Search::fifty++;

        if (piece == P || piece == p) {
            Search::fifty = 0;
        }

        if (getMoveCapture(move) != 13) {
            Search::fifty = 0;
            int startPiece, endPiece;

            if (bitboard.sideToMove == white) {
                startPiece = p;
                endPiece = k;
            }
            else {
                startPiece = P;
                endPiece = K;
            }

            for (int bbPiece = startPiece; bbPiece <= endPiece; bbPiece++) {
                if (GET_BIT(bitboard.bitboards[bbPiece], targetSquare)) {
                    undo.capturedPiece = bbPiece;
                    CLEAR_BIT(bitboard.bitboards[bbPiece], targetSquare);
                    hashKey ^= pieceKeys[bbPiece][targetSquare];
                    CLEAR_BIT(bitboard.occupancies[bitboard.sideToMove ^ 1], targetSquare);
                    break;
                }
            }
        }

        SET_BIT(bitboard.occupancies[bitboard.sideToMove], targetSquare);
        SET_BIT(bitboard.occupancies[both], targetSquare);

        if (promotedPiece) {
            CLEAR_BIT(bitboard.bitboards[piece], targetSquare);
            SET_BIT(bitboard.bitboards[promotedPiece], targetSquare);
            hashKey ^= pieceKeys[piece][targetSquare] ^ pieceKeys[promotedPiece][targetSquare];
        }

        if (getMoveEnPassant(move)) {
            if (bitboard.sideToMove == white) {
                undo.capturedPiece = p;
                CLEAR_BIT(bitboard.bitboards[p], targetSquare - 8);
                hashKey ^= pieceKeys[p][targetSquare - 8];
                CLEAR_BIT(bitboard.occupancies[black], targetSquare - 8);
                CLEAR_BIT(bitboard.occupancies[both], targetSquare - 8);
            }
            else {
                undo.capturedPiece = P;
                CLEAR_BIT(bitboard.bitboards[P], targetSquare + 8);
                hashKey ^= pieceKeys[P][targetSquare + 8];
                CLEAR_BIT(bitboard.occupancies[white], targetSquare + 8);
                CLEAR_BIT(bitboard.occupancies[both], targetSquare + 8);
            }
        }
        
        if (bitboard.enPassantSquare != noSquare) {
            hashKey ^= enPassantKeys[bitboard.enPassantSquare];
        }

        bitboard.enPassantSquare = noSquare;

        if (getMoveDoublePush(move)) {
            if (bitboard.sideToMove == white) {
                bitboard.enPassantSquare = targetSquare - 8;
                hashKey ^= enPassantKeys[targetSquare - 8];
            }
            else {
                bitboard.enPassantSquare = targetSquare + 8;
                hashKey ^= enPassantKeys[targetSquare + 8];
            }
        }

        if (getMoveCastling(move)) {
            switch (targetSquare) {
                case (g1):
                    CLEAR_BIT(bitboard.bitboards[R], h1);
                    SET_BIT(bitboard.bitboards[R], f1);
                    hashKey ^= pieceKeys[R][h1] ^ pieceKeys[R][f1];
                    CLEAR_BIT(bitboard.occupancies[white], h1);
                    SET_BIT(bitboard.occupancies[white], f1);
                    CLEAR_BIT(bitboard.occupancies[both], h1);
                    SET_BIT(bitboard.occupancies[both], f1);
                    break;
                case (c1):
                    CLEAR_BIT(bitboard.bitboards[R], a1);
                    SET_BIT(bitboard.bitboards[R], d1);
                    hashKey ^= pieceKeys[R][a1] ^ pieceKeys[R][d1];
                    CLEAR_BIT(bitboard.occupancies[white], a1);
                    SET_BIT(bitboard.occupancies[white], d1);
                    CLEAR_BIT(bitboard.occupancies[both], a1);
                    SET_BIT(bitboard.occupancies[both], d1);
                    break;
                case (g8):
                    CLEAR_BIT(bitboard.bitboards[r], h8);
                    SET_BIT(bitboard.bitboards[r], f8);
                    hashKey ^= pieceKeys[r][h8] ^ pieceKeys[r][f8];
                    CLEAR_BIT(bitboard.occupancies[black], h8);
                    SET_BIT(bitboard.occupancies[black], f8);
                    CLEAR_BIT(bitboard.occupancies[both], h8);
                    SET_BIT(bitboard.occupancies[both], f8);
                    break;
                case (c8):
                    CLEAR_BIT(bitboard.bitboards[r], a8);
                    SET_BIT(bitboard.bitboards[r], d8);
                    hashKey ^= pieceKeys[r][a8] ^ pieceKeys[r][d8];
                    CLEAR_BIT(bitboard.occupancies[black], a8);
                    SET_BIT(bitboard.occupancies[black], d8);
                    CLEAR_BIT(bitboard.occupancies[both], a8);
                    SET_BIT(bitboard.occupancies[both], d8);
                    break;
            }
        }

        hashKey ^= castleKeys[bitboard.castlingRights];
        bitboard.castlingRights &= Move::castlingRightsMask[sourceSquare] & Move::castlingRightsMask[targetSquare];
        hashKey ^= castleKeys[bitboard.castlingRights];

        bitboard.sideToMove ^= 1; // Switch side to move
        hashKey ^= sideKey;

        if (isSquareAttacked(getLSBIndex(((bitboard.sideToMove == white) ? bitboard.bitboards[k] : bitboard.bitboards[K])), bitboard.sideToMove)) {
            unmakeMove(move, undo);
            return 0;
        }
        else {
            return 1;
        }
    }
    else { // Capture Moves
        if (getMoveCapture(move) != 13) {
            return makeMove(move, allMoves, undo);
        }
        else {
            return 0;
        }
    }
    std::cout << "Invalid Move!" << std::endl;
    return 0;
}

void Chessboard::unmakeMove(int move, const UndoInfo& undo) {
    int sourceSquare = getMoveSource(move);
    int targetSquare = getMoveTarget(move);
    int piece = getMovePiece(move);
    int promotedPiece = getMovePromoted(move);

    bitboard.sideToMove ^= 1; // Revert side to move

    if (promotedPiece) {
        CLEAR_BIT(bitboard.bitboards[promotedPiece], targetSquare);
        SET_BIT(bitboard.bitboards[piece], targetSquare);
    }

    CLEAR_BIT(bitboard.bitboards[piece], targetSquare);
    SET_BIT(bitboard.bitboards[piece], sourceSquare);

    CLEAR_BIT(bitboard.occupancies[bitboard.sideToMove], targetSquare);
    SET_BIT(bitboard.occupancies[bitboard.sideToMove], sourceSquare);
    CLEAR_BIT(bitboard.occupancies[both], targetSquare);
    SET_BIT(bitboard.occupancies[both], sourceSquare);

    if (getMoveEnPassant(move)) {
        if (bitboard.sideToMove == white) {
            SET_BIT(bitboard.bitboards[undo.capturedPiece], targetSquare - 8);
            SET_BIT(bitboard.occupancies[black], targetSquare - 8);
            SET_BIT(bitboard.occupancies[both], targetSquare - 8);
        } else {
            SET_BIT(bitboard.bitboards[undo.capturedPiece], targetSquare + 8);
            SET_BIT(bitboard.occupancies[white], targetSquare + 8);
            SET_BIT(bitboard.occupancies[both], targetSquare + 8);
        }
    } else if (undo.capturedPiece != 13) {
        SET_BIT(bitboard.bitboards[undo.capturedPiece], targetSquare);
        SET_BIT(bitboard.occupancies[bitboard.sideToMove ^ 1], targetSquare);
        SET_BIT(bitboard.occupancies[both], targetSquare);
    }

    if (getMoveCastling(move)) {
        switch (targetSquare) {
            case (g1):
                CLEAR_BIT(bitboard.bitboards[R], f1);
                SET_BIT(bitboard.bitboards[R], h1);
                CLEAR_BIT(bitboard.occupancies[white], f1);
                SET_BIT(bitboard.occupancies[white], h1);
                CLEAR_BIT(bitboard.occupancies[both], f1);
                SET_BIT(bitboard.occupancies[both], h1);
                break;
            case (c1):
                CLEAR_BIT(bitboard.bitboards[R], d1);
                SET_BIT(bitboard.bitboards[R], a1);
                CLEAR_BIT(bitboard.occupancies[white], d1);
                SET_BIT(bitboard.occupancies[white], a1);
                CLEAR_BIT(bitboard.occupancies[both], d1);
                SET_BIT(bitboard.occupancies[both], a1);
                break;
            case (g8):
                CLEAR_BIT(bitboard.bitboards[r], f8);
                SET_BIT(bitboard.bitboards[r], h8);
                CLEAR_BIT(bitboard.occupancies[black], f8);
                SET_BIT(bitboard.occupancies[black], h8);
                CLEAR_BIT(bitboard.occupancies[both], f8);
                SET_BIT(bitboard.occupancies[both], h8);
                break;
            case (c8):
                CLEAR_BIT(bitboard.bitboards[r], d8);
                SET_BIT(bitboard.bitboards[r], a8);
                CLEAR_BIT(bitboard.occupancies[black], d8);
                SET_BIT(bitboard.occupancies[black], a8);
                CLEAR_BIT(bitboard.occupancies[both], d8);
                SET_BIT(bitboard.occupancies[both], a8);
                break;
        }
    }

    bitboard.enPassantSquare = undo.enPassantSquare;
    bitboard.castlingRights = undo.castlingRights;
    Search::fifty = undo.fifty;
    hashKey = undo.hashKey;
}

// Detect if the given square is under attack by the given color
bool Chessboard::isSquareAttacked(int square, int side) {
    
    // Attacked by White Pawns
    if ((side == white) && (Move::pawnAttacks[black][square] & bitboard.bitboards[P])) return true; 

    // Attacked by Black Pawns
    if ((side == black) && (Move::pawnAttacks[white][square] & bitboard.bitboards[p])) return true;

    // Attacked by Knights
    if ((Move::knightAttacks[square] & ((side == white) ? bitboard.bitboards[N] : bitboard.bitboards[n]))) return true;

    // Attacked by Kings
    if ((Move::kingAttacks[square] & ((side == white) ? bitboard.bitboards[K] : bitboard.bitboards[k]))) return true;

    // Attacked by Bishops
    if (Move::getBishopAttacks(square, bitboard.occupancies[both]) & ((side == white) ? bitboard.bitboards[B] : bitboard.bitboards[b])) return true;

    // Attacked by Rooks
    if (Move::getRookAttacks(square, bitboard.occupancies[both]) & ((side == white) ? bitboard.bitboards[R] : bitboard.bitboards[r])) return true;

    // Attacked by Queens
    if (Move::getQueenAttacks(square, bitboard.occupancies[both]) & ((side == white) ? bitboard.bitboards[Q] : bitboard.bitboards[q])) return true;
    
    return false;
}

// Print attacked squares
void Chessboard::printAttackedSquares(int side) {
    for (int rank = 0; rank < 8; rank++) {
        for (int file = 0; file < 8; file++) {
            int square = (7 - rank) * 8 + file;

            if (!file)
                std::cout << "  " << 8 - rank << "  ";
            
            std::cout << (isSquareAttacked(square, side) ? "X " : ". ");
        }
        std::cout << std::endl;
    }
    std::cout << "\n     a b c d e f g h\n" << std::endl;
}

int Chessboard::getTimeMs() {
    struct timeval timeValue;
    gettimeofday(&timeValue, NULL);
    return timeValue.tv_sec * 1000 + timeValue.tv_usec / 1000;
}

inline void Chessboard::perftDriver(int depth) {
    if (depth == 0) {
        threadStats[threadId].nodes++;
        return;
    }

    moves moveList[1];
    generateMoves(moveList);

    moves capList[1];
    generateMoves(capList, true);
    
    // Test capturesOnly parity
    int filteredCapCount = 0;
    for (int count = 0; count < moveList->count; count++) {
        if (getMoveCapture(moveList->moves[count]) != 13) {
            filteredCapCount++;
            // Check if this move exists in capList
            bool found = false;
            for (int j = 0; j < capList->count; j++) {
                if (moveList->moves[count] == capList->moves[j]) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                std::cout << "CAPTURE PARITY BUG! Generated capture not in capturesOnly list!" << std::endl;
                exit(1);
            }
        }
    }
    
    int actualCapCount = capList->count;
    if (filteredCapCount != actualCapCount) {
        std::cout << "CAPTURE PARITY BUG! counts mismatch: filtered=" << filteredCapCount << " capList=" << actualCapCount << std::endl;
        exit(1);
    }

    for (int moveCount = 0; moveCount < moveList->count; moveCount++) {

        UndoInfo undo;

        if (!makeMove(moveList->moves[moveCount], allMoves, undo)) {
            continue; 
        }

        perftDriver(depth - 1);
        unmakeMove(moveList->moves[moveCount], undo);

        // uint64_t hashFromScratch = generateHashKey();

        // if (hashKey != hashFromScratch) {
        //     std::cout << "\nTAKE BACK\n" << std::endl;
        //     std::cout << "move: ";
        //     Move::printMove(moveList->moves[moveCount]);
        //     std::cout << std::endl;
        //     printBoard();
        //     std::cout << "Hash Key should be: " << std::hex << hashFromScratch << std::dec << std::endl; 
        //     getchar();
        // }
    }
}

void Chessboard::perftTest(int depth) {
    std::cout << "\n    Performance Test\n" << std::endl;

    moves moveList[1];
    generateMoves(moveList);

    int start = getTimeMs();
    for (int moveCount = 0; moveCount < moveList->count; moveCount++) {

        UndoInfo undo;

        if (!makeMove(moveList->moves[moveCount], allMoves, undo)) {
            continue; 
        }

        long commulativeNodes = getNodes();
        
        perftDriver(depth - 1);

        long oldNodes = getNodes() - commulativeNodes;

        unmakeMove(moveList->moves[moveCount], undo);

        std::cout << "  Move: ";
        std::cout << squareToCoordinates[getMoveSource(moveList->moves[moveCount])] << squareToCoordinates[getMoveTarget(moveList->moves[moveCount])] << Move::promotedPieces[getMovePromoted(moveList->moves[moveCount])] << 
                     "  Nodes: " << oldNodes << std::endl;
    }
    std::cout << std::endl;
    std::cout << "  Depth: " << depth << std::endl <<
                 "  Nodes: " << getNodes() << std::endl <<
                 "  Time: " << getTimeMs() - start << "ms" << std::endl << std::endl;
}

int Chessboard::parseMove(char *moveString) {

    moves moveList[1];
    generateMoves(moveList);

    int sourceSquare = (moveString[0] - 'a') + (7 - (8 - (moveString[1] - '0'))) * 8;
    int targetSquare = (moveString[2] - 'a') + (7 - (8 - (moveString[3] - '0'))) * 8;

    for (int moveCount = 0; moveCount < moveList->count; moveCount++) {
        int move = moveList->moves[moveCount];

        if (sourceSquare == getMoveSource(move) && targetSquare == getMoveTarget(move)) {
            int promotedPiece = getMovePromoted(move);

            if (promotedPiece) {
                if (moveString[4] == Move::promotedPieces[promotedPiece]) {
                    return move;
                }
                continue;
            }
            return move;
        }
    }
    return 0;
}

void Chessboard::parsePosition(char *command) {
    command += 9;
    char *currentChar = command;

    // Parse "startpos" command
    if(strncmp(command, "startpos", 8) == 0) {
        // Initialize board to starting position
        parseFEN(startPosition);
    }
    else {
        // Parse FEN string
        currentChar = strstr(command, "fen");

        if (currentChar == NULL) {
            parseFEN(startPosition);
        }
        else {
            currentChar += 4;
            
            // Check if there are moves, if yes, we null-terminate the FEN string temporarily
            char *movesPtr = strstr(command, "moves");
            if (movesPtr != NULL) {
                *(movesPtr - 1) = '\0'; // Replace the space before "moves" with null terminator
                parseFEN(currentChar);
                *(movesPtr - 1) = ' ';  // Restore the space
            } else {
                parseFEN(currentChar);
            }
        }
    }

    // Parse moves
    currentChar = strstr(command, "moves");

    if (currentChar != NULL) {
        currentChar += 6;

        while(*currentChar) {
            int move = parseMove(currentChar);

            if (move == 0) {
                break;
            }

            Search::repetitionIndex++;
            Search::repetitionTable[Search::repetitionIndex] = hashKey;

            UndoInfo undo;
            makeMove(move, allMoves, undo);

            while (*currentChar && *currentChar != ' ') {
                currentChar++;
            }
            currentChar++;
        }
    }

    // printBoard();
}

void Chessboard::resetTimeControl() {
    quit = false;
    movesToGo = 30;
    moveTime = -1;
    time = -1;
    inc = 0;
    startTime = 0;
    stopTime = 0;
    timeSet = false;
    stopped = false;
}

void Chessboard::parseGo(char *command) {
    resetTimeControl();

    int depth = -1;
    char *argument = NULL;

    if ((argument = strstr(command, "infinite"))) {}

    if ((argument = strstr(command, "binc")) && bitboard.sideToMove == black) {
        inc = atoi(argument + 5);
    }

    if ((argument = strstr(command, "winc")) && bitboard.sideToMove == white) {
        inc = atoi(argument + 5);
    }

    if ((argument = strstr(command, "wtime")) && bitboard.sideToMove == white) {
        time = atoi(argument + 6);
    }

    if ((argument = strstr(command, "btime")) && bitboard.sideToMove == black) {
        time = atoi(argument + 6);
    }

    if ((argument = strstr(command, "movestogo"))) {
        movesToGo = atoi(argument + 10);
    }

    if ((argument = strstr(command, "movetime"))) {
        moveTime = atoi(argument + 9);
    }
    
    if ((argument = strstr(command, "depth"))) {
        depth = atoi(argument + 6);
    }
    
    if ((argument = strstr(command, "perft"))) {
        depth = atoi(argument + 6);
        for (int i = 0; i < threadCount; i++) {
            threadStats[i].nodes = 0;
        }
        perftTest(depth);
        return;
    }
    
    if (moveTime != -1) {
        time = moveTime;
        time -= 50;
        movesToGo = 1;
    }

    startTime = getTimeMs();

    depth = depth;

    if (time != -1 || moveTime != -1) {
        timeSet = true;

        int moveOverhead = 40;
        
        if (moveTime != -1) {
            optTime = moveTime;
            maxTime = moveTime;
        } else {
            int divider = (movesToGo != 30) ? movesToGo : 40;
            optTime = time / divider + (inc * 3 / 4) - moveOverhead;
            maxTime = time / 5 + inc - moveOverhead;
        }
        
        if (optTime < 1) { optTime = 1; }
        if (maxTime < 1) { maxTime = 1; }

        if (time != -1 && moveTime == -1) {
            int timeRemaining = time - moveOverhead;
            if (timeRemaining < 1) { timeRemaining = 1; }
            if (optTime > timeRemaining) { optTime = timeRemaining; }
            if (maxTime > timeRemaining) { maxTime = timeRemaining; }
        }

        
        stopTime = startTime + maxTime;
    }

    if (depth == -1) {
        depth = 64;
    }

    rootBitboard = bitboard;
    rootHashKey = hashKey;

    // std::cout << "time: " << time << " inc: " << inc << " start: " << startTime << " stop: " << stopTime << " depth: " << depth << " timeset: " << timeSet << std::endl;

    // Wake up worker threads
    for (int i = 0; i < threadCount - 1; i++) {
        if (i >= (int)threads.size()) {
            Thread* t = new Thread();
            t->id = i + 1;
            t->isSearching = false;
            t->shouldQuit = false;
            t->depth = depth;
            t->thread = new std::thread(searchWorker, t);
            threads.push_back(t);
        }

        Thread* t = threads[i];
        
        // Wait until previous search is effectively over
        while (t->isSearching) {
            std::this_thread::yield();
        }
        
        std::unique_lock<std::mutex> lock(t->mutex);
        t->depth = depth;
        t->isSearching = true;
        t->cv.notify_one();
    }

    // Master thread runs the main search
    Search::searchPosition(depth, 0);

    // Wait for all worker threads to finish
    for (int i = 0; i < threadCount - 1; i++) {
        Thread* t = threads[i];
        while (t->isSearching) {
            std::this_thread::yield();
        }
    }
}

int Chessboard::inputWaiting() {
    fd_set readfds;
    struct timeval tv;
    FD_ZERO(&readfds);
    FD_SET(fileno(stdin), &readfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    select(16, &readfds, 0, 0, &tv);

    return (FD_ISSET(fileno(stdin), &readfds));
}

void Chessboard::readInput() {
    int bytes;
    char input[256] = "", *endc;

    if (inputWaiting()) {
        do {
            bytes = read(fileno(stdin), input, 256);
        }
        while (bytes < 0);

        if (bytes == 0) {
            quit = true;
            stopped = true;
            return; // EOF
        }

        endc = strchr(input, '\n');

        if (endc) {
            *endc = 0;
        }

        if (strlen(input) > 0) {
            if (!strncmp(input, "quit", 4)) {
                quit = true;
                stopped = true;
            }
            else if (!strncmp(input, "stop", 4)) {
                stopped = true;
            }
        }
    }
}

void Chessboard::communicate() {
    if (threadId == 0) {
        if (timeSet == true && getTimeMs() > stopTime) {
            stopped = true;
        }

        readInput();
    }
}

/*
    GUI Commands
        - "uci" --> "uciok"
        - "isready" --> "readyok"
        - "ucinewgame"
    
*/

void Chessboard::uciLoop() {

    int maxHash = 512;
    int mb = 64;
    int moveOverhead = 0;
    threadCount = 1;
    int maxThreads = 256;
    std::string syzygyPath = "";
    bool uciShowWDL = false;

    // Reset STDIN and STDOUT buffers
    setbuf(stdin, NULL);
    setbuf(stdout, NULL);

    // Define User / GUI input buffer
    char input[2000];

    while (true) {
        // Reset input buffer
        memset(input, 0, sizeof(input));
        fflush(stdout);

        if(!fgets(input, 2000, stdin)) {
            if (feof(stdin)) {
                quit = true;
                stopped = true;
                for (auto t : threads) {
                    {
                        std::unique_lock<std::mutex> lock(t->mutex);
                        t->shouldQuit = true;
                        t->cv.notify_one();
                    }
                    if (t->thread->joinable()) {
                        t->thread->join();
                    }
                    delete t->thread;
                    delete t;
                }
                threads.clear();
                break;
            }
            continue;
        }

        if (input[0] == '\n') {
            continue;
        }

        if (strncmp(input, "isready", 7) == 0) {
            std::cout << "readyok" << std::endl;
            continue;
        }

        else if (strncmp(input, "position", 8) == 0) {
            parsePosition(input);
            Search::clearTranspositionTable();
        }

        else if (strncmp(input, "ucinewgame", 10) == 0) {
            Chessboard::useBook = true;
            parsePosition("position startpos");
            Search::clearTranspositionTable();
        }

        else if (strncmp(input, "go", 2) == 0) {
            parseGo(input);
        }

        else if (strncmp(input, "quit", 4) == 0) {
            quit = true;
            stopped = true;
            for (auto t : threads) {
                {
                    std::unique_lock<std::mutex> lock(t->mutex);
                    t->shouldQuit = true;
                    t->cv.notify_one();
                }
                t->thread->join();
                delete t->thread;
                delete t;
            }
            threads.clear();
            break;
        }

        else if (strncmp(input, "uci", 3) == 0) {
            std::cout << "id name Donut" << std::endl;
            std::cout << "id author Redux" << std::endl;
            std::cout << "option name Move Overhead type spin default " << moveOverhead << " min 0 max 0" << std::endl;
            std::cout << "option name Threads type spin default " << threadCount << " min 1 max " << maxThreads << std::endl;
            std::cout << "option name Hash type spin default 64 min 4 max " << maxHash << std::endl;
            std::cout << "option name SyzygyPath type string default \"" << syzygyPath << "\"" << std::endl;
            std::cout << "option name UCI_ShowWDL type check default false" << std::endl;
            std::cout << "option name OwnBook type check default true" << std::endl;
            std::cout << "uciok" << std::endl;
        }

        else if (strncmp(input, "setoption name Move Overhead value ", 34) == 0) {
            // moveOverhead = atoi(input + 34);
            // std::cout << "info string MoveOverhead set to " << moveOverhead << std::endl;
            continue;
        }

        else if (strncmp(input, "setoption name Threads value ", 29) == 0) {
            threadCount = atoi(input + 29);
            if (threadCount < 1) { threadCount = 1; }
            if (threadCount > maxThreads) { threadCount = maxThreads; }
            // std::cout << "info string Threads set to " << threadCount << std::endl;
            continue;
        }

        // else if (!strncmp(input, "setoption name Hash value ", 26)) {			
        //     // init MB
        //     sscanf(input,"%*s %*s %*s %*s %d", &mb);
            
        //     // adjust MB if going beyond the aloowed bounds
        //     if(mb < 4) mb = 4;
        //     if(mb > maxHash) mb = maxHash;
            
        //     // set hash table size in MB
        //     std::cout << "    Set hash table size to " << mb << "MB" << std::endl;
        //     Search::initHashTable(mb);
        //     continue;
        // }

        else if (strncmp(input, "setoption name Hash value ", 26) == 0) {
            sscanf(input, "%*s %*s %*s %*s %d", &mb);
            if (mb < 4) { mb = 4; }
            if (mb > maxHash) { 
                std::cerr << "info string Hash value too large, set to max of " << maxHash << " MB" << std::endl;
                mb = maxHash;
            }

            // std::cout << "info string Hash set to " << mb << "MB" << std::endl;
            Search::initHashTable(mb);
            continue;
        }

        else if (strncmp(input, "setoption name SyzygyPath value ", 31) == 0) {
            // syzygyPath = std::string(input + 31);
            // std::cout << "info string SyzygyPath set to \"" << syzygyPath << "\"" << std::endl;
            continue;
        }

        else if (strncmp(input, "setoption name UCI_ShowWDL value ", 32) == 0) {
            // std::string value = std::string(input + 32);
            // if (value == "true") {
            //     uciShowWDL = true;
            //     std::cout << "info string UCI_ShowWDL is set to true, but this feature is not supported." << std::endl;
            // } else {
            //     uciShowWDL = false;
            // }
            // std::cout << "info string UCI_ShowWDL set to " << (uciShowWDL ? "true" : "false") << std::endl;
            continue;
        }
        else if (strncmp(input, "setoption name OwnBook value ", 29) == 0) {
            char *value = NULL;
            value = strstr(input, "true");
            if (value != NULL) {
                useBook = true;
            }
            else {
                useBook = false;
            }
        } 
        else if (strncmp(input, "d", 1) == 0) {
            printBoard();
        }
    }
}