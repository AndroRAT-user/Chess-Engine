#include "eval.h"

#include "movegen.h"

#include <algorithm>
#include <array>
#include <climits>

namespace Eval
{
    namespace
    {
        const int DOUBLED_PAWN_PENALTY = 15;
        const int ISOLATED_PAWN_PENALTY = 20;
        constexpr std::array<int, PIECE_TYPE_NB> EG_MATERIAL = {120, 310, 340, 520, 900, 0};
        constexpr std::array<int, PIECE_TYPE_NB> PHASE_WEIGHTS = {0, 1, 1, 2, 4, 0};
        constexpr std::array<int, PIECE_TYPE_NB> MG_MOBILITY = {0, 4, 5, 2, 1, 0};
        constexpr std::array<int, PIECE_TYPE_NB> EG_MOBILITY = {0, 3, 3, 2, 1, 0};
        constexpr std::array<int, 8> PASSED_PAWN_MG = {0, 5, 10, 18, 30, 45, 0, 0};
        constexpr std::array<int, 8> PASSED_PAWN_EG = {0, 8, 18, 32, 52, 78, 0, 0};

        // Pawn structure penalties

        constexpr std::array<int, BOARD_SQUARES> PAWN_PST = {
            0, 0, 0, 0, 0, 0, 0, 0,
            10, 10, 0, -10, -10, 0, 10, 10,
            5, 5, 10, 20, 20, 10, 5, 5,
            0, 0, 0, 28, 28, 0, 0, 0,
            5, 5, 10, 22, 22, 10, 5, 5,
            10, 10, 18, 24, 24, 18, 10, 10,
            40, 40, 40, 46, 46, 40, 40, 40,
            0, 0, 0, 0, 0, 0, 0, 0};

        constexpr std::array<int, BOARD_SQUARES> KNIGHT_PST = {
            -50, -40, -30, -30, -30, -30, -40, -50,
            -40, -20, 0, 5, 5, 0, -20, -40,
            -30, 5, 10, 15, 15, 10, 5, -30,
            -30, 0, 15, 20, 20, 15, 0, -30,
            -30, 5, 15, 20, 20, 15, 5, -30,
            -30, 0, 10, 15, 15, 10, 0, -30,
            -40, -20, 0, 0, 0, 0, -20, -40,
            -50, -40, -30, -30, -30, -30, -40, -50};

        constexpr std::array<int, BOARD_SQUARES> BISHOP_PST = {
            -20, -10, -10, -10, -10, -10, -10, -20,
            -10, 5, 0, 0, 0, 0, 5, -10,
            -10, 10, 10, 12, 12, 10, 10, -10,
            -10, 0, 12, 15, 15, 12, 0, -10,
            -10, 5, 10, 15, 15, 10, 5, -10,
            -10, 0, 5, 10, 10, 5, 0, -10,
            -10, 0, 0, 0, 0, 0, 0, -10,
            -20, -10, -10, -10, -10, -10, -10, -20};

        constexpr std::array<int, BOARD_SQUARES> ROOK_PST = {
            0, 0, 5, 10, 10, 5, 0, 0,
            -5, 0, 0, 0, 0, 0, 0, -5,
            -5, 0, 0, 0, 0, 0, 0, -5,
            -5, 0, 0, 0, 0, 0, 0, -5,
            -5, 0, 0, 0, 0, 0, 0, -5,
            -5, 0, 0, 0, 0, 0, 0, -5,
            8, 12, 12, 12, 12, 12, 12, 8,
            0, 0, 5, 10, 10, 5, 0, 0};

        constexpr std::array<int, BOARD_SQUARES> QUEEN_PST = {
            -20, -10, -10, -5, -5, -10, -10, -20,
            -10, 0, 0, 0, 0, 0, 0, -10,
            -10, 0, 5, 5, 5, 5, 0, -10,
            -5, 0, 5, 5, 5, 5, 0, -5,
            0, 0, 5, 5, 5, 5, 0, -5,
            -10, 5, 5, 5, 5, 5, 0, -10,
            -10, 0, 5, 0, 0, 0, 0, -10,
            -20, -10, -10, -5, -5, -10, -10, -20};

        constexpr std::array<int, BOARD_SQUARES> KING_MG_PST = {
            -40, -40, -40, -50, -50, -40, -40, -40,
            -30, -30, -30, -40, -40, -30, -30, -30,
            -20, -20, -20, -30, -30, -20, -20, -20,
            -10, -10, -10, -20, -20, -10, -10, -10,
            0, 0, 0, -10, -10, 0, 0, 0,
            10, 10, 0, 0, 0, 0, 10, 10,
            25, 25, 10, 0, 0, 10, 25, 25,
            30, 35, 15, 0, 0, 15, 35, 30};

        constexpr std::array<int, BOARD_SQUARES> KING_EG_PST = {
            -50, -30, -20, -10, -10, -20, -30, -50,
            -30, -10, 0, 5, 5, 0, -10, -30,
            -20, 0, 10, 15, 15, 10, 0, -20,
            -10, 5, 15, 20, 20, 15, 5, -10,
            -10, 5, 15, 20, 20, 15, 5, -10,
            -20, 0, 10, 15, 15, 10, 0, -20,
            -30, -10, 0, 5, 5, 0, -10, -30,
            -50, -30, -20, -10, -10, -20, -30, -50};

