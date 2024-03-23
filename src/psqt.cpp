/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2023 The Stockfish developers (see AUTHORS file)

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


#include "psqt.h"

#include <algorithm>

#include "bitboard.h"
#include "types.h"

namespace Stockfish {

namespace
{

auto constexpr S = make_score;

// 'Bonus' contains Piece-Square parameters.
// Scores are explicit for files A to E, implicitly mirrored for E to I.
Score Bonus[][RANK_NB][int(FILE_NB) / 2 + 1] = {
  { },
  { // ROOK
   { S(-218,-132), S(  24,-242), S(-143, -73), S( -31,  -5), S(   7, -45)},
   { S(-186, -25), S(  60, -45), S( -71,-123), S( -86, 122), S(-107, 101)},
   { S(-157, -20), S(   5, -83), S( -39,  47), S(-132,   6), S( -20,  46)},
   { S( -31,  77), S( -14, -53), S( -71,  50), S(  89,  49), S( 166, -20)},
   { S( -91,  27), S(  28,  -6), S(-114,  19), S( 188,  18), S(-165,  -6)},
   { S( -41,  -7), S( 237,  30), S(  86, -55), S(  80, -84), S( 118,  65)},
   { S(-148, 135), S( -20, -78), S( 154, -29), S(  51,  11), S(  22,  78)},
   { S( 145, 150), S(  53,-137), S(  92, 122), S(  88,  93), S( -52, -38)},
   { S( -46, -43), S( 180, -10), S( 127, -41), S(  29,  35), S( -77,-118)},
   { S( 116, -64), S(  67, -68), S( -77,   0), S(  18,  58), S(  10,  34)}
  },
  { // ADVISOR
   { S(   0,   0), S(   0,   0), S(   0,   0), S(  20,  44), S(   0,   0)},
   { S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(  29, 119)},
   { S(   0,   0), S(   0,   0), S(   0,   0), S(-185,  75), S(   0,   0)},
   { S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0)},
   { S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0)},
   { S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0)},
   { S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0)},
   { S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0)},
   { S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0)},
   { S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0)}
  },
  { // CANNON
   { S( -21,   0), S( -62, -69), S( -30, -64), S(  80,  90), S( -37,  -9)},
   { S(  26,  -5), S( 145, -41), S( -68,   7), S(   8,  22), S(  99,  29)},
   { S( -34, -62), S(  57, -17), S(  16,  93), S(  79,  88), S( 216,  66)},
   { S(  -5,  77), S(-217, -20), S( 117,  84), S(  76,-153), S(  29, -66)},
   { S( -24, -27), S( -73,  48), S(-105,  11), S( -93, 102), S( 248, -35)},
   { S( -68,  -4), S(  31, -87), S(   5,  39), S( -11,  19), S(  92,  76)},
   { S(  47,-211), S(  90, -84), S(  28, -43), S(  21,  62), S(  54, 156)},
   { S( 120,-133), S(  37, 133), S(  12, -23), S(  57, -99), S( 151,  69)},
   { S( -28, 105), S(  54,   3), S(  20,  18), S( -64, -85), S(  61,  66)},
   { S(  89, -49), S( 225, -15), S( -20,  20), S( 163, 114), S(  18,  -3)}
  },
  { // PAWN
   { S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0)},
   { S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0)},
   { S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0)},
   { S(  -8,   1), S(   0,   0), S( -26, -32), S(   0,   0), S(  30,  30)},
   { S(  15,   8), S(   0,   0), S(  57, -17), S(   0,   0), S(  52,  31)},
   { S( -19,  93), S( -57,  91), S( 123,  58), S(  30, 199), S(  76,  52)},
   { S( -80,  64), S(-160, 102), S(  65, 178), S(  21, 196), S( -77,  20)},
   { S(  88, -87), S(-133,  83), S(  26,  16), S(  19,  33), S(-132, -65)},
   { S(-176,-149), S(  35,  42), S( 191,  23), S(  -6, -14), S(  74,   0)},
   { S( 116, -54), S(  82, -95), S( -79,  10), S(  28, -19), S(  27,-100)}
  },
  { // KNIGHT
   { S( -18, -23), S(-183,-252), S( -18, -38), S(-136,-152), S( -81,  43)},
   { S(-126, -87), S(-100,  78), S(-133, -16), S( -25, -97), S( -45,-118)},
   { S(-108,-119), S(  50, -69), S(   1, -91), S( -21,  16), S(  14,   8)},
   { S(  25,   7), S( -72,-116), S( 120,  -7), S( 154, -87), S( -81,  87)},
   { S(-123,  15), S( -24, 129), S( -22,  88), S( -19,-103), S(  69,  61)},
   { S( 119, -57), S(  85,  73), S(  60, -69), S( 157,  79), S( -72, 123)},
   { S( -44, -61), S(  72,  34), S( 168,  60), S(  25,  21), S( -90,   7)},
   { S(  97, -54), S( -87,-122), S( -24,  24), S(  78, 141), S( -29, 194)},
   { S(-102, -45), S( -83, -44), S( -35,  58), S( -59, -66), S( 155, 121)},
   { S( -28,  31), S(  23, 139), S(  22,   7), S(-132, -75), S( -58,  45)}
  },
  { // BISHOP
   { S(   0,   0), S(   0,   0), S( 108, 134), S(   0,   0), S(   0,   0)},
   { S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0)},
   { S(  12,  81), S(   0,   0), S(   0,   0), S(   0,   0), S( 226, 159)},
   { S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0)},
   { S(   0,   0), S(   0,   0), S( -13,  86), S(   0,   0), S(   0,   0)},
   { S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0)},
   { S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0)},
   { S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0)},
   { S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0)},
   { S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0)}
  },
  { // KING
   { S(   0,   0), S(   0,   0), S(   0,   0), S( -55,  36), S(  45,  90)},
   { S(   0,   0), S(   0,   0), S(   0,   0), S(-216,  -3), S( -60,  80)},
   { S(   0,   0), S(   0,   0), S(   0,   0), S(  32,-131), S( -24, -66)},
   { S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0)},
   { S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0)},
   { S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0)},
   { S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0)},
   { S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0)},
   { S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0)},
   { S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0)}
  }
};

} // namespace


