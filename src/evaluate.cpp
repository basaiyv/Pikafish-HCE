/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2022 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>   // For std::memset
#include <fstream>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <streambuf>
#include <vector>

#include "bitboard.h"
#include "evaluate.h"
#include "misc.h"
#include "thread.h"
#include "uci.h"

using namespace std;

namespace Stockfish {

namespace Eval {

  string currentEvalFileName = "None";

  /// NNUE::init() tries to load a NNUE network at startup time, or when the engine
  /// receives a UCI command "setoption name EvalFile value .*.nnue"
  /// The name of the NNUE network is always retrieved from the EvalFile option.
  /// We search the given network in two locations: in the active working directory and
  /// in the engine directory.

  void NNUE::init() {

    string eval_file = string(Options["EvalFile"]);
    if (eval_file.empty())
        eval_file = EvalFileDefaultName;

    vector<string> dirs = { "" , CommandLine::binaryDirectory };

    for (string directory : dirs)
        if (currentEvalFileName != eval_file)
        {
            ifstream stream(directory + eval_file, ios::binary);
            stringstream ss = read_zipped_nnue(directory + eval_file);
            if (load_eval(eval_file, stream) || load_eval(eval_file, ss))
                currentEvalFileName = eval_file;
        }
  }

  /// NNUE::verify() verifies that the last net used was loaded successfully
  void NNUE::verify() {

    return;
  }
}

namespace Trace {

    enum Tracing { NO_TRACE, TRACE };

    enum Term { // The first 8 entries are reserved for PieceType
        MATERIAL = 8, IMBALANCE, PAIR, MOBILITY, THREAT, PASSED, SPACE, WINNABLE, TOTAL, TERM_NB
    };

    Score scores[TERM_NB][COLOR_NB];

    double to_cp(Value v) { return double(v) / PawnValueEg; }

    static void add(int idx, Color c, Score s) {
        scores[idx][c] = s;
    }

    static void add(int idx, Score w, Score b = SCORE_ZERO) {
        scores[idx][WHITE] = w;
        scores[idx][BLACK] = b;
    }

    static std::ostream& operator<<(std::ostream& os, Score s) {
        os << std::setw(5) << to_cp(mg_value(s)) << " "
            << std::setw(5) << to_cp(eg_value(s));
        return os;
    }

    static std::ostream& operator<<(std::ostream& os, Term t) {

        if (t == MATERIAL || t == IMBALANCE || t == WINNABLE || t == TOTAL)
            os << " ----  ----" << " | " << " ----  ----";
        else
            os << scores[t][WHITE] << " | " << scores[t][BLACK];

        os << " | " << scores[t][WHITE] - scores[t][BLACK] << " |\n";
        return os;
    }
}

using namespace Trace;

namespace {

#define S(mg, eg) make_score(mg, eg)
    Score HollowCannon = S(85, 91);
    Score CentralKnight = S(50, 53);
    Score BottomCannon = S(18, 8);
    Score AdvisorBishopPair = S(24, -43);
    Score CrossedPawn[6] = {
        S(-58, -7), S(19, 0), S(11, -11), S(-23, 6), S(-11, -7), S(-17, -13)
    };
    Score ConnectedPawn = S(5, -5);

    // Polynomial material imbalance parameters

    // One Score parameter for each pair (our piece, another of our pieces)
    Score QuadraticOurs[][PIECE_TYPE_NB] = {
        // OUR PIECE 2
        // rook   advisor  cannon   pawn     knight    bishop
        {S(71, 3)                                             }, // Rook
        {S(24, 74), S(44, -67)                                }, // Advisor
        {S(48, 72), S(33, 62), S(-5, -63)                     }, // Cannon      OUR PIECE 1
        {S(75, -14), S(31, 44), S(-3, 28), S(-11, 11)         }, // Pawn
        {S(-92, 53), S(27, -9), S(-3, 234), S(44, 88), S(-30, -29)}, // Knight
        {S(54, 104), S(175, -103), S(106, -64), S(43, -113), S(24, 6), S(2, -59)}  // Bishop
    };

