#pragma once

#include "board.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <vector>

struct SearchLimits
{
    int depth = 0;
    int movetime = 0;
    int wtime = 0;
    int btime = 0;
    int winc = 0;
    int binc = 0;
    int movestogo = 0;
    std::uint64_t nodes = 0;
    bool infinite = false;
};

class Searcher
{
public:
    Searcher();

    void clear();
    void requestStop();
    void resetStop();
    bool isStopRequested() const;
    void setDebug(bool enabled);
    bool debugEnabled() const;

    void search(Board board, const SearchLimits &limits);

private:
    struct TTEntry
    {
        std::uint64_t key = 0ULL;
        std::uint32_t move = 0U;
        int score = 0;
        int depth = -1;
        std::uint8_t flag = 0;
    };

    enum TTFlag : std::uint8_t
    {
        TT_ALPHA = 0,
        TT_BETA = 1,
        TT_EXACT = 2
    };

    static constexpr int INF = 32000;
    static constexpr int MATE_SCORE = 30000;
    static constexpr int MATE_BOUND = 29000;
    static constexpr std::size_t TT_SIZE = 1 << 20;

    int alphaBeta(Board &board, int depth, int alpha, int beta, int ply, bool allowNullMove, Move prevMove);
    int quiescence(Board &board, int alpha, int beta, int ply);
    int scoreMove(const Board &board, Move move, Move ttMove, int ply, Move prevMove) const;
    void updatePV(int ply, Move move);
    bool timeUp();
    long long allocateTimeMs(const SearchLimits &limits, int side) const;
    std::string formatScore(int score) const;
    std::string pvToString() const;

    void storeTT(std::uint64_t key, int depth, int score, TTFlag flag, Move bestMove, int ply);
    bool probeTT(std::uint64_t key, int depth, int alpha, int beta, int ply, Move &ttMove, int &score) const;
    static int packMateScore(int score, int ply);
    static int unpackMateScore(int score, int ply);

    std::vector<TTEntry> table_;
    std::array<std::array<Move, MAX_PLY>, 2> killerMoves_{};
    std::array<std::array<std::array<int, BOARD_SQUARES>, BOARD_SQUARES>, COLOR_NB> historyHeuristic_{};
    std::array<std::array<std::array<Move, BOARD_SQUARES>, BOARD_SQUARES>, COLOR_NB> counterMove_{};
    std::array<std::array<Move, MAX_PLY>, MAX_PLY> pvTable_{};
    std::array<int, MAX_PLY> pvLength_{};

    std::atomic<bool> stopRequested_{false};
    std::chrono::steady_clock::time_point startTime_{};
    long long hardTimeLimitMs_ = -1;
    std::uint64_t nodes_ = 0;
    std::uint64_t nodeLimit_ = 0;
    bool debugEnabled_ = false;
};
