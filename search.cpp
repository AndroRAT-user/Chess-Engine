#include "search.h"

#include "eval.h"
#include "movegen.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>

namespace
{

    bool hasNonPawnMaterial(const Board &board, int color)
    {
        return (board.bitboards[color][KNIGHT] | board.bitboards[color][BISHOP] | board.bitboards[color][ROOK] | board.bitboards[color][QUEEN]) != 0ULL;
    }

    bool isQuietMove(Move move)
    {
        return !move.isCapture() && !move.isPromotion() && !move.isCastle();
    }

} // namespace

Searcher::Searcher() : table_(TT_SIZE) {}

void Searcher::clear()
{
    std::fill(table_.begin(), table_.end(), TTEntry{});
    for (auto &killers : killerMoves_)
    {
        killers.fill(Move{});
    }
    for (auto &colorHistory : historyHeuristic_)
    {
        for (auto &fromHistory : colorHistory)
        {
            fromHistory.fill(0);
        }
    }
    for (auto &colorCounter : counterMove_)
    {
        for (auto &fromCounter : colorCounter)
        {
            fromCounter.fill(Move{});
        }
    }
    pvLength_.fill(0);
    nodes_ = 0;
    stopRequested_.store(false, std::memory_order_relaxed);
}

void Searcher::requestStop()
{
    stopRequested_.store(true, std::memory_order_relaxed);
}

void Searcher::resetStop()
{
    stopRequested_.store(false, std::memory_order_relaxed);
}

bool Searcher::isStopRequested() const
{
    return stopRequested_.load(std::memory_order_relaxed);
}

void Searcher::setDebug(bool enabled)
{
    debugEnabled_ = enabled;
}

bool Searcher::debugEnabled() const
{
    return debugEnabled_;
}

void Searcher::search(Board board, const SearchLimits &limits)
{
    resetStop();
    startTime_ = std::chrono::steady_clock::now();
    hardTimeLimitMs_ = allocateTimeMs(limits, board.sideToMove);
    nodes_ = 0;
    nodeLimit_ = limits.nodes;
    pvLength_.fill(0);

    int maxDepth = 6;
    if (limits.infinite)
    {
        maxDepth = MAX_PLY - 1;
        hardTimeLimitMs_ = -1;
    }
    else if (limits.depth > 0)
    {
        maxDepth = std::min(limits.depth, MAX_PLY - 1);
    }
    else if (hardTimeLimitMs_ > 0 || nodeLimit_ > 0)
    {
        maxDepth = MAX_PLY - 1;
    }

    if (debugEnabled_)
    {
        std::cout << "info string search start depth=" << maxDepth
                  << " time_ms=" << hardTimeLimitMs_
                  << " node_limit=" << nodeLimit_
                  << std::endl;
    }

    MoveList rootMoves;
    MoveGen::generateLegal(board, rootMoves, false);
    if (rootMoves.size == 0)
    {
        std::cout << "bestmove 0000" << std::endl;
        return;
    }

    Move bestMove = rootMoves[0];
    int bestScore = 0;

    for (int depth = 1; depth <= maxDepth; ++depth)
    {
        if (debugEnabled_)
        {
            std::cout << "info string searching depth " << depth << std::endl;
        }
        int alpha = -INF;
        int beta = INF;
        if (depth >= 3)
        {
            alpha = std::max(-INF, bestScore - 35);
            beta = std::min(INF, bestScore + 35);
        }

        int score = alphaBeta(board, depth, alpha, beta, 0, true, Move{});
        if (!isStopRequested() && (score <= alpha || score >= beta))
        {
            score = alphaBeta(board, depth, -INF, INF, 0, true, Move{});
        }
        if (isStopRequested())
        {
            if (debugEnabled_)
            {
                std::cout << "info string search interrupted at depth " << depth << std::endl;
            }
            break;
        }

        if (pvLength_[0] > 0)
        {
            bestMove = pvTable_[0][0];
            bestScore = score;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto elapsedMs = std::max<long long>(
            1, std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime_).count());
        const auto nps = static_cast<long long>((nodes_ * 1000ULL) / elapsedMs);

        std::cout << "info depth " << depth
                  << " score " << formatScore(bestScore)
                  << " nodes " << nodes_
                  << " time " << elapsedMs
                  << " nps " << nps
                  << " pv " << pvToString()
                  << std::endl;
    }

    std::cout << "bestmove " << MoveGen::moveToUci(bestMove) << std::endl;
}

