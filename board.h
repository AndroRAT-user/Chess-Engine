#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using Bitboard = std::uint64_t;

constexpr int BOARD_SQUARES = 64;
constexpr int NO_SQUARE = -1;
constexpr int MAX_PLY = 128;

enum Color : int {
    WHITE = 0,
    BLACK = 1,
    COLOR_NB = 2
};

enum PieceType : int {
    PAWN = 0,
    KNIGHT = 1,
    BISHOP = 2,
    ROOK = 3,
    QUEEN = 4,
    KING = 5,
    PIECE_TYPE_NB = 6,
    NO_PIECE_TYPE = 7
};

enum Piece : int {
    NO_PIECE = 0,
    W_PAWN,
    W_KNIGHT,
    W_BISHOP,
    W_ROOK,
    W_QUEEN,
    W_KING,
    B_PAWN,
    B_KNIGHT,
    B_BISHOP,
    B_ROOK,
    B_QUEEN,
    B_KING
};

enum CastlingRight : int {
    WHITE_OO = 1,
    WHITE_OOO = 2,
    BLACK_OO = 4,
    BLACK_OOO = 8
};

enum MoveFlag : int {
    FLAG_NONE = 0,
    FLAG_CAPTURE = 1 << 0,
    FLAG_DOUBLE_PAWN = 1 << 1,
    FLAG_EN_PASSANT = 1 << 2,
    FLAG_CASTLE = 1 << 3
};

constexpr std::array<int, PIECE_TYPE_NB> PIECE_VALUES = {100, 320, 330, 500, 900, 0};
constexpr const char* STARTPOS_FEN =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

inline constexpr Bitboard bit(int square) {
    return 1ULL << square;
}

inline constexpr int file_of(int square) {
    return square & 7;
}

inline constexpr int rank_of(int square) {
    return square >> 3;
}

inline constexpr int mirror_square(int square) {
    return square ^ 56;
}

inline constexpr int make_piece(int color, int pieceType) {
    return 1 + color * 6 + pieceType;
}

inline constexpr int piece_color(int piece) {
    return piece >= B_PAWN ? BLACK : WHITE;
}

inline constexpr int piece_type(int piece) {
    return piece == NO_PIECE ? NO_PIECE_TYPE : (piece - 1) % 6;
}

inline int popcount(Bitboard value) {
#if defined(__GNUG__) || defined(__clang__)
    return __builtin_popcountll(value);
#else
    int count = 0;
    while (value) {
        value &= value - 1;
        ++count;
    }
    return count;
#endif
}

inline int pop_lsb(Bitboard& value) {
#if defined(__GNUG__) || defined(__clang__)
    const int square = __builtin_ctzll(value);
#else
    int square = 0;
    Bitboard temp = value;
    while ((temp & 1ULL) == 0ULL) {
        temp >>= 1ULL;
        ++square;
    }
#endif
    value &= value - 1;
    return square;
}

struct Move {
    std::uint32_t value = 0;

    constexpr Move() = default;

    constexpr Move(int from, int to, int piece, int captured, int promotion, int flags)
        : value(static_cast<std::uint32_t>(from)
                | (static_cast<std::uint32_t>(to) << 6)
                | (static_cast<std::uint32_t>(piece) << 12)
                | (static_cast<std::uint32_t>(captured) << 15)
                | (static_cast<std::uint32_t>(promotion) << 18)
                | (static_cast<std::uint32_t>(flags) << 21)) {}

    constexpr int from() const { return value & 0x3F; }
    constexpr int to() const { return (value >> 6) & 0x3F; }
    constexpr int piece() const { return (value >> 12) & 0x7; }
    constexpr int captured() const { return (value >> 15) & 0x7; }
    constexpr int promotion() const { return (value >> 18) & 0x7; }
    constexpr int flags() const { return (value >> 21) & 0xF; }
    constexpr bool isNull() const { return value == 0; }
    constexpr bool isCapture() const { return (flags() & FLAG_CAPTURE) != 0; }
    constexpr bool isPromotion() const { return promotion() != NO_PIECE_TYPE; }
    constexpr bool isEnPassant() const { return (flags() & FLAG_EN_PASSANT) != 0; }
    constexpr bool isCastle() const { return (flags() & FLAG_CASTLE) != 0; }
    constexpr bool isDoublePawnPush() const { return (flags() & FLAG_DOUBLE_PAWN) != 0; }

    constexpr bool operator==(const Move& other) const { return value == other.value; }
    constexpr bool operator!=(const Move& other) const { return value != other.value; }
};

struct UndoState {
    Move move;
    int capturedPiece = NO_PIECE;
    int castlingRights = 0;
    int enPassantSquare = NO_SQUARE;
    int halfmoveClock = 0;
    int fullmoveNumber = 1;
    std::uint64_t hashKey = 0;
};

class Board {
public:
    Board();

    void clear();
    bool setFen(const std::string& fen);
    void setStartPos();

    bool makeMove(Move move, UndoState& undo);
    void unmakeMove(const UndoState& undo);
    void makeNullMove(UndoState& undo);
    void unmakeNullMove(const UndoState& undo);

    bool isSquareAttacked(int square, int byColor) const;
    bool inCheck(int color) const;
    int kingSquare(int color) const;
    bool isRepetition() const;

    int pieceAt(int square) const { return squares[square]; }
    std::string toString() const;

    std::array<std::array<Bitboard, PIECE_TYPE_NB>, COLOR_NB> bitboards{};
    std::array<Bitboard, 3> occupancies{};
    std::array<int, BOARD_SQUARES> squares{};
    int sideToMove = WHITE;
    int castlingRights = 0;
    int enPassantSquare = NO_SQUARE;
    int halfmoveClock = 0;
    int fullmoveNumber = 1;
    std::uint64_t hashKey = 0;

private:
    void refreshOccupancies();
    void addPiece(int color, int pieceType, int square);
    void removePiece(int color, int pieceType, int square);
    void movePiece(int color, int pieceType, int from, int to);
    std::uint64_t computeHashKey() const;

    std::vector<std::uint64_t> positionHistory;
};

std::string squareToString(int square);
int squareFromString(const std::string& token);