        constexpr std::array<const std::array<int, BOARD_SQUARES> *, 5> PSTS = {
            &PAWN_PST, &KNIGHT_PST, &BISHOP_PST, &ROOK_PST, &QUEEN_PST};

        bool isPassedPawn(const Board &board, int color, int square)
        {
            Bitboard enemyPawns = board.bitboards[color ^ 1][PAWN];
            const int pawnFile = file_of(square);
            const int pawnRank = rank_of(square);

            while (enemyPawns)
            {
                const int enemySquare = pop_lsb(enemyPawns);
                const int enemyFile = file_of(enemySquare);
                const int enemyRank = rank_of(enemySquare);

                if (std::abs(enemyFile - pawnFile) > 1)
                {
                    continue;
                }
                if (color == WHITE && enemyRank > pawnRank)
                {
                    return false;
                }
                if (color == BLACK && enemyRank < pawnRank)
                {
                    return false;
                }
            }

            return true;
        }

        int kingShieldScore(const Board &board, int color)
        {
            const int kingSquare = board.kingSquare(color);
            if (kingSquare == NO_SQUARE)
            {
                return 0;
            }

            const int direction = color == WHITE ? 1 : -1;
            const int frontRank = rank_of(kingSquare) + direction;
            if (frontRank < 0 || frontRank > 7)
            {
                return 0;
            }

            int score = 0;
            for (int df = -1; df <= 1; ++df)
            {
                const int file = file_of(kingSquare) + df;
                if (file < 0 || file > 7)
                {
                    continue;
                }
                const int shieldSquare = frontRank * 8 + file;
                if (board.pieceAt(shieldSquare) == make_piece(color, PAWN))
                {
                    score += 12;
                }
            }
            return score;
        }

        int kingDangerPenalty(const Board &board, int color)
        {
            const int kingSquare = board.kingSquare(color);
            if (kingSquare == NO_SQUARE)
            {
                return 0;
            }

            Bitboard ring = MoveGen::kingAttacks[kingSquare] | bit(kingSquare);
            int penalty = 0;
            while (ring)
            {
                const int square = pop_lsb(ring);
                if (board.isSquareAttacked(square, color ^ 1))
                {
                    penalty += 8;
                }
            }
            return penalty;
        }

        int pawnStructureScore(const Board &board, int color)
        {
            int score = 0;
            Bitboard pawns = board.bitboards[color][PAWN];

            // Check each file for doubled and isolated pawns
            for (int file = 0; file < 8; ++file)
            {
                Bitboard fileMask = 0ULL;
                for (int rank = 0; rank < 8; ++rank)
                {
                    fileMask |= bit(rank * 8 + file);
                }
                Bitboard filePawns = pawns & fileMask;

                if (filePawns)
                {
                    int pawnCount = popcount(filePawns);
                    if (pawnCount > 1)
                    {
                        score -= DOUBLED_PAWN_PENALTY * (pawnCount - 1);
                    }

                    // Check if isolated
                    bool hasNeighbor = false;
                    if (file > 0)
                    {
                        Bitboard leftFile = 0ULL;
                        for (int rank = 0; rank < 8; ++rank)
                        {
                            leftFile |= bit(rank * 8 + file - 1);
                        }
                        if (board.bitboards[color][PAWN] & leftFile)
                        {
                            hasNeighbor = true;
                        }
                    }
                    if (file < 7)
                    {
                        Bitboard rightFile = 0ULL;
                        for (int rank = 0; rank < 8; ++rank)
                        {
                            rightFile |= bit(rank * 8 + file + 1);
                        }
                        if (board.bitboards[color][PAWN] & rightFile)
                        {
                            hasNeighbor = true;
                        }
                    }
                    if (!hasNeighbor)
                    {
                        score -= ISOLATED_PAWN_PENALTY;
                    }
                }
            }

            return score;
        }

        int mobilityScore(const Board &board, int color, int pieceType, int square)
        {
            const Bitboard own = board.occupancies[color];
            Bitboard attacks = 0ULL;

            switch (pieceType)
            {
            case KNIGHT:
                attacks = MoveGen::knightAttacks[square];
                break;
            case BISHOP:
                attacks = MoveGen::bishopAttacks(square, board.occupancies[2]);
                break;
            case ROOK:
                attacks = MoveGen::rookAttacks(square, board.occupancies[2]);
                break;
            case QUEEN:
                attacks = MoveGen::bishopAttacks(square, board.occupancies[2]) | MoveGen::rookAttacks(square, board.occupancies[2]);
                break;
            default:
                break;
            }

            attacks &= ~own;
            return popcount(attacks);
        }

    } // namespace