int Searcher::alphaBeta(Board &board, int depth, int alpha, int beta, int ply, bool allowNullMove, Move prevMove)
{
    pvLength_[ply] = ply;

    ++nodes_;
    if ((nodeLimit_ > 0 && nodes_ >= nodeLimit_) || ((nodes_ & 2047ULL) == 0ULL && timeUp()))
    {
        stopRequested_.store(true, std::memory_order_relaxed);
        return 0;
    }
    if (isStopRequested())
    {
        return 0;
    }

    if (ply >= MAX_PLY - 1)
    {
        return Eval::evaluate(board);
    }

    if (board.halfmoveClock >= 100 || board.isRepetition())
    {
        return 0;
    }

    const bool inCheck = board.inCheck(board.sideToMove);
    if (inCheck)
    {
        ++depth;
    }
    if (depth <= 0)
    {
        return quiescence(board, alpha, beta, ply);
    }

    if (allowNullMove && ply > 0 && depth >= 3 && !inCheck && beta < MATE_BOUND && hasNonPawnMaterial(board, board.sideToMove))
    {
        // Basic zugzwang safety: disable in low material endgames
        const int totalNonPawnMaterial = popcount(board.bitboards[WHITE][KNIGHT] | board.bitboards[WHITE][BISHOP] | board.bitboards[WHITE][ROOK] | board.bitboards[WHITE][QUEEN]) +
                                         popcount(board.bitboards[BLACK][KNIGHT] | board.bitboards[BLACK][BISHOP] | board.bitboards[BLACK][ROOK] | board.bitboards[BLACK][QUEEN]);
        if (totalNonPawnMaterial > 2)
        {
            UndoState nullUndo;
            board.makeNullMove(nullUndo);
            const int reduction = 2 + depth / 4; // Adaptive reduction
            const int score = -alphaBeta(board, depth - 1 - reduction, -beta, -beta + 1, ply + 1, false);
            board.unmakeNullMove(nullUndo);

            if (isStopRequested())
            {
                return 0;
            }
            if (score >= beta)
            {
                return beta;
            }
        }
    }

    Move ttMove;
    int ttScore = 0;
    if (ply > 0 && probeTT(board.hashKey, depth, alpha, beta, ply, ttMove, ttScore))
    {
        return ttScore;
    }

    const int originalAlpha = alpha;
    const int side = board.sideToMove;
    Move bestMove;
    MoveList moves;
    MoveGen::generatePseudoLegal(board, moves, false);

    std::array<int, MAX_MOVES> scores{};
    for (int i = 0; i < moves.size; ++i)
    {
        scores[i] = scoreMove(board, moves[i], ttMove, ply, prevMove);
    }

    int legalMoves = 0;
    for (int i = 0; i < moves.size; ++i)
    {
        int bestIndex = i;
        for (int j = i + 1; j < moves.size; ++j)
        {
            if (scores[j] > scores[bestIndex])
            {
                bestIndex = j;
            }
        }
        if (bestIndex != i)
        {
            std::swap(moves[i], moves[bestIndex]);
            std::swap(scores[i], scores[bestIndex]);
        }

        UndoState undo;
        if (!board.makeMove(moves[i], undo))
        {
            continue;
        }

        ++legalMoves;

        int score = 0;
        if (legalMoves == 1)
        {
            // First move: full window search
            score = -alphaBeta(board, depth - 1, -beta, -alpha, ply + 1);
        }
        else
        {
            // Remaining moves: null window search with LMR
            int lmrDepth = depth - 1;
            const bool quiet = isQuietMove(moves[i]);
            const bool isKiller = (moves[i] == killerMoves_[0][ply] || moves[i] == killerMoves_[1][ply]);
            const bool isGoodHistory = historyHeuristic_[side][moves[i].from()][moves[i].to()] > 1000;
            const bool canReduce = !inCheck && quiet && legalMoves >= 4 && !isKiller && !isGoodHistory;

            if (canReduce)
            {
                const double reduction = std::log(depth) * std::log(legalMoves) / 2.0;
                lmrDepth = std::max(1, depth - 1 - static_cast<int>(reduction));
            }

            score = -alphaBeta(board, lmrDepth, -alpha - 1, -alpha, ply + 1);

            // Re-search at full depth if reduced move improves alpha
            if (score > alpha && lmrDepth < depth - 1)
            {
                score = -alphaBeta(board, depth - 1, -alpha - 1, -alpha, ply + 1);
            }

            // If still improving, re-search with full window
            if (score > alpha)
            {
                score = -alphaBeta(board, depth - 1, -beta, -alpha, ply + 1);
            }
        }
        board.unmakeMove(undo);

        if (isStopRequested())
        {
            return 0;
        }

        if (score >= beta)
        {
            if (!moves[i].isCapture())
            {
                if (killerMoves_[0][ply] != moves[i])
                {
                    killerMoves_[1][ply] = killerMoves_[0][ply];
                    killerMoves_[0][ply] = moves[i];
                }
                historyHeuristic_[side][moves[i].from()][moves[i].to()] += depth * depth;
            }
            storeTT(board.hashKey, depth, beta, TT_BETA, moves[i], ply);
            return beta;
        }

        if (score > alpha)
        {
            alpha = score;
            bestMove = moves[i];
            updatePV(ply, moves[i]);
        }
    }

    if (legalMoves == 0)
    {
        return inCheck ? (-MATE_SCORE + ply) : 0;
    }

    storeTT(board.hashKey, depth, alpha, alpha > originalAlpha ? TT_EXACT : TT_ALPHA, bestMove, ply);
    return alpha;
}

