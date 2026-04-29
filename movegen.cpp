#include "movegen.h"

#include <algorithm>
#include <cctype>
#include <mutex>

namespace MoveGen {

std::array<std::array<Bitboard, BOARD_SQUARES>, COLOR_NB> pawnAttacks{};
std::array<Bitboard, BOARD_SQUARES> knightAttacks{};
std::array<Bitboard, BOARD_SQUARES> kingAttacks{};

namespace {

std::once_flag attackInitOnce;

Bitboard rayAttacks(int square, int deltaRank, int deltaFile, Bitboard occupied) {
    Bitboard attacks = 0ULL;
    int rank = rank_of(square) + deltaRank;
    int file = file_of(square) + deltaFile;

    while (rank >= 0 && rank < 8 && file >= 0 && file < 8) {
        const int target = rank * 8 + file;
        attacks |= bit(target);
        if (occupied & bit(target)) {
            break;
        }
        rank += deltaRank;
        file += deltaFile;
    }

    return attacks;
}

void addMove(MoveList& moves, int from, int to, int piece, int captured, int promotion, int flags) {
    moves.add(Move(from, to, piece, captured, promotion, flags));
}

void addPromotionMoves(MoveList& moves, int from, int to, int captured, int flags) {
    addMove(moves, from, to, PAWN, captured, QUEEN, flags);
    addMove(moves, from, to, PAWN, captured, ROOK, flags);
    addMove(moves, from, to, PAWN, captured, BISHOP, flags);
    addMove(moves, from, to, PAWN, captured, KNIGHT, flags);
}

}  // namespace

void initAttackTables() {
    std::call_once(attackInitOnce, [] {
        for (int square = 0; square < BOARD_SQUARES; ++square) {
            const int rank = rank_of(square);
            const int file = file_of(square);

            if (rank < 7) {
                if (file > 0) pawnAttacks[WHITE][square] |= bit(square + 7);
                if (file < 7) pawnAttacks[WHITE][square] |= bit(square + 9);
            }
            if (rank > 0) {
                if (file > 0) pawnAttacks[BLACK][square] |= bit(square - 9);
                if (file < 7) pawnAttacks[BLACK][square] |= bit(square - 7);
            }

            constexpr int knightOffsets[8][2] = {
                {2, 1}, {2, -1}, {1, 2}, {1, -2},
                {-1, 2}, {-1, -2}, {-2, 1}, {-2, -1}
            };
            for (const auto& offset : knightOffsets) {
                const int targetRank = rank + offset[0];
                const int targetFile = file + offset[1];
                if (targetRank >= 0 && targetRank < 8 && targetFile >= 0 && targetFile < 8) {
                    knightAttacks[square] |= bit(targetRank * 8 + targetFile);
                }
            }

            constexpr int kingOffsets[8][2] = {
                {1, 0}, {1, 1}, {0, 1}, {-1, 1},
                {-1, 0}, {-1, -1}, {0, -1}, {1, -1}
            };
            for (const auto& offset : kingOffsets) {
                const int targetRank = rank + offset[0];
                const int targetFile = file + offset[1];
                if (targetRank >= 0 && targetRank < 8 && targetFile >= 0 && targetFile < 8) {
                    kingAttacks[square] |= bit(targetRank * 8 + targetFile);
                }
            }
        }
    });
}

Bitboard rookAttacks(int square, Bitboard occupied) {
    return rayAttacks(square, 1, 0, occupied)
         | rayAttacks(square, -1, 0, occupied)
         | rayAttacks(square, 0, 1, occupied)
         | rayAttacks(square, 0, -1, occupied);
}

Bitboard bishopAttacks(int square, Bitboard occupied) {
    return rayAttacks(square, 1, 1, occupied)
         | rayAttacks(square, 1, -1, occupied)
         | rayAttacks(square, -1, 1, occupied)
         | rayAttacks(square, -1, -1, occupied);
}

void generatePseudoLegal(const Board& board, MoveList& moves, bool capturesOnly) {
    initAttackTables();
    moves.clear();

    const int us = board.sideToMove;
    const int them = us ^ 1;
    const Bitboard own = board.occupancies[us];
    const Bitboard enemy = board.occupancies[them];
    const Bitboard occupied = board.occupancies[2];

    Bitboard pawns = board.bitboards[us][PAWN];
    while (pawns) {
        const int from = pop_lsb(pawns);
        const int rank = rank_of(from);
        const int step = us == WHITE ? 8 : -8;
        const int promotionRank = us == WHITE ? 6 : 1;
        const int startRank = us == WHITE ? 1 : 6;
        const int singlePush = from + step;

        if (singlePush >= 0 && singlePush < 64 && board.pieceAt(singlePush) == NO_PIECE) {
            if (rank == promotionRank) {
                addPromotionMoves(moves, from, singlePush, NO_PIECE_TYPE, FLAG_NONE);
            } else if (!capturesOnly) {
                addMove(moves, from, singlePush, PAWN, NO_PIECE_TYPE, NO_PIECE_TYPE, FLAG_NONE);

                const int doublePush = from + step * 2;
                if (rank == startRank && board.pieceAt(doublePush) == NO_PIECE) {
                    addMove(moves, from, doublePush, PAWN, NO_PIECE_TYPE, NO_PIECE_TYPE, FLAG_DOUBLE_PAWN);
                }
            }
        }

        Bitboard attacks = pawnAttacks[us][from] & enemy;
        while (attacks) {
            const int to = pop_lsb(attacks);
            const int captured = piece_type(board.pieceAt(to));
            if (rank == promotionRank) {
                addPromotionMoves(moves, from, to, captured, FLAG_CAPTURE);
            } else {
                addMove(moves, from, to, PAWN, captured, NO_PIECE_TYPE, FLAG_CAPTURE);
            }
        }

        if (board.enPassantSquare != NO_SQUARE && (pawnAttacks[us][from] & bit(board.enPassantSquare))) {
            addMove(moves, from, board.enPassantSquare, PAWN, PAWN, NO_PIECE_TYPE, FLAG_CAPTURE | FLAG_EN_PASSANT);
        }
    }

    Bitboard knights = board.bitboards[us][KNIGHT];
    while (knights) {
        const int from = pop_lsb(knights);
        Bitboard attacks = knightAttacks[from] & ~own;
        if (capturesOnly) {
            attacks &= enemy;
        }
        while (attacks) {
            const int to = pop_lsb(attacks);
            const int target = board.pieceAt(to);
            const int flags = target == NO_PIECE ? FLAG_NONE : FLAG_CAPTURE;
            addMove(moves, from, to, KNIGHT, target == NO_PIECE ? NO_PIECE_TYPE : piece_type(target), NO_PIECE_TYPE, flags);
        }
    }

    Bitboard bishops = board.bitboards[us][BISHOP];
    while (bishops) {
        const int from = pop_lsb(bishops);
        Bitboard attacks = bishopAttacks(from, occupied) & ~own;
        if (capturesOnly) {
            attacks &= enemy;
        }
        while (attacks) {
            const int to = pop_lsb(attacks);
            const int target = board.pieceAt(to);
            const int flags = target == NO_PIECE ? FLAG_NONE : FLAG_CAPTURE;
            addMove(moves, from, to, BISHOP, target == NO_PIECE ? NO_PIECE_TYPE : piece_type(target), NO_PIECE_TYPE, flags);
        }
    }

    Bitboard rooks = board.bitboards[us][ROOK];
    while (rooks) {
        const int from = pop_lsb(rooks);
        Bitboard attacks = rookAttacks(from, occupied) & ~own;
        if (capturesOnly) {
            attacks &= enemy;
        }
        while (attacks) {
            const int to = pop_lsb(attacks);
            const int target = board.pieceAt(to);
            const int flags = target == NO_PIECE ? FLAG_NONE : FLAG_CAPTURE;
            addMove(moves, from, to, ROOK, target == NO_PIECE ? NO_PIECE_TYPE : piece_type(target), NO_PIECE_TYPE, flags);
        }
    }

    Bitboard queens = board.bitboards[us][QUEEN];
    while (queens) {
        const int from = pop_lsb(queens);
        Bitboard attacks = (rookAttacks(from, occupied) | bishopAttacks(from, occupied)) & ~own;
        if (capturesOnly) {
            attacks &= enemy;
        }
        while (attacks) {
            const int to = pop_lsb(attacks);
            const int target = board.pieceAt(to);
            const int flags = target == NO_PIECE ? FLAG_NONE : FLAG_CAPTURE;
            addMove(moves, from, to, QUEEN, target == NO_PIECE ? NO_PIECE_TYPE : piece_type(target), NO_PIECE_TYPE, flags);
        }
    }

    Bitboard king = board.bitboards[us][KING];
    if (king) {
        const int from = pop_lsb(king);
        Bitboard attacks = kingAttacks[from] & ~own;
        if (capturesOnly) {
            attacks &= enemy;
        }
        while (attacks) {
            const int to = pop_lsb(attacks);
            const int target = board.pieceAt(to);
            const int flags = target == NO_PIECE ? FLAG_NONE : FLAG_CAPTURE;
            addMove(moves, from, to, KING, target == NO_PIECE ? NO_PIECE_TYPE : piece_type(target), NO_PIECE_TYPE, flags);
        }

        if (!capturesOnly && !board.inCheck(us)) {
            if (us == WHITE) {
                if ((board.castlingRights & WHITE_OO)
                    && board.pieceAt(5) == NO_PIECE
                    && board.pieceAt(6) == NO_PIECE
                    && !board.isSquareAttacked(5, BLACK)
                    && !board.isSquareAttacked(6, BLACK)) {
                    addMove(moves, 4, 6, KING, NO_PIECE_TYPE, NO_PIECE_TYPE, FLAG_CASTLE);
                }
                if ((board.castlingRights & WHITE_OOO)
                    && board.pieceAt(1) == NO_PIECE
                    && board.pieceAt(2) == NO_PIECE
                    && board.pieceAt(3) == NO_PIECE
                    && !board.isSquareAttacked(3, BLACK)
                    && !board.isSquareAttacked(2, BLACK)) {
                    addMove(moves, 4, 2, KING, NO_PIECE_TYPE, NO_PIECE_TYPE, FLAG_CASTLE);
                }
            } else {
                if ((board.castlingRights & BLACK_OO)
                    && board.pieceAt(61) == NO_PIECE
                    && board.pieceAt(62) == NO_PIECE
                    && !board.isSquareAttacked(61, WHITE)
                    && !board.isSquareAttacked(62, WHITE)) {
                    addMove(moves, 60, 62, KING, NO_PIECE_TYPE, NO_PIECE_TYPE, FLAG_CASTLE);
                }
                if ((board.castlingRights & BLACK_OOO)
                    && board.pieceAt(57) == NO_PIECE
                    && board.pieceAt(58) == NO_PIECE
                    && board.pieceAt(59) == NO_PIECE
                    && !board.isSquareAttacked(59, WHITE)
                    && !board.isSquareAttacked(58, WHITE)) {
                    addMove(moves, 60, 58, KING, NO_PIECE_TYPE, NO_PIECE_TYPE, FLAG_CASTLE);
                }
            }
        }
    }
}

void generateLegal(Board& board, MoveList& legalMoves, bool capturesOnly) {
    MoveList pseudoMoves;
    generatePseudoLegal(board, pseudoMoves, capturesOnly);

    legalMoves.clear();
    for (int i = 0; i < pseudoMoves.size; ++i) {
        UndoState undo;
        if (board.makeMove(pseudoMoves[i], undo)) {
            legalMoves.add(pseudoMoves[i]);
            board.unmakeMove(undo);
        }
    }
}

std::string moveToUci(Move move) {
    std::string token = squareToString(move.from()) + squareToString(move.to());
    if (move.isPromotion()) {
        char promo = 'q';
        switch (move.promotion()) {
            case KNIGHT: promo = 'n'; break;
            case BISHOP: promo = 'b'; break;
            case ROOK: promo = 'r'; break;
            case QUEEN: promo = 'q'; break;
            default: break;
        }
        token.push_back(promo);
    }
    return token;
}

Move parseMove(Board& board, const std::string& token) {
    std::string lowered = token;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    MoveList legalMoves;
    generateLegal(board, legalMoves, false);
    for (int i = 0; i < legalMoves.size; ++i) {
        if (moveToUci(legalMoves[i]) == lowered) {
            return legalMoves[i];
        }
    }
    return Move{};
}

}  // namespace MoveGen

