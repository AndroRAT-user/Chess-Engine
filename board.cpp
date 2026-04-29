#include "board.h"

#include "movegen.h"

#include <algorithm>
#include <iomanip>
#include <mutex>
#include <random>
#include <sstream>

namespace {

std::once_flag zobristOnce;

struct ZobristTables {
    std::array<std::array<std::array<std::uint64_t, BOARD_SQUARES>, PIECE_TYPE_NB>, COLOR_NB> piece{};
    std::array<std::uint64_t, 16> castling{};
    std::array<std::uint64_t, 8> enPassant{};
    std::uint64_t side = 0;
};

ZobristTables g_zobrist;

std::uint64_t splitmix64(std::uint64_t& seed) {
    std::uint64_t value = (seed += 0x9E3779B97F4A7C15ULL);
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31);
}

void initializeZobrist() {
    std::uint64_t seed = 0xC0DEFACE12345678ULL;
    for (int color = 0; color < COLOR_NB; ++color) {
        for (int piece = 0; piece < PIECE_TYPE_NB; ++piece) {
            for (int square = 0; square < BOARD_SQUARES; ++square) {
                g_zobrist.piece[color][piece][square] = splitmix64(seed);
            }
        }
    }
    for (auto& key : g_zobrist.castling) {
        key = splitmix64(seed);
    }
    for (auto& key : g_zobrist.enPassant) {
        key = splitmix64(seed);
    }
    g_zobrist.side = splitmix64(seed);
}

const std::array<int, BOARD_SQUARES> kCastleMasks = [] {
    std::array<int, BOARD_SQUARES> masks{};
    masks.fill(WHITE_OO | WHITE_OOO | BLACK_OO | BLACK_OOO);

    masks[4] &= ~(WHITE_OO | WHITE_OOO);
    masks[0] &= ~WHITE_OOO;
    masks[7] &= ~WHITE_OO;

    masks[60] &= ~(BLACK_OO | BLACK_OOO);
    masks[56] &= ~BLACK_OOO;
    masks[63] &= ~BLACK_OO;

    return masks;
}();

char pieceToChar(int piece) {
    switch (piece) {
        case W_PAWN: return 'P';
        case W_KNIGHT: return 'N';
        case W_BISHOP: return 'B';
        case W_ROOK: return 'R';
        case W_QUEEN: return 'Q';
        case W_KING: return 'K';
        case B_PAWN: return 'p';
        case B_KNIGHT: return 'n';
        case B_BISHOP: return 'b';
        case B_ROOK: return 'r';
        case B_QUEEN: return 'q';
        case B_KING: return 'k';
        default: return '.';
    }
}

int pieceFromFenChar(char c) {
    switch (c) {
        case 'P': return W_PAWN;
        case 'N': return W_KNIGHT;
        case 'B': return W_BISHOP;
        case 'R': return W_ROOK;
        case 'Q': return W_QUEEN;
        case 'K': return W_KING;
        case 'p': return B_PAWN;
        case 'n': return B_KNIGHT;
        case 'b': return B_BISHOP;
        case 'r': return B_ROOK;
        case 'q': return B_QUEEN;
        case 'k': return B_KING;
        default: return NO_PIECE;
    }
}

}  // namespace

Board::Board() {
    std::call_once(zobristOnce, initializeZobrist);
    clear();
    setStartPos();
}

void Board::clear() {
    for (auto& colorSet : bitboards) {
        colorSet.fill(0ULL);
    }
    occupancies.fill(0ULL);
    squares.fill(NO_PIECE);
    sideToMove = WHITE;
    castlingRights = 0;
    enPassantSquare = NO_SQUARE;
    halfmoveClock = 0;
    fullmoveNumber = 1;
    hashKey = 0ULL;
    positionHistory.clear();
}

void Board::setStartPos() {
    setFen(STARTPOS_FEN);
}