    // One Score parameter for each pair (our piece, their piece)
    Score QuadraticTheirs[][PIECE_TYPE_NB] = {
        // THEIR PIECE
        // rook   advisor  cannon   pawn     knight    bishop
        {S(-35, -46)                                           }, // Rook
        {S(-92, 32), S(138, -7)                                }, // Advisor
        {S(-83, 13), S(-41, 43), S(20, 28)                     }, // Cannon      OUR PIECE
        {S(-2, 13), S(-57, -118), S(-18, 121), S(70, -58)      }, // Pawn
        {S(-37, 17), S(14, -86), S(38, -24), S(67, 43), S(-21, -42) }, // Knight
        {S(72, 38), S(6, -79), S(24, -2), S(48, 30), S(30, 14), S(-51, 35) }  // Bishop
    };
    Score mobilityBonus[PIECE_TYPE_NB][2] = {
        {}, // NO_PIECE_TYPE
        {S(7, 11), S(-18, -28)}, // ROOK
        {S(8, 4), S(-3, -13)}, // ADVISOR
        {S(0, 0), S(-1, 0)}, // CANNON
        {}, // PAWN
        {S(11, 8), S(-3, -27)}, // KNIGHT
        {S(5, 4), S(-2, -27)}, // BISHOP
    };
#undef S

    // Evaluation class computes and stores attacks tables and other working data
    template<Tracing T>
    class Evaluation {

    public:
        Evaluation() = delete;
        explicit Evaluation(const Position& p) : pos(p) {}
        Evaluation& operator=(const Evaluation&) = delete;
        Value value();

    private:
        template<Color Us> void initialize();
        template<Color Us, PieceType Pt> Score pieces();
        template<Color Us> Score threat();
        template<Color Us> Score imbalance();
        Value winnable(Score score) const;

        const Position& pos;

        // attackedBy[color][piece type] is a bitboard representing all squares
        // attacked by a given color and piece type. Special "piece types" which
        // is also calculated is ALL_PIECES.
        Bitboard attackedBy[COLOR_NB][PIECE_TYPE_NB];

        // attackedBy2[color] are the squares attacked by at least 2 units of a given
        // color, including x-rays. But diagonal x-rays through pawns are not computed.
        Bitboard attackedBy2[COLOR_NB];

        Score mobility[COLOR_NB] = { SCORE_ZERO, SCORE_ZERO };
    };


    // Evaluation::initialize() computes king and pawn attacks, and the king ring
    // bitboard for a given color. This is done at the beginning of the evaluation.

    template<Tracing T> template<Color Us>
    void Evaluation<T>::initialize() {

        constexpr Color     Them = ~Us;
        const Square ksq = pos.square<KING>(Us);
        constexpr Bitboard LowRanks = (Us == WHITE ? Rank0BB | Rank1BB : Rank8BB | Rank9BB);


        // Initialize attackedBy[] for king and pawns
        attackedBy[Us][KING] = attacks_bb<KING>(ksq);
        attackedBy[Us][PAWN] = pawn_attacks_bb<Us>(pos.pieces(Us, PAWN));
        attackedBy[Us][ALL_PIECES] = attackedBy[Us][KING] | attackedBy[Us][PAWN];
        attackedBy2[Us] = attackedBy[Us][KING] & attackedBy[Us][PAWN];
        // Find our pawns that are on the first two ranks
        Bitboard b0 = pos.pieces(Us, PAWN) & LowRanks;

    }


    // Evaluation::pieces() scores pieces of a given color and type