int Searcher::quiescence(Board &board, int alpha, int beta, int ply)
{
    pvLength_[ply] = ply;

    ++nodes_;
    if ((nodeLimit_ > 0 && nodes_ >= nodeLimit_) || ((nodes_ & 2047ULL) == 0ULL && timeUp()))
    {
        stopRequested_.store(true, std::memory_order_relaxed);
        return 0;
    }
    if (isStopRequested())
    {
        return 0;
    }

    if (ply >= MAX_PLY - 1)
    {
        return Eval::evaluate(board);
    }

    if (board.halfmoveClock >= 100 || board.isRepetition())
    {
        return 0;
    }

    const bool inCheck = board.inCheck(board.sideToMove);
    int standPat = -MATE_SCORE; // Initialize to a low value
    if (!inCheck)
    {
        standPat = Eval::evaluate(board);
        // Delta pruning: if stand pat + queen value < alpha, no need to search further
        if (standPat + PIECE_VALUES[QUEEN] < alpha)
        {
            return alpha;
        }
        if (standPat >= beta)
        {
            return beta;
        }
        if (standPat > alpha)
        {
            alpha = standPat;
        }
    }

    MoveList moves;
    MoveGen::generatePseudoLegal(board, moves, false); // Generate all moves to include captures, promotions, and checks

    std::array<int, MAX_MOVES> scores{};
    for (int i = 0; i < moves.size; ++i)
    {
        scores[i] = scoreMove(board, moves[i], Move{}, ply, Move{});
    }

    int legalMoves = 0;
    for (int i = 0; i < moves.size; ++i)
    {
        int bestIndex = i;
        for (int j = i + 1; j < moves.size; ++j)
        {
            if (scores[j] > scores[bestIndex])
            {
                bestIndex = j;
            }
        }
        if (bestIndex != i)
        {
            std::swap(moves[i], moves[bestIndex]);
            std::swap(scores[i], scores[bestIndex]);
        }

        // Only search captures, promotions, and checks (checks optional, skipped for simplicity)
        if (!inCheck && !moves[i].isCapture() && !moves[i].isPromotion())
        {
            continue;
        }

        // Skip losing captures using SEE
        if (moves[i].isCapture() && Eval::see(board, moves[i]) < 0)
        {
            continue;
        }

        UndoState undo;
        if (!board.makeMove(moves[i], undo))
        {
            continue;
        }

        ++legalMoves;
        const int score = -quiescence(board, -beta, -alpha, ply + 1);
        board.unmakeMove(undo);

        if (isStopRequested())
        {
            return 0;
        }

        if (score >= beta)
        {
            return beta;
        }
        if (score > alpha)
        {
            alpha = score;
            updatePV(ply, moves[i]);
        }
    }

    if (inCheck && legalMoves == 0)
    {
        return -MATE_SCORE + ply;
    }

    return alpha;
}