bool Board::setFen(const std::string& fen) {
    clear();

    std::istringstream stream(fen);
    std::string boardPart;
    std::string sidePart;
    std::string castlePart;
    std::string epPart;

    if (!(stream >> boardPart >> sidePart >> castlePart >> epPart)) {
        return false;
    }

    if (!(stream >> halfmoveClock)) {
        halfmoveClock = 0;
    }
    if (!(stream >> fullmoveNumber)) {
        fullmoveNumber = 1;
    }

    int rank = 7;
    int file = 0;
    for (char c : boardPart) {
        if (c == '/') {
            --rank;
            file = 0;
            continue;
        }
        if (c >= '1' && c <= '8') {
            file += c - '0';
            continue;
        }

        const int piece = pieceFromFenChar(c);
        if (piece == NO_PIECE || file >= 8 || rank < 0) {
            return false;
        }

        const int square = rank * 8 + file;
        addPiece(piece_color(piece), piece_type(piece), square);
        ++file;
    }

    sideToMove = (sidePart == "b") ? BLACK : WHITE;

    castlingRights = 0;
    if (castlePart.find('K') != std::string::npos) castlingRights |= WHITE_OO;
    if (castlePart.find('Q') != std::string::npos) castlingRights |= WHITE_OOO;
    if (castlePart.find('k') != std::string::npos) castlingRights |= BLACK_OO;
    if (castlePart.find('q') != std::string::npos) castlingRights |= BLACK_OOO;

    enPassantSquare = (epPart == "-") ? NO_SQUARE : squareFromString(epPart);
    refreshOccupancies();
    hashKey = computeHashKey();
    positionHistory.push_back(hashKey);
    return true;
}

void Board::refreshOccupancies() {
    occupancies[WHITE] = 0ULL;
    occupancies[BLACK] = 0ULL;

    for (int piece = 0; piece < PIECE_TYPE_NB; ++piece) {
        occupancies[WHITE] |= bitboards[WHITE][piece];
        occupancies[BLACK] |= bitboards[BLACK][piece];
    }
    occupancies[2] = occupancies[WHITE] | occupancies[BLACK];
}

void Board::addPiece(int color, int pieceType, int square) {
    bitboards[color][pieceType] |= bit(square);
    squares[square] = make_piece(color, pieceType);
    hashKey ^= g_zobrist.piece[color][pieceType][square];
}

void Board::removePiece(int color, int pieceType, int square) {
    bitboards[color][pieceType] &= ~bit(square);
    squares[square] = NO_PIECE;
    hashKey ^= g_zobrist.piece[color][pieceType][square];
}

void Board::movePiece(int color, int pieceType, int from, int to) {
    removePiece(color, pieceType, from);
    addPiece(color, pieceType, to);
}

std::uint64_t Board::computeHashKey() const {
    std::uint64_t key = 0ULL;

    for (int square = 0; square < BOARD_SQUARES; ++square) {
        const int piece = squares[square];
        if (piece == NO_PIECE) {
            continue;
        }
        key ^= g_zobrist.piece[piece_color(piece)][piece_type(piece)][square];
    }

    key ^= g_zobrist.castling[castlingRights];
    if (enPassantSquare != NO_SQUARE) {
        key ^= g_zobrist.enPassant[file_of(enPassantSquare)];
    }
    if (sideToMove == BLACK) {
        key ^= g_zobrist.side;
    }

    return key;
}

