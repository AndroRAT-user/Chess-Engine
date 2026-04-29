#pragma once

#include "board.h"

#include <array>
#include <string>

constexpr int MAX_MOVES = 256;

struct MoveList {
    std::array<Move, MAX_MOVES> moves{};
    int size = 0;

    void clear() { size = 0; }

    void add(Move move) {
        if (size < MAX_MOVES) {
            moves[size++] = move;
        }
    }

    Move& operator[](int index) { return moves[index]; }
    const Move& operator[](int index) const { return moves[index]; }
};

namespace MoveGen {

extern std::array<std::array<Bitboard, BOARD_SQUARES>, COLOR_NB> pawnAttacks;
extern std::array<Bitboard, BOARD_SQUARES> knightAttacks;
extern std::array<Bitboard, BOARD_SQUARES> kingAttacks;

void initAttackTables();

Bitboard rookAttacks(int square, Bitboard occupied);
Bitboard bishopAttacks(int square, Bitboard occupied);

void generatePseudoLegal(const Board& board, MoveList& moves, bool capturesOnly = false);
void generateLegal(Board& board, MoveList& legalMoves, bool capturesOnly = false);

std::string moveToUci(Move move);
Move parseMove(Board& board, const std::string& token);

}  // namespace MoveGen

