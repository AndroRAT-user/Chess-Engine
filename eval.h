#pragma once

#include "board.h"

namespace Eval
{

    int evaluate(const Board &board);
    int see(const Board &board, Move move);

} // namespace Eval