bool Board::makeMove(Move move, UndoState& undo) {
    undo.move = move;
    undo.capturedPiece = NO_PIECE;
    undo.castlingRights = castlingRights;
    undo.enPassantSquare = enPassantSquare;
    undo.halfmoveClock = halfmoveClock;
    undo.fullmoveNumber = fullmoveNumber;
    undo.hashKey = hashKey;

    const int us = sideToMove;
    const int them = us ^ 1;
    const int from = move.from();
    const int to = move.to();
    const int movingPieceType = move.piece();

    hashKey ^= g_zobrist.castling[castlingRights];
    if (enPassantSquare != NO_SQUARE) {
        hashKey ^= g_zobrist.enPassant[file_of(enPassantSquare)];
    }

    enPassantSquare = NO_SQUARE;

    removePiece(us, movingPieceType, from);

    if (move.isEnPassant()) {
        const int captureSquare = to + (us == WHITE ? -8 : 8);
        undo.capturedPiece = squares[captureSquare];
        removePiece(them, PAWN, captureSquare);
    } else if (move.isCapture()) {
        undo.capturedPiece = squares[to];
        removePiece(them, piece_type(undo.capturedPiece), to);
    }

    const int placedPieceType = move.isPromotion() ? move.promotion() : movingPieceType;
    addPiece(us, placedPieceType, to);

    if (move.isCastle()) {
        if (to == 6) {
            movePiece(WHITE, ROOK, 7, 5);
        } else if (to == 2) {
            movePiece(WHITE, ROOK, 0, 3);
        } else if (to == 62) {
            movePiece(BLACK, ROOK, 63, 61);
        } else if (to == 58) {
            movePiece(BLACK, ROOK, 56, 59);
        }
    }

    castlingRights &= kCastleMasks[from];
    castlingRights &= kCastleMasks[to];

    if (movingPieceType == PAWN && move.isDoublePawnPush()) {
        enPassantSquare = us == WHITE ? from + 8 : from - 8;
    }

    halfmoveClock = (movingPieceType == PAWN || undo.capturedPiece != NO_PIECE) ? 0 : halfmoveClock + 1;
    if (us == BLACK) {
        ++fullmoveNumber;
    }

    hashKey ^= g_zobrist.castling[castlingRights];
    if (enPassantSquare != NO_SQUARE) {
        hashKey ^= g_zobrist.enPassant[file_of(enPassantSquare)];
    }

    sideToMove ^= 1;
    hashKey ^= g_zobrist.side;

    refreshOccupancies();
    if (inCheck(us)) {
        unmakeMove(undo);
        return false;
    }

    positionHistory.push_back(hashKey);
    return true;
}

void Board::unmakeMove(const UndoState& undo) {
    if (!positionHistory.empty()) {
        positionHistory.pop_back();
    }

    const Move move = undo.move;
    const int us = sideToMove ^ 1;
    const int them = sideToMove;
    const int from = move.from();
    const int to = move.to();
    const int movingPieceType = move.piece();
    const int placedPieceType = move.isPromotion() ? move.promotion() : movingPieceType;

    sideToMove = us;

    removePiece(us, placedPieceType, to);

    if (move.isCastle()) {
        if (to == 6) {
            movePiece(WHITE, ROOK, 5, 7);
        } else if (to == 2) {
            movePiece(WHITE, ROOK, 3, 0);
        } else if (to == 62) {
            movePiece(BLACK, ROOK, 61, 63);
        } else if (to == 58) {
            movePiece(BLACK, ROOK, 59, 56);
        }
    }

    addPiece(us, movingPieceType, from);

    if (undo.capturedPiece != NO_PIECE) {
        const int captureSquare = move.isEnPassant() ? (to + (us == WHITE ? -8 : 8)) : to;
        addPiece(them, piece_type(undo.capturedPiece), captureSquare);
    }

    castlingRights = undo.castlingRights;
    enPassantSquare = undo.enPassantSquare;
    halfmoveClock = undo.halfmoveClock;
    fullmoveNumber = undo.fullmoveNumber;
    hashKey = undo.hashKey;
    refreshOccupancies();
}

void Board::makeNullMove(UndoState& undo) {
    undo.move = Move{};
    undo.capturedPiece = NO_PIECE;
    undo.castlingRights = castlingRights;
    undo.enPassantSquare = enPassantSquare;
    undo.halfmoveClock = halfmoveClock;
    undo.fullmoveNumber = fullmoveNumber;
    undo.hashKey = hashKey;

    if (enPassantSquare != NO_SQUARE) {
        hashKey ^= g_zobrist.enPassant[file_of(enPassantSquare)];
    }

    enPassantSquare = NO_SQUARE;
    ++halfmoveClock;
    if (sideToMove == BLACK) {
        ++fullmoveNumber;
    }

    sideToMove ^= 1;
    hashKey ^= g_zobrist.side;
    positionHistory.push_back(hashKey);
}