    template<Tracing T> template<Color Us, PieceType Pt>
    Score Evaluation<T>::pieces() {

        constexpr Color Them = ~Us;
        const Square ksq = pos.square<KING>(Them);
        Bitboard b1 = pos.pieces(Us, Pt);
        Bitboard b;
        Score score = SCORE_ZERO;

        attackedBy[Us][Pt] = 0;

        while (b1)
        {
            Square s = pop_lsb(b1);

            // Find attacked squares, including x-ray attacks for bishops and rooks
            b = attacks_bb<Pt>(s, pos.pieces());

            if (pos.blockers_for_king(Us) & s)
                b &= line_bb(pos.square<KING>(Us), s);

            attackedBy2[Us] |= attackedBy[Us][ALL_PIECES] & b;
            attackedBy[Us][Pt] |= b;
            attackedBy[Us][ALL_PIECES] |= b;

            int mob = popcount(b & ~attackedBy[Them][PAWN]);
            mobility[Us] += mobilityBonus[Pt][0] * mob + mobilityBonus[Pt][1];

            if constexpr (Pt == CANNON) { // 炮的评估 (~5 Elo)
                int blocker = popcount(between_bb(s, ksq) & pos.pieces()) - 1;
                const Bitboard originalAdvisor = square_bb(SQ_D0) | square_bb(SQ_D9) | square_bb(SQ_F0) | square_bb(SQ_F9);
                Bitboard advisorBB = pos.pieces(Them, ADVISOR);
                if (file_of(s) == FILE_E && (ksq == SQ_E0 || ksq == SQ_E9) && popcount(originalAdvisor & advisorBB) == 2) {
                    if (!blocker) { // 空头炮
                        score += HollowCannon;
                    }
                    if (blocker == 2 && (between_bb(s, ksq) & pos.pieces(Them, KNIGHT) & attackedBy[Them][KING])) { // 炮镇窝心马
                        score += CentralKnight;
                    }
                }
                Rank enemyBottom = (Us == WHITE ? RANK_9 : RANK_0);
                Square enemyCenter = (Us == WHITE ? SQ_E8 : SQ_E1);
                if (rank_of(s) == enemyBottom && !blocker && (ksq == SQ_E0 || ksq == SQ_E9) && (pos.pieces(Them) & enemyCenter)) { // 沉底炮
                    score += BottomCannon;
                }
            }
        }
        return score;
    }

    template<Tracing T> template<Color Us>
    Score Evaluation<T>::threat() {
        Score score = SCORE_ZERO; // 初始化
        constexpr Color Them = ~Us;
        // 士象全
        if (pos.count<ADVISOR>(Us) + pos.count<BISHOP>(Us) == 4)
            score += AdvisorBishopPair;
        // 过河兵 (~8.5 Elo)
        constexpr Bitboard crossed = (Us == WHITE ? (Rank5BB | Rank6BB | Rank7BB | Rank8BB) : (Rank1BB | Rank2BB | Rank3BB | Rank4BB)); // 底线不算
        int crossedPawnCnt = popcount(crossed & pos.pieces(Us, PAWN));
        score += CrossedPawn[crossedPawnCnt];
        // 牵手兵
        score += ConnectedPawn * popcount(shift<EAST>(pos.pieces(Us, PAWN)) & pos.pieces(Us, PAWN));
        return score;
    }

    /// imbalance() calculates the imbalance by comparing the piece count of each
    /// piece type for both colors.

    template<Tracing T> template<Color Us>
    Score Evaluation<T>::imbalance() {

        const int pieceCount[COLOR_NB][PIECE_TYPE_NB] = {
        { pos.count<ROOK>(WHITE), pos.count<ADVISOR>(WHITE), pos.count<CANNON>(WHITE),
          pos.count<PAWN>(WHITE), pos.count<KNIGHT >(WHITE), pos.count<BISHOP>(WHITE) },
        { pos.count<ROOK>(BLACK), pos.count<ADVISOR>(BLACK), pos.count<CANNON>(BLACK),
          pos.count<PAWN>(BLACK), pos.count<KNIGHT >(BLACK), pos.count<BISHOP>(BLACK) }
        };

        constexpr Color Them = ~Us;

        Score bonus = SCORE_ZERO;

        // Second-degree polynomial material imbalance, by Tord Romstad
        for (int pt1 = NO_PIECE_TYPE; pt1 < BISHOP; ++pt1)
        {
            if (!pieceCount[Us][pt1])
                continue;

            int v = QuadraticOurs[pt1][pt1] * pieceCount[Us][pt1];

            for (int pt2 = NO_PIECE_TYPE; pt2 < pt1; ++pt2)
                v += QuadraticOurs[pt1][pt2] * pieceCount[Us][pt2]
                + QuadraticTheirs[pt1][pt2] * pieceCount[Them][pt2];

            bonus += pieceCount[Us][pt1] * v;
        }

        return bonus;
    }


    // Evaluation::winnable() adjusts the midgame and endgame score components, based on
    // the known attacking/defending status of the players. The final value is derived
    // by interpolation from the midgame and endgame values.