int Searcher::scoreMove(const Board &board, Move move, Move ttMove, int ply, Move prevMove) const
{
    if (!ttMove.isNull() && move == ttMove)
    {
        return 2'000'000;
    }

    if (move.isCapture())
    {
        const int victim = move.captured() == NO_PIECE_TYPE ? PAWN : move.captured();
        return 1'000'000 + PIECE_VALUES[victim] * 10 - PIECE_VALUES[move.piece()];
    }

    if (move.isPromotion())
    {
        return 900'000 + PIECE_VALUES[move.promotion()];
    }

    if (killerMoves_[0][ply] == move)
    {
        return 800'000;
    }
    if (killerMoves_[1][ply] == move)
    {
        return 799'000;
    }

    if (!prevMove.isNull() && move == counterMove_[board.sideToMove][prevMove.from()][prevMove.to()])
    {
        return 750'000;
    }

    return historyHeuristic_[board.sideToMove][move.from()][move.to()];
}

void Searcher::updatePV(int ply, Move move)
{
    pvTable_[ply][ply] = move;
    for (int i = ply + 1; i < pvLength_[ply + 1]; ++i)
    {
        pvTable_[ply][i] = pvTable_[ply + 1][i];
    }
    pvLength_[ply] = std::max(ply + 1, pvLength_[ply + 1]);
}

bool Searcher::timeUp()
{
    if (isStopRequested())
    {
        return true;
    }
    if (hardTimeLimitMs_ < 0)
    {
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime_).count();
    if (elapsedMs >= hardTimeLimitMs_)
    {
        stopRequested_.store(true, std::memory_order_relaxed);
        return true;
    }
    return false;
}

long long Searcher::allocateTimeMs(const SearchLimits &limits, int side) const
{
    if (limits.movetime > 0)
    {
        return limits.movetime;
    }

    const int remaining = side == WHITE ? limits.wtime : limits.btime;
    const int increment = side == WHITE ? limits.winc : limits.binc;
    if (remaining <= 0)
    {
        return -1;
    }

    const int movesToGo = limits.movestogo > 0 ? limits.movestogo : 30;
    const int safety = std::min(50, remaining / 10);
    long long allocation = remaining / (movesToGo + 2);
    allocation += increment * 3 / 4;
    allocation = std::max<long long>(10, allocation);
    allocation = std::min<long long>(allocation, std::max(10, remaining - safety));
    return allocation;
}

std::string Searcher::formatScore(int score) const
{
    std::ostringstream out;
    if (score >= MATE_BOUND)
    {
        out << "mate " << (MATE_SCORE - score + 1) / 2;
    }
    else if (score <= -MATE_BOUND)
    {
        out << "mate " << -((MATE_SCORE + score + 1) / 2);
    }
    else
    {
        out << "cp " << score;
    }
    return out.str();
}

std::string Searcher::pvToString() const
{
    if (pvLength_[0] == 0)
    {
        return "";
    }

    std::ostringstream out;
    for (int i = 0; i < pvLength_[0]; ++i)
    {
        if (i > 0)
        {
            out << ' ';
        }
        out << MoveGen::moveToUci(pvTable_[0][i]);
    }
    return out.str();
}

void Searcher::storeTT(std::uint64_t key, int depth, int score, TTFlag flag, Move bestMove, int ply)
{
    TTEntry &entry = table_[key & (TT_SIZE - 1)];
    if (entry.key != key || depth >= entry.depth)
    {
        entry.key = key;
        entry.move = bestMove.value;
        entry.score = packMateScore(score, ply);
        entry.depth = depth;
        entry.flag = flag;
    }
}

bool Searcher::probeTT(std::uint64_t key, int depth, int alpha, int beta, int ply, Move &ttMove, int &score) const
{
    const TTEntry &entry = table_[key & (TT_SIZE - 1)];
    if (entry.key != key)
    {
        return false;
    }

    ttMove.value = entry.move;
    if (entry.depth < depth)
    {
        return false;
    }

    score = unpackMateScore(entry.score, ply);
    if (entry.flag == TT_EXACT)
    {
        return true;
    }
    if (entry.flag == TT_ALPHA && score <= alpha)
    {
        return true;
    }
    if (entry.flag == TT_BETA && score >= beta)
    {
        return true;
    }
    return false;
}

int Searcher::packMateScore(int score, int ply)
{
    if (score >= MATE_BOUND)
    {
        return score + ply;
    }
    if (score <= -MATE_BOUND)
    {
        return score - ply;
    }
    return score;
}

int Searcher::unpackMateScore(int score, int ply)
{
    if (score >= MATE_BOUND)
    {
        return score - ply;
    }
    if (score <= -MATE_BOUND)
    {
        return score + ply;
    }
    return score;
}