void Board::unmakeNullMove(const UndoState& undo) {
    if (!positionHistory.empty()) {
        positionHistory.pop_back();
    }

    sideToMove ^= 1;
    castlingRights = undo.castlingRights;
    enPassantSquare = undo.enPassantSquare;
    halfmoveClock = undo.halfmoveClock;
    fullmoveNumber = undo.fullmoveNumber;
    hashKey = undo.hashKey;
}

bool Board::isSquareAttacked(int square, int byColor) const {
    const Bitboard all = occupancies[2];

    if (MoveGen::pawnAttacks[byColor ^ 1][square] & bitboards[byColor][PAWN]) {
        return true;
    }
    if (MoveGen::knightAttacks[square] & bitboards[byColor][KNIGHT]) {
        return true;
    }
    if (MoveGen::kingAttacks[square] & bitboards[byColor][KING]) {
        return true;
    }

    const Bitboard bishopLike = bitboards[byColor][BISHOP] | bitboards[byColor][QUEEN];
    if (MoveGen::bishopAttacks(square, all) & bishopLike) {
        return true;
    }

    const Bitboard rookLike = bitboards[byColor][ROOK] | bitboards[byColor][QUEEN];
    return (MoveGen::rookAttacks(square, all) & rookLike) != 0ULL;
}

bool Board::inCheck(int color) const {
    const int kingSq = kingSquare(color);
    return kingSq != NO_SQUARE && isSquareAttacked(kingSq, color ^ 1);
}

int Board::kingSquare(int color) const {
    Bitboard kingBoard = bitboards[color][KING];
    if (kingBoard == 0ULL) {
        return NO_SQUARE;
    }
#if defined(__GNUG__) || defined(__clang__)
    return __builtin_ctzll(kingBoard);
#else
    return pop_lsb(kingBoard);
#endif
}

bool Board::isRepetition() const {
    if (positionHistory.size() < 5) {
        return false;
    }

    const std::uint64_t current = positionHistory.back();
    int matches = 1;
    const int minIndex = std::max(0, static_cast<int>(positionHistory.size()) - halfmoveClock - 1);
    for (int index = static_cast<int>(positionHistory.size()) - 3; index >= minIndex; index -= 2) {
        if (positionHistory[index] == current) {
            ++matches;
            if (matches >= 3) {
                return true;
            }
        }
    }
    return false;
}

std::string Board::toString() const {
    std::ostringstream out;
    for (int rank = 7; rank >= 0; --rank) {
        out << (rank + 1) << " ";
        for (int file = 0; file < 8; ++file) {
            const int square = rank * 8 + file;
            out << pieceToChar(squares[square]) << ' ';
        }
        out << '\n';
    }
    out << "  a b c d e f g h\n";
    out << "Side: " << (sideToMove == WHITE ? "white" : "black") << '\n';
    out << "Castling: "
        << ((castlingRights & WHITE_OO) ? 'K' : '-')
        << ((castlingRights & WHITE_OOO) ? 'Q' : '-')
        << ((castlingRights & BLACK_OO) ? 'k' : '-')
        << ((castlingRights & BLACK_OOO) ? 'q' : '-') << '\n';
    out << "EP: " << (enPassantSquare == NO_SQUARE ? "-" : squareToString(enPassantSquare)) << '\n';
    out << "Hash: 0x" << std::hex << std::setw(16) << std::setfill('0') << hashKey << std::dec << '\n';
    return out.str();
}

std::string squareToString(int square) {
    if (square < 0 || square >= BOARD_SQUARES) {
        return "-";
    }
    std::string token = "a1";
    token[0] = static_cast<char>('a' + file_of(square));
    token[1] = static_cast<char>('1' + rank_of(square));
    return token;
}

int squareFromString(const std::string& token) {
    if (token.size() != 2 || token[0] < 'a' || token[0] > 'h' || token[1] < '1' || token[1] > '8') {
        return NO_SQUARE;
    }
    const int file = token[0] - 'a';
    const int rank = token[1] - '1';
    return rank * 8 + file;
}