namespace PSQT
{
Score psq[PIECE_NB][SQUARE_NB];

// PSQT::init() initializes piece-square tables: the white halves of the tables are
// copied from Bonus[] and PBonus[], adding the piece value, then the black halves of
// the tables are initialized by flipping and changing the sign of the white scores.
void init() {

  for (Piece pc : {W_ROOK, W_ADVISOR, W_CANNON, W_PAWN, W_KNIGHT, W_BISHOP, W_KING})
  {
    Score score = make_score(PieceValue[MG][pc], PieceValue[EG][pc]);

    for (Square s = SQ_A0; s <= SQ_I9; ++s)
    {
      File f = File(edge_distance(file_of(s)));
      if (f > FILE_E) --f;
      psq[pc  ][s] = score + Bonus[pc][rank_of(s)][f];
      psq[pc+8][flip_rank(s)] = -psq[pc][s];
    }
  }
}
/*
TUNE(SetRange(-35, 35), Bonus[ROOK], init);
TUNE(SetRange(-35, 35), Bonus[CANNON], init);
TUNE(SetRange(-35, 35), Bonus[KNIGHT], init);

// PAWN TUNE
TUNE(SetRange(-35, 35), Bonus[PAWN][3], init);
TUNE(SetRange(-35, 35), Bonus[PAWN][4], init);
TUNE(SetRange(-35, 35), Bonus[PAWN][5], init);
TUNE(SetRange(-35, 35), Bonus[PAWN][6], init);
TUNE(SetRange(-35, 35), Bonus[PAWN][7], init);
TUNE(SetRange(-35, 35), Bonus[PAWN][8], init);
TUNE(SetRange(-35, 35), Bonus[PAWN][9], init);

// BISHOP TUNE
TUNE(SetRange(-35, 35), Bonus[BISHOP][0][2], init);
TUNE(SetRange(-35, 35), Bonus[BISHOP][2][0], init);
TUNE(SetRange(-35, 35), Bonus[BISHOP][2][4], init);
TUNE(SetRange(-35, 35), Bonus[BISHOP][4][2], init);

// ADVISOR TUNE
TUNE(SetRange(-35, 35), Bonus[ADVISOR][0][3], init);
TUNE(SetRange(-35, 35), Bonus[ADVISOR][1][4], init);
TUNE(SetRange(-35, 35), Bonus[ADVISOR][2][3], init);

// KING TUNE
TUNE(SetRange(-35, 35), Bonus[KING][0][3], init);
TUNE(SetRange(-35, 35), Bonus[KING][0][4], init);
TUNE(SetRange(-35, 35), Bonus[KING][1][3], init);
TUNE(SetRange(-35, 35), Bonus[KING][1][4], init);
TUNE(SetRange(-35, 35), Bonus[KING][2][3], init);
TUNE(SetRange(-35, 35), Bonus[KING][2][4], init);
*/
} // namespace PSQT

} // namespace Stockfish