    template<Tracing T>
    Value Evaluation<T>::winnable(Score score) const {
        const int MidgameLimit = 15258, EndgameLimit = 3915;
        Value sum = pos.material_sum();
        int gamePhase = (((int)sum - EndgameLimit) * 128) / (MidgameLimit - EndgameLimit);
        Value mg = mg_value(score), eg = eg_value(score);
        Value v = mg * int(gamePhase)
            + eg * int(128 - gamePhase);
        v /= 128;
        return v;
    }


    // Evaluation::value() is the main function of the class. It computes the various
    // parts of the evaluation and returns the value of the position from the point
    // of view of the side to move.

    template<Tracing T>
    Value Evaluation<T>::value() {

        Score score = pos.psq_score() + (imbalance<WHITE>() - imbalance<BLACK>()) / 16;

        if constexpr (T) {
            Trace::add(MATERIAL, pos.psq_score());
            Trace::add(IMBALANCE, (imbalance<WHITE>() - imbalance<BLACK>()) / 16);
        }

        // Main evaluation begins here
        initialize<WHITE>();
        initialize<BLACK>();

        score += pieces<WHITE, KNIGHT>() - pieces<BLACK, KNIGHT>()
            + pieces<WHITE, BISHOP>() - pieces<BLACK, BISHOP>()
            + pieces<WHITE, ROOK>() - pieces<BLACK, ROOK>()
            + pieces<WHITE, ADVISOR>() - pieces<BLACK, ADVISOR>()
            + pieces<WHITE, CANNON>() - pieces<BLACK, CANNON>();

        score += threat<WHITE>() - threat<BLACK>();

        score += mobility[WHITE] - mobility[BLACK];

        if constexpr (T) {
            Trace::add(THREAT, threat<WHITE>(), threat<BLACK>());
            Trace::add(MOBILITY, mobility[WHITE], mobility[BLACK]);
            Trace::add(TOTAL, score);
        }

        // Derive single value from mg and eg parts of score
        Value v = winnable(score);

        // Side to move point of view
        v = (pos.side_to_move() == WHITE ? v : -v);

        return v;
    }

} // namespace Eval

using namespace Trace;

/// evaluate() is the evaluator for the outer world. It returns a static
/// evaluation of the position from the point of view of the side to move.
int rule60_a = 118, rule60_b = 221;

Value Eval::evaluate(const Position& pos, int* complexity) {

  if (complexity)
      *complexity = 0;
  Value v = Evaluation<NO_TRACE>(pos).value();

  // Damp down the evaluation linearly when shuffling
  v = v * (rule60_a - pos.rule60_count()) / rule60_b;

  // Guarantee evaluation does not hit the mate range
  v = std::clamp(v, VALUE_MATED_IN_MAX_PLY + 1, VALUE_MATE_IN_MAX_PLY - 1);
  return v;
}

// format_cp_compact() converts a Value into (centi)pawns and writes it in a buffer.
// The buffer must have capacity for at least 5 chars.
static void format_cp_compact(Value v, char* buffer) {

    buffer[0] = (v < 0 ? '-' : v > 0 ? '+' : ' ');

    int cp = std::abs(100 * v / PawnValueEg);
    if (cp >= 10000)
    {
        buffer[1] = '0' + cp / 10000; cp %= 10000;
        buffer[2] = '0' + cp / 1000; cp %= 1000;
        buffer[3] = '0' + cp / 100;
        buffer[4] = ' ';
    }
    else if (cp >= 1000)
    {
        buffer[1] = '0' + cp / 1000; cp %= 1000;
        buffer[2] = '0' + cp / 100; cp %= 100;
        buffer[3] = '.';
        buffer[4] = '0' + cp / 10;
    }
    else
    {
        buffer[1] = '0' + cp / 100; cp %= 100;
        buffer[2] = '.';
        buffer[3] = '0' + cp / 10; cp %= 10;
        buffer[4] = '0' + cp / 1;
    }
}

/// trace() is like evaluate(), but instead of returning a value, it returns
/// a string (suitable for outputting to stdout) that contains the detailed
/// descriptions and values of each evaluation term. Useful for debugging.
/// Trace scores are from white's point of view

std::string Eval::trace(Position& pos) {

    if (pos.checkers())
        return "Final evaluation: none (in check)";

    std::stringstream ss;
    ss << std::showpoint << std::noshowpos << std::fixed << std::setprecision(2);

    Value v;

    // Reset any global variable used in eval
    pos.this_thread()->bestValue = VALUE_ZERO;
    pos.this_thread()->optimism[WHITE] = VALUE_ZERO;
    pos.this_thread()->optimism[BLACK] = VALUE_ZERO;

    char board[3 * RANK_NB + 1][8 * FILE_NB + 2];
    std::memset(board, ' ', sizeof(board));
    for (int row = 0; row < 3 * RANK_NB + 1; ++row)
        board[row][8 * FILE_NB + 1] = '\0';
    // A lambda to output one box of the board
    auto writeSquare = [&board](File file, Rank rank, Piece pc, Value value) {
        const std::string PieceToChar(" RACPNBK racpnbk XXXXXX  xxxxxx");
        const int x = ((int)file) * 8;
        const int y = (RANK_9 - (int)rank) * 3;
        for (int i = 1; i < 8; ++i)
            board[y][x + i] = board[y + 3][x + i] = '-';
        for (int i = 1; i < 3; ++i)
            board[y + i][x] = board[y + i][x + 8] = '|';
        board[y][x] = board[y][x + 8] = board[y + 3][x + 8] = board[y + 3][x] = '+';
        if (pc != NO_PIECE)
            board[y + 1][x + 4] = PieceToChar[pc];
        if (value != VALUE_NONE)
            format_cp_compact(value, &board[y + 2][x + 2]);
    };
    // We estimate the value of each piece by doing a differential evaluation from
    // the current base eval, simulating the removal of the piece from its square.
    Value base = evaluate(pos);
    base = pos.side_to_move() == WHITE ? base : -base;
    for (File f = FILE_A; f <= FILE_I; ++f)
        for (Rank r = RANK_0; r <= RANK_9; ++r)
        {
            Square sq = make_square(f, r);
            Piece pc = pos.piece_on(sq);
            Value v = VALUE_NONE;
            if (pc != NO_PIECE && type_of(pc) != KING)
            {
                auto st = pos.state();
                pos.remove_piece(sq);
                Value eval = evaluate(pos);
                eval = pos.side_to_move() == WHITE ? eval : -eval;
                v = base - eval;
                pos.put_piece(pc, sq);
            }
            writeSquare(f, r, pc, v);
        }
    ss << "HCE derived piece values:\n";
    for (int row = 0; row < 3 * RANK_NB + 1; ++row)
        ss << board[row] << '\n';
    ss << '\n';

    v = Evaluation<TRACE>(pos).value();

    ss << std::showpoint << std::noshowpos << std::fixed << std::setprecision(2)
        << " Contributing terms for the classical eval:\n"
        << "+------------+-------------+-------------+-------------+\n"
        << "|    Term    |    White    |    Black    |    Total    |\n"
        << "|            |   MG    EG  |   MG    EG  |   MG    EG  |\n"
        << "+------------+-------------+-------------+-------------+\n"
        << "|   Material | " << Term(MATERIAL)
        << "|  Imbalance | " << Term(IMBALANCE)
        << "|       Pair | " << Term(PAIR)
        << "|      Pawns | " << Term(PAWN)
        << "|    Knights | " << Term(KNIGHT)
        << "|    Bishops | " << Term(BISHOP)
        << "|      Rooks | " << Term(ROOK)
        << "|   Mobility | " << Term(MOBILITY)
        << "|King safety | " << Term(KING)
        << "|    Threats | " << Term(THREAT)
        << "|     Passed | " << Term(PASSED)
        << "|      Space | " << Term(SPACE)
        << "|   Winnable | " << Term(WINNABLE)
        << "+------------+-------------+-------------+-------------+\n"
        << "|      Total | " << Term(TOTAL)
        << "+------------+-------------+-------------+-------------+\n";

    ss << std::showpoint << std::showpos << std::fixed << std::setprecision(2) << std::setw(15);

    v = evaluate(pos);
    v = pos.side_to_move() == WHITE ? v : -v;
    ss << "Final evaluation       " << to_cp(v) << " (white side) [with scaled NNUE, optimism, ...]\n";

    return ss.str();
}

} // namespace Stockfish