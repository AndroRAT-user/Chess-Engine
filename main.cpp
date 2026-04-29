#include "board.h"
#include "movegen.h"
#include "search.h"

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace {

std::uint64_t perft(Board& board, int depth) {
    if (depth == 0) {
        return 1ULL;
    }

    MoveList moves;
    MoveGen::generatePseudoLegal(board, moves, false);

    std::uint64_t nodes = 0ULL;
    for (int i = 0; i < moves.size; ++i) {
        UndoState undo;
        if (!board.makeMove(moves[i], undo)) {
            continue;
        }
        nodes += perft(board, depth - 1);
        board.unmakeMove(undo);
    }
    return nodes;
}

SearchLimits parseGoCommand(std::istringstream& stream) {
    SearchLimits limits;
    std::string token;
    while (stream >> token) {
        if (token == "depth") stream >> limits.depth;
        else if (token == "movetime") stream >> limits.movetime;
        else if (token == "wtime") stream >> limits.wtime;
        else if (token == "btime") stream >> limits.btime;
        else if (token == "winc") stream >> limits.winc;
        else if (token == "binc") stream >> limits.binc;
        else if (token == "movestogo") stream >> limits.movestogo;
        else if (token == "nodes") stream >> limits.nodes;
        else if (token == "infinite") limits.infinite = true;
    }
    return limits;
}

bool applyPositionCommand(Board& board, const std::string& line) {
    Board working = board;
    std::istringstream stream(line);
    std::string token;
    stream >> token;  // position
    if (!(stream >> token)) {
        return false;
    }

    if (token == "startpos") {
        working.setStartPos();
    } else if (token == "fen") {
        std::string fen;
        std::string part;
        int collected = 0;
        while (stream >> part) {
            if (part == "moves") {
                break;
            }
            if (!fen.empty()) {
                fen.push_back(' ');
            }
            fen += part;
            ++collected;
            if (collected == 6) {
                break;
            }
        }
        if (!working.setFen(fen)) {
            return false;
        }
        if (part != "moves") {
            token = part;
        } else {
            token = "moves";
        }
    } else {
        return false;
    }

    if (token != "moves") {
        while (stream >> token) {
            if (token == "moves") {
                break;
            }
        }
    }

    if (token == "moves") {
        while (stream >> token) {
            Move move = MoveGen::parseMove(working, token);
            if (move.isNull()) {
                return false;
            }
            UndoState undo;
            if (!working.makeMove(move, undo)) {
                return false;
            }
        }
    }

    board = working;
    return true;
}

void printHelp() {
    std::cout
        << "HearthBit is a UCI chess engine.\n"
        << "Common commands:\n"
        << "  uci\n"
        << "  isready\n"
        << "  position startpos\n"
        << "  go depth 3\n"
        << "  stop\n"
        << "  quit\n"
        << "Extra terminal helpers:\n"
        << "  help\n"
        << "  perft N\n"
        << "  selftest\n"
        << "  debug on|off\n"
        << std::endl;
}

bool runSelfTest(Searcher& searcher) {
    Board board;
    Board kiwipete;
    if (!kiwipete.setFen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1")) {
        std::cout << "selftest: failed to load Kiwipete FEN" << std::endl;
        return false;
    }

    bool ok = true;
    Board startCopy = board;
    const std::uint64_t start1 = perft(startCopy, 1);
    startCopy = board;
    const std::uint64_t start2 = perft(startCopy, 2);
    startCopy = board;
    const std::uint64_t start3 = perft(startCopy, 3);

    ok &= start1 == 20;
    ok &= start2 == 400;
    ok &= start3 == 8902;

    Board kiwiCopy = kiwipete;
    const std::uint64_t kiwi1 = perft(kiwiCopy, 1);
    kiwiCopy = kiwipete;
    const std::uint64_t kiwi2 = perft(kiwiCopy, 2);
    kiwiCopy = kiwipete;
    const std::uint64_t kiwi3 = perft(kiwiCopy, 3);

    ok &= kiwi1 == 48;
    ok &= kiwi2 == 2039;
    ok &= kiwi3 == 97862;

    std::cout << "selftest startpos: " << start1 << ", " << start2 << ", " << start3 << std::endl;
    std::cout << "selftest kiwipete: " << kiwi1 << ", " << kiwi2 << ", " << kiwi3 << std::endl;

    SearchLimits limits;
    limits.depth = 3;
    searcher.search(board, limits);

    std::cout << (ok ? "selftest: ok" : "selftest: failed") << std::endl;
    return ok;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    std::cout << std::unitbuf;

    MoveGen::initAttackTables();

    Board board;
    Searcher searcher;
    std::thread searchThread;
    bool debugMode = false;

    if (argc > 1) {
        const std::string arg = argv[1];
        if (arg == "--help" || arg == "help") {
            printHelp();
            return 0;
        }
        if (arg == "--selftest") {
            return runSelfTest(searcher) ? 0 : 1;
        }
    }

    const auto stopSearch = [&]() {
        searcher.requestStop();
        if (searchThread.joinable()) {
            searchThread.join();
        }
        searcher.resetStop();
    };

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }

        std::istringstream stream(line);
        std::string command;
        stream >> command;

        if (command == "uci") {
            std::cout << "id name HearthBit" << '\n';
            std::cout << "id author OpenAI" << '\n';
            std::cout << "uciok" << std::endl;
        } else if (command == "isready") {
            std::cout << "readyok" << std::endl;
        } else if (command == "debug") {
            std::string mode;
            stream >> mode;
            debugMode = (mode == "on");
            searcher.setDebug(debugMode);
            std::cout << "info string debug " << (debugMode ? "on" : "off") << std::endl;
        } else if (command == "ucinewgame") {
            stopSearch();
            searcher.clear();
            searcher.setDebug(debugMode);
            board.setStartPos();
        } else if (command == "position") {
            stopSearch();
            if (!applyPositionCommand(board, line)) {
                std::cout << "info string invalid position command" << std::endl;
            }
        } else if (command == "go") {
            stopSearch();
            SearchLimits limits = parseGoCommand(stream);
            Board boardCopy = board;
            searchThread = std::thread([&searcher, boardCopy, limits]() mutable {
                searcher.search(boardCopy, limits);
            });
        } else if (command == "stop") {
            stopSearch();
        } else if (command == "quit") {
            stopSearch();
            break;
        } else if (command == "help") {
            stopSearch();
            printHelp();
        } else if (command == "d") {
            stopSearch();
            std::cout << board.toString() << std::flush;
        } else if (command == "perft") {
            stopSearch();
            int depth = 1;
            stream >> depth;
            Board copy = board;
            const auto nodes = perft(copy, depth);
            std::cout << "nodes " << nodes << std::endl;
        } else if (command == "selftest") {
            stopSearch();
            runSelfTest(searcher);
        } else if (command == "setoption" || command == "ponderhit") {
            continue;
        } else {
            std::cout << "info string unknown command: " << command << std::endl;
        }
    }

    if (searchThread.joinable()) {
        searcher.requestStop();
        searchThread.join();
    }

    return 0;
}