    int evaluate(const Board &board)
    {
        MoveGen::initAttackTables();

        int mgScore[COLOR_NB] = {0, 0};
        int egScore[COLOR_NB] = {0, 0};
        int phase = 0;
        int bishopCount[COLOR_NB] = {0, 0};

        for (int color = WHITE; color <= BLACK; ++color)
        {
            for (int pieceType = PAWN; pieceType <= KING; ++pieceType)
            {
                Bitboard pieces = board.bitboards[color][pieceType];
                while (pieces)
                {
                    const int square = pop_lsb(pieces);
                    const int relative = color == WHITE ? square : mirror_square(square);

                    if (pieceType != KING)
                    {
                        mgScore[color] += MG_MATERIAL[pieceType];
                        egScore[color] += EG_MATERIAL[pieceType];
                    }

                    if (pieceType == KING)
                    {
                        mgScore[color] += KING_MG_PST[relative];
                        egScore[color] += KING_EG_PST[relative];
                    }
                    else
                    {
                        mgScore[color] += (*PSTS[pieceType])[relative];
                        egScore[color] += (*PSTS[pieceType])[relative] / 2;
                    }

                    phase += PHASE_WEIGHTS[pieceType];

                    if (pieceType == BISHOP)
                    {
                        ++bishopCount[color];
                    }

                    if (pieceType == PAWN && isPassedPawn(board, color, square))
                    {
                        const int advance = color == WHITE ? rank_of(square) : 7 - rank_of(square);
                        mgScore[color] += PASSED_PAWN_MG[advance];
                        egScore[color] += PASSED_PAWN_EG[advance];
                    }

                    if (pieceType >= KNIGHT && pieceType <= QUEEN)
                    {
                        const int mobility = mobilityScore(board, color, pieceType, square);
                        mgScore[color] += mobility * MG_MOBILITY[pieceType];
                        egScore[color] += mobility * EG_MOBILITY[pieceType];
                    }
                }
            }

            if (bishopCount[color] >= 2)
            {
                mgScore[color] += 30;
                egScore[color] += 40;
            }

            mgScore[color] += pawnStructureScore(board, color);
            egScore[color] += pawnStructureScore(board, color) / 2;

            mgScore[color] += kingShieldScore(board, color);
            mgScore[color] -= kingDangerPenalty(board, color);
            egScore[color] -= kingDangerPenalty(board, color) / 2;

            if (color == WHITE)
            {
                if (board.castlingRights & (WHITE_OO | WHITE_OOO))
                {
                    mgScore[color] += 10;
                }
            }
            else
            {
                if (board.castlingRights & (BLACK_OO | BLACK_OOO))
                {
                    mgScore[color] += 10;
                }
            }
        }

        phase = std::min(24, phase);

        const int mg = mgScore[WHITE] - mgScore[BLACK];
        const int eg = egScore[WHITE] - egScore[BLACK];
        const int score = (mg * phase + eg * (24 - phase)) / 24;

        return board.sideToMove == WHITE ? score : -score;
    }

    Bitboard get_piece_attacks(int pieceType, int square, Bitboard occupied, int color)
    {
        switch (pieceType)
        {
        case PAWN:
            return MoveGen::pawnAttacks[color][square];
        case KNIGHT:
            return MoveGen::knightAttacks[square];
        case BISHOP:
            return MoveGen::bishopAttacks(square, occupied);
        case ROOK:
            return MoveGen::rookAttacks(square, occupied);
        case QUEEN:
            return MoveGen::bishopAttacks(square, occupied) | MoveGen::rookAttacks(square, occupied);
        case KING:
            return MoveGen::kingAttacks[square];
        default:
            return 0ULL;
        }
    }

    int see(const Board &board, Move move)
    {
        const int to = move.to();
        const int captured = move.captured();
        int gain[32];
        int d = 0;
        gain[d] = PIECE_VALUES[captured];

        Bitboard occupied = board.occupancies[WHITE] | board.occupancies[BLACK];
        occupied ^= bit(to); // Remove the captured piece

        int side = board.sideToMove ^ 1; // Opponent recaptures first

        while (d < 31)
        {
            int min_attacker_value = INT_MAX;
            int attacker_square = -1;
            int attacker_type = -1;

            // Find the least valuable attacker for the current side
            for (int pt = PAWN; pt <= KING; ++pt)
            {
                Bitboard pieces = board.bitboards[side][pt];
                while (pieces)
                {
                    const int square = pop_lsb(pieces);
                    const Bitboard attacks = get_piece_attacks(pt, square, occupied, side);
                    if (attacks & bit(to))
                    {
                        if (PIECE_VALUES[pt] < min_attacker_value)
                        {
                            min_attacker_value = PIECE_VALUES[pt];
                            attacker_square = square;
                            attacker_type = pt;
                        }
                    }
                }
            }

            if (attacker_square == -1)
            {
                break; // No more attackers
            }

            d++;
            gain[d] = PIECE_VALUES[attacker_type] - gain[d - 1];

            // Remove the attacking piece from occupied
            occupied ^= bit(attacker_square);

            side ^= 1; // Switch sides
        }

        // Negamax backpropagation
        while (--d)
        {
            gain[d - 1] = -std::max(-gain[d - 1], gain[d]);
        }

        return gain[0];
    }

} // namespace Eval
