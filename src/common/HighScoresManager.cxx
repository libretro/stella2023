//============================================================================
//
//   SSSS    tt          lll  lll
//  SS  SS   tt           ll   ll
//  SS     tttttt  eeee   ll   ll   aaaa
//   SSSS    tt   ee  ee  ll   ll      aa
//      SS   tt   eeeeee  ll   ll   aaaaa  --  "An Atari 2600 VCS Emulator"
//  SS  SS   tt   ee      ll   ll  aa  aa
//   SSSS     ttt  eeeee llll llll  aaaaa
//
// Copyright (c) 1995-2020 by Bradford W. Mott, Stephen Anthony
// and the Stella Team
//
// See the file "License.txt" for information on usage and redistribution of
// this file, and for a DISCLAIMER OF ALL WARRANTIES.
//============================================================================

/*
 Formats (all optional):
   4,             ; score digits per player
   0,             ; trailing zeroes
   B,             ; score format (BCD, HEX)
   B,             ; variation format (BCD, HEX)
   0,             ; zero-based variation
   "",            ; special label (5 chars)
   B,             ; special format (BCD, HEX)
   0,             ; zero-based special
 Addresses (in hex):
   n*p-times xx,  ; score info for each player, high to low
   xx,            ; variation address (if more than 1 variation)
   xx             ; player address (if more than 1 player)
   xx             ; special address (if defined)

 TODO:
 - variation bits (Centipede)
 - player bits (Asteroids, Space Invaders)
 - score swaps (Asteroids)
 - special: one optional and named value extra per game (round, level...)
*/

#include <cmath>

#include "OSystem.hxx"
#include "PropsSet.hxx"
#include "Console.hxx"
#include "Launcher.hxx"
#include "System.hxx"

#include "HighScoresManager.hxx"

using namespace BSPF;
using namespace std;
using namespace HSM;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
HighScoresManager::HighScoresManager(OSystem& osystem)
  : myOSystem(osystem)
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Int16 HighScoresManager::peek(uInt16 addr) const
{
  if (myOSystem.hasConsole())
  {
    System& system = myOSystem.console().system();
    return system.peek(addr);
  }
  return -1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Properties& HighScoresManager::properties(Properties& props) const
{

  if (myOSystem.hasConsole())
  {
    props = myOSystem.console().properties();
  }
  else
  {
    const string& md5 = myOSystem.launcher().selectedRomMD5();
    myOSystem.propSet().getMD5(md5, props);
  }
  return props;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
string HighScoresManager::getPropIdx(const Properties& props, PropType type, uInt32 idx) const
{
  string property = props.get(type);

  replace(property.begin(), property.end(), ',', ' ');
  replace(property.begin(), property.end(), '|', ' ');
  istringstream buf(property);
  string result;

  for (uInt32 i = 0; i <= idx; ++i)
    if(!(buf >> result))
      return "";

  return result;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 HighScoresManager::numPlayers(const Properties& props) const
{
  string numPlayers = getPropIdx(props, PropType::Cart_Players);

  return min(uInt32(stringToInt(numPlayers, DEFAULT_PLAYER)), MAX_PLAYERS);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 HighScoresManager::numVariations(const Properties& props) const
{
  string numVariations = getPropIdx(props, PropType::Cart_Variations);

  return min(uInt32(stringToInt(numVariations, DEFAULT_VARIATION)), MAX_VARIATIONS);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool HighScoresManager::get(const Properties& props, uInt32& numPlayersR, uInt32& numVariationsR,
                            ScoresInfo& info) const
{
  numPlayersR = numPlayers(props);
  numVariationsR = numVariations(props);

  info.numDigits = numDigits(props);
  info.trailingZeroes = trailingZeroes(props);
  info.scoreBCD = scoreBCD(props);
  info.varsBCD = varBCD(props);
  info.varsZeroBased = varZeroBased(props);
  info.specialBCD = false; // TODO
  info.specialZeroBased = true; // TODO

  info.playersAddr = playerAddress(props);
  info.varsAddr = varAddress(props);
  info.specialAddr = 0; // TODO
  for (uInt32 p = 0; p < MAX_PLAYERS; ++p)
  {
    if (p < numPlayersR)
    {
      for (uInt32 a = 0; a < numAddrBytes(props); ++a)
      {
        uInt32 idx = p * numAddrBytes(props) + a;
        string addr = getPropIdx(props, PropType::Cart_Addresses, idx);

        info.scoresAddr[p][a] = stringToIntBase16(addr);
      }
    }
    else
      for (uInt32 a = 0; a < numAddrBytes(props); ++a)
        info.scoresAddr[p][a] = -1;
  }

  return (!getPropIdx(props, PropType::Cart_Addresses, 0).empty());
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void HighScoresManager::set(Properties& props, uInt32 numPlayers, uInt32 numVariations,
                            const ScoresInfo& info) const
{
  ostringstream buf;
  string output;

  props.set(PropType::Cart_Players, to_string(numPlayers));
  props.set(PropType::Cart_Variations, to_string(min(numVariations, MAX_VARIATIONS)));

  // fill from the back to skip default values
  if (info.varsZeroBased != DEFAULT_VARS_ZERO_BASED)
    output = info.varsZeroBased ? ",1" : ",0";
  if (output.length() || info.varsBCD != DEFAULT_VARS_BCD)
    output.insert(0, info.varsBCD ? ",B" : ",D");
  if (output.length() || info.scoreBCD != DEFAULT_SCORE_BCD)
    output.insert(0, info.scoreBCD ? ",B" : ",H");
  if (output.length() || info.trailingZeroes != DEFAULT_TRAILING)
    output.insert(0, "," + to_string(info.trailingZeroes));
  if (output.length() || info.numDigits != DEFAULT_DIGITS)
    output.insert(0, to_string(info.numDigits));

  props.set(PropType::Cart_Formats, output);

  for (uInt32 p = 0; p < numPlayers; ++p)
  {
    for (uInt32 a = 0; a < numAddrBytes(info.numDigits, info.trailingZeroes); ++a)
      buf << hex << info.scoresAddr[p][a] << ",";
  }

  // add optional addresses
  if (numVariations != DEFAULT_VARIATION || numPlayers != DEFAULT_PLAYER)
    buf << info.varsAddr << "," ;
  if (numPlayers != DEFAULT_PLAYER)
    buf << info.playersAddr << "," ;

  output = buf.str();
  output.pop_back();
  props.set(PropType::Cart_Addresses, output);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 HighScoresManager::numDigits(const Properties& props) const
{
  string digits = getPropIdx(props, PropType::Cart_Formats, 0);

  return min(uInt32(stringToInt(digits, DEFAULT_DIGITS)), MAX_DIGITS);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 HighScoresManager::trailingZeroes(const Properties& props) const
{
  string trailing = getPropIdx(props, PropType::Cart_Formats, 1);

  return min(uInt32(stringToInt(trailing, DEFAULT_TRAILING)), MAX_TRAILING);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool HighScoresManager::scoreBCD(const Properties& props) const
{
  string bcd = getPropIdx(props, PropType::Cart_Formats, 2);

  return bcd == EmptyString ? DEFAULT_SCORE_BCD : bcd == "B";
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool HighScoresManager::varBCD(const Properties& props) const
{
  string bcd = getPropIdx(props, PropType::Cart_Formats, 3);

  return bcd == EmptyString ? DEFAULT_VARS_BCD : bcd == "B";
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool HighScoresManager::varZeroBased(const Properties& props) const
{
  string zeroBased = getPropIdx(props, PropType::Cart_Formats, 4);

  return zeroBased == EmptyString ? DEFAULT_VARS_ZERO_BASED : zeroBased != "0";
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool HighScoresManager::playerZeroBased(const Properties& props) const
{
  return DEFAULT_PLAYERS_ZERO_BASED;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt16 HighScoresManager::playerAddress(const Properties& props) const
{
  uInt32 idx = numAddrBytes(props) * numPlayers(props) + 1;
  string addr = getPropIdx(props, PropType::Cart_Addresses, idx);

  return stringToIntBase16(addr, DEFAULT_ADDRESS);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt16 HighScoresManager::varAddress(const Properties& props) const
{
  uInt32 idx = numAddrBytes(props) * numPlayers(props);
  string addr = getPropIdx(props, PropType::Cart_Addresses, idx);

  return stringToIntBase16(addr, DEFAULT_ADDRESS);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 HighScoresManager::numAddrBytes(Int32 digits, Int32 trailing) const
{
  return (digits - trailing + 1) / 2;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 HighScoresManager::numAddrBytes(const Properties& props) const
{
  return numAddrBytes(numDigits(props), trailingZeroes(props));
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Int32 HighScoresManager::player(uInt16 addr, uInt32 numPlayers, bool zeroBased) const
{
  if (!myOSystem.hasConsole())
    return -1;

  Int32 player = peek(addr);
  Int32 bits = ceil(log(numPlayers + (!zeroBased ? 1 : 0))/log(2));

  // limit to game's number of players
  player %= 1 << bits;
  player += zeroBased ? 1 : 0;

  return player;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Int32 HighScoresManager::numVariations() const
{
  Properties props;
  uInt16 vars = numVariations(properties(props));

  return vars;;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Int32 HighScoresManager::player() const
{
  Properties props;
  uInt16 addr = playerAddress(properties(props));

  if (addr == DEFAULT_ADDRESS)
    return DEFAULT_PLAYER;

  return player(addr, numPlayers(props), playerZeroBased(props));
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Int32 HighScoresManager::variation(uInt16 addr, bool varBCD, bool zeroBased,
                                   uInt32 numVariations) const
{
  if (!myOSystem.hasConsole())
    return -1;

  Int32 var = peek(addr);
  Int32 bits = ceil(log(numVariations + (!zeroBased ? 1 : 0))/log(2));

  if (varBCD)
    var = fromBCD(var);

  // limit to game's number of variations
  var %= 1 << bits;
  var += zeroBased ? 1 : 0;

  return var;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Int32 HighScoresManager::variation() const
{
  Properties props;
  uInt16 addr = varAddress(properties(props));

  if (addr == DEFAULT_ADDRESS)
    return DEFAULT_VARIATION;

  return variation(addr, varBCD(props), varZeroBased(props), numVariations(props));
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Int32 HighScoresManager::score(uInt32 player, uInt32 numAddrBytes, uInt32 trailingZeroes,
                               bool isBCD, const ScoreAddresses& scoreAddr) const
{
  if (!myOSystem.hasConsole())
    return -1;

  Int32 totalScore = 0;

  for (uInt32 b = 0; b < numAddrBytes; ++b)
  {
    uInt16 addr = scoreAddr[b];
    uInt32 score;

    totalScore *= isBCD ? 100 : 256;
    score = peek(addr);
    if (isBCD)
    {
      score = fromBCD(score);
      // verify if score is legit
      if (score == -1)
        return -1;
    }
    totalScore += score;
  }

  if (totalScore != -1)
    for (uInt32 i = 0; i < trailingZeroes; ++i)
      totalScore *= 10;

  return totalScore;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Int32 HighScoresManager::score() const
{
  Properties props;
  uInt32 numBytes = numAddrBytes(properties(props));
  uInt32 currentPlayer = player() - (playerZeroBased(props) ? 1 : 0);
  uInt32 idx = numBytes * currentPlayer;
  ScoreAddresses scoreAddr;

  for (uInt32 b = 0; b < numBytes; ++b)
  {
    string addr = getPropIdx(props, PropType::Cart_Addresses, idx + b);

    if (addr == EmptyString)
      return -1;
    scoreAddr[b] = stringToIntBase16(addr);
  }

  return score(currentPlayer, numBytes, trailingZeroes(props), scoreBCD(props), scoreAddr);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Int32 HighScoresManager::fromBCD(uInt8 bcd) const
{
  // verify if score is legit
  if (bcd >= 160)
    return -1;

  return (bcd >> 4) * 10 + bcd % 16;
}