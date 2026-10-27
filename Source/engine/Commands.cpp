// Paula's Revenge - effect command names and help text
#include "Commands.h"
#include "Song.h"
#include <cstdio>
#include <cmath>
#include <cstdarg>
#include <algorithm>

namespace prv
{
namespace
{
    std::string fmt (const char* f, ...)
    {
        char buf[160];
        va_list a; va_start (a, f); std::vsnprintf (buf, sizeof (buf), f, a); va_end (a);
        return buf;
    }
    int pct (uint8_t p) { return (int) std::lround (p / 255.0 * 100.0); }
    double cutoffHz (uint8_t p) { return 20.0 * std::pow (1000.0, p / 255.0); }
}

std::string commandName (int c)
{
    switch (c)
    {
        case 0x0: return "ARPEGGIO";       case 0x1: return "SLIDE UP";       case 0x2: return "SLIDE DOWN";
        case 0x3: return "SLIDE TO NOTE";  case 0x4: return "VIBRATO";        case 0x5: return "SLIDE TO NOTE+VOL SLIDE";
        case 0x6: return "VIBRATO+VOL SLIDE"; case 0x7: return "TREMOLO";     case 0x8: return "PAN";
        case 0x9: return "SAMPLE OFFSET";  case 0xA: return "VOLUME SLIDE";   case 0xB: return "JUMP TO POSITION";
        case 0xC: return "SET VOLUME";     case 0xD: return "NEXT BLOCK";     case 0xE: return "EXTENDED";
        case 0xF: return "SPEED/TEMPO";
        default: break;
    }
    switch (commandChar (c))
    {
        case 'G': return "GLIDE";          case 'H': return "HIGH-PASS";      case 'K': return "CHANCE";
        case 'I': return "NOTE TRANSPOSE";
        case 'J': return "AUTO B";         case 'V': return "AUTO A";         case 'Y': return "KNOB GLIDE";
        case 'L': return "LOW-PASS";       case 'M': return "MICRO-MOVE";     case 'N': return "NOTE LENGTH";
        case 'O': return "OVERDRIVE";      case 'P': return "POSITION";       case 'Q': return "RESONANCE";
        case 'R': return "ROLL";           case 'S': return "SLICE";          case 'T': return "MICRO-TUNE";
        case 'U': return "REVERSE";        case 'W': return "BAND-PASS";      case 'X': return "RANDOMISE";
        case 'Z': return "BIT CRUSH";
        default: return "UNUSED";
    }
}

std::string describeCommand (int c, uint8_t p)
{
    const int x = p >> 4, y = p & 15;
    const std::string head = fmt ("%c%02X: ", commandChar (c), p);
    switch (c)
    {
        case 0x0: return head + (p ? fmt ("ARPEGGIO +%d +%d SEMITONES", x, y) : "NOTHING");
        case 0x1: return head + fmt ("SLIDE PITCH UP BY %d PER TICK", p);
        case 0x2: return head + fmt ("SLIDE PITCH DOWN BY %d PER TICK", p);
        case 0x3: return head + fmt ("SLIDE TO THE NOTE, SPEED %d", p);
        case 0x4: return head + fmt ("VIBRATO SPEED %d DEPTH %d", x, y);
        case 0x5: return head + fmt ("SLIDE TO NOTE + VOLUME %s %d", x ? "UP" : "DOWN", x ? x : y);
        case 0x6: return head + fmt ("VIBRATO + VOLUME %s %d", x ? "UP" : "DOWN", x ? x : y);
        case 0x7: return head + fmt ("TREMOLO SPEED %d DEPTH %d", x, y);
        case 0x8: return head + (p == 0x80 ? std::string ("PAN CENTRE") : fmt ("PAN %s %d%%", p < 0x80 ? "LEFT" : "RIGHT", (int) std::lround (std::abs (p - 128) / 127.0 * 100)));
        case 0x9: return head + fmt ("START THE SAMPLE AT %d", p * 256);
        case 0xA: return head + fmt ("VOLUME %s %d PER TICK", x ? "UP" : "DOWN", x ? x : y);
        case 0xB: return head + fmt ("JUMP TO SONG POSITION %d", p);
        case 0xC: return head + fmt ("VOLUME %d OF 64", std::min ((int) p, 64));
        case 0xD: return head + fmt ("NEXT BLOCK, START AT LINE %d", x * 10 + y);
        case 0xE:
            switch (x)
            {
                case 0x0: return head + (y ? "LED FILTER OFF" : "LED FILTER ON");
                case 0x1: return head + fmt ("FINE SLIDE UP %d", y);
                case 0x2: return head + fmt ("FINE SLIDE DOWN %d", y);
                case 0x4: return head + fmt ("VIBRATO WAVEFORM %d", y);
                case 0x5: return head + fmt ("FINETUNE %d", y > 7 ? y - 16 : y);
                case 0x6: return head + (y ? fmt ("LOOP BACK %d TIMES", y) : "SET LOOP START");
                case 0x7: return head + fmt ("TREMOLO WAVEFORM %d", y);
                case 0x8: return head + fmt ("COARSE PAN %d", y);
                case 0x9: return head + fmt ("RETRIGGER EVERY %d TICKS", y);
                case 0xA: return head + fmt ("FINE VOLUME UP %d", y);
                case 0xB: return head + fmt ("FINE VOLUME DOWN %d", y);
                case 0xC: return head + fmt ("CUT THE NOTE AT TICK %d", y);
                case 0xD: return head + fmt ("DELAY THE NOTE BY %d TICKS", y);
                case 0xE: return head + fmt ("DELAY THE LINE BY %d LINES", y);
                default:  return head + "UNUSED";
            }
        case 0xF: return head + (p == 0 ? std::string ("STOP THE SONG") : p < 0x20 ? fmt ("SPEED %d TICKS PER LINE", p) : fmt ("TEMPO %d BPM", p));
        default: break;
    }
    switch (commandChar (c))
    {
        case 'G': return head + fmt ("GLIDE TO THE NOTE OVER %d TICKS", std::max (1, (int) p));
        case 'H': return head + fmt ("HIGH-PASS CUTOFF %d%% (%d HZ)", pct (p), (int) cutoffHz (p));
        case 'I':
            if (p == 0 || p == 0x80) return head + "NO TRANSPOSE (I8C = +12, I74 = -12)";
            return head + fmt ("TRANSPOSE THIS NOTE %+d SEMITONES", std::clamp ((int) p - 128, -48, 48));
        case 'K': return head + fmt ("PLAY THIS STEP %d%% OF THE TIME", pct (p));
        case 'V': return head + fmt ("SET THE TRACK'S AUTO A KNOB TO %d%%", pct (p));
        case 'J': return head + fmt ("SET THE TRACK'S AUTO B KNOB TO %d%%", pct (p));
        case 'Y': return head + (p ? fmt ("THE NEXT KNOB CHANGE GLIDES OVER %d TICKS", (int) p) : "KNOB CHANGES JUMP");
        case 'L': return head + fmt ("LOW-PASS CUTOFF %d%% (%d HZ)", pct (p), (int) cutoffHz (p));
        case 'M':
            if (p == 0 || p == 0x80) return head + "NO TIMING SHIFT";
            return head + (p < 0x80 ? fmt ("PLAY %d%% OF A LINE LATE", (int) std::lround (p / 128.0 * 100))
                                    : fmt ("PLAY %d%% OF A LINE EARLY", (int) std::lround ((p - 0x80) / 128.0 * 100)));
        case 'N': return head + (p ? fmt ("RELEASE THE NOTE AFTER %d TICKS", p) : "FULL LENGTH");
        case 'O': return head + fmt ("OVERDRIVE %d%%", pct (p));
        case 'P': return head + fmt ("START AT %d%% OF THE SAMPLE", (int) std::lround (p / 256.0 * 100));
        case 'Q': return head + fmt ("FILTER RESONANCE %d%%", pct (p));
        case 'R': return head + fmt ("ROLL EVERY %d TICKS", std::max (1, x)) + (y == 0 || y == 8 ? "" : y < 8 ? fmt (", %d QUIETER EACH", y * 4) : fmt (", %d LOUDER EACH", (y - 8) * 4));
        case 'S': return head + fmt ("PLAY SLICE %d (OF THE SAMPLE'S SLICES, ELSE OF 16)", (int) p + 1);
        case 'T': return head + fmt ("MICRO-TUNE %+d CENTS", (int) p - 128);
        case 'U': return head + ((p & 1) ? "PLAY BACKWARDS" : "PLAY FORWARDS");
        case 'W': return head + fmt ("BAND-PASS CUTOFF %d%% (%d HZ)", pct (p), (int) cutoffHz (p));
        case 'X':
        {
            static const char* what[] = { "NOTE +/-%d SEMITONES", "VOLUME +/-%d", "PAN +/-%d", "START POSITION UP TO %d/16" };
            const int amt = x == 1 ? y * 4 : x == 2 ? y * 16 : y;
            return head + "RANDOM " + fmt (what[std::min (x, 3)], amt);
        }
        case 'Z': return head + (p == 0 || p >= 16 ? std::string ("NO BIT CRUSH") : fmt ("CRUSH TO %d BITS", (int) p));
        default:  return head + "UNUSED";
    }
}

const std::vector<CommandInfo>& commandList()
{
    static const std::vector<CommandInfo> list {
        { 0x0, 0x37, false, "0",  "ARPEGGIO",          "CHORD: NOTE, +X, +Y SEMITONES IN TURN EACH TICK" },
        { 0x1, 0x08, false, "1",  "SLIDE UP",          "BENDS THE PITCH UP, XX PER TICK" },
        { 0x2, 0x08, false, "2",  "SLIDE DOWN",        "BENDS THE PITCH DOWN, XX PER TICK" },
        { 0x3, 0x10, false, "3",  "SLIDE TO NOTE",     "GLIDES FROM THE LAST NOTE TO THIS ONE AT SPEED XX" },
        { 0x4, 0x44, false, "4",  "VIBRATO",           "WOBBLES THE PITCH: SPEED X, DEPTH Y" },
        { 0x5, 0x04, false, "5",  "SLIDE + VOL SLIDE", "KEEPS SLIDING TO THE NOTE, VOLUME X UP OR Y DOWN" },
        { 0x6, 0x04, false, "6",  "VIBRATO + VOL",     "KEEPS THE VIBRATO, VOLUME X UP OR Y DOWN" },
        { 0x7, 0x44, false, "7",  "TREMOLO",           "WOBBLES THE VOLUME: SPEED X, DEPTH Y" },
        { 0x8, 0x80, false, "8",  "PAN",               "00 HARD LEFT, 80 CENTRE, FF HARD RIGHT" },
        { 0x9, 0x10, false, "9",  "SAMPLE OFFSET",     "STARTS THE SAMPLE XX x 256 SAMPLES IN" },
        { 0xA, 0x04, false, "A",  "VOLUME SLIDE",      "FADES: X UP OR Y DOWN EACH TICK" },
        { 0xB, 0x00, false, "B",  "JUMP TO POSITION",  "CARRIES ON FROM SONG POSITION XX" },
        { 0xC, 0x40, false, "C",  "SET VOLUME",        "THE NOTE'S VOLUME, 00-40 (40 = FULL)" },
        { 0xD, 0x00, false, "D",  "NEXT BLOCK",        "ENDS THIS BLOCK; THE NEXT STARTS AT LINE XX" },
        { 0xE, 0x00, true,  "E0", "LED FILTER",        "E00 LED FILTER ON, E01 OFF" },
        { 0xE, 0x11, true,  "E1", "FINE SLIDE UP",     "NUDGES THE PITCH UP ONCE BY Y" },
        { 0xE, 0x21, true,  "E2", "FINE SLIDE DOWN",   "NUDGES THE PITCH DOWN ONCE BY Y" },
        { 0xE, 0x40, true,  "E4", "VIBRATO WAVE",      "0 SINE, 1 RAMP, 2 SQUARE" },
        { 0xE, 0x50, true,  "E5", "FINETUNE",          "TUNES THIS NOTE, 8-F FLAT, 1-7 SHARP" },
        { 0xE, 0x60, true,  "E6", "PATTERN LOOP",      "E60 MARKS THE START, E6Y REPEATS Y TIMES" },
        { 0xE, 0x70, true,  "E7", "TREMOLO WAVE",      "0 SINE, 1 RAMP, 2 SQUARE" },
        { 0xE, 0x88, true,  "E8", "COARSE PAN",        "PAN IN 16 STEPS, 0 LEFT - F RIGHT" },
        { 0xE, 0x93, true,  "E9", "RETRIGGER",         "PLAYS THE NOTE AGAIN EVERY Y TICKS" },
        { 0xE, 0xA1, true,  "EA", "FINE VOLUME UP",    "RAISES THE VOLUME ONCE BY Y" },
        { 0xE, 0xB1, true,  "EB", "FINE VOLUME DOWN",  "LOWERS THE VOLUME ONCE BY Y" },
        { 0xE, 0xC3, true,  "EC", "NOTE CUT",          "SILENCES THE NOTE AT TICK Y" },
        { 0xE, 0xD3, true,  "ED", "NOTE DELAY",        "STARTS THE NOTE Y TICKS LATE" },
        { 0xE, 0xE1, true,  "EE", "LINE DELAY",        "HOLDS THE WHOLE LINE FOR Y MORE LINES" },
        { 0xF, 0x06, false, "F",  "SPEED / TEMPO",     "01-1F TICKS PER LINE, 20-FF BPM, 00 STOPS" },
        { cmdLetter ('G'), 0x06, false, "G", "GLIDE",        "GLIDES TO THE NOTE OVER XX TICKS" },
        { cmdLetter ('H'), 0x40, false, "H", "HIGH-PASS",    "HIGH-PASS FILTER, CUTOFF 00-FF" },
        { cmdLetter ('I'), 0x8C, false, "I", "TRANSPOSE",    "THIS NOTE ONLY: 80 NONE, 8C +12, 74 -12" },
        { cmdLetter ('J'), 0x80, false, "J", "AUTO B",       "SETS THE TRACK'S AUTO B KNOB (LEARN IT IN THE MIXER)" },
        { cmdLetter ('K'), 0x80, false, "K", "CHANCE",       "PLAYS THIS STEP XX/FF OF THE TIME" },
        { cmdLetter ('L'), 0x80, false, "L", "LOW-PASS",     "LOW-PASS FILTER, CUTOFF 00-FF" },
        { cmdLetter ('M'), 0x20, false, "M", "MICRO-MOVE",   "PLAYS LATE (01-7F) OR EARLY (81-FF)" },
        { cmdLetter ('N'), 0x03, false, "N", "NOTE LENGTH",  "RELEASES THE NOTE AFTER XX TICKS" },
        { cmdLetter ('O'), 0x80, false, "O", "OVERDRIVE",    "DISTORTS THIS NOTE, 00-FF" },
        { cmdLetter ('P'), 0x40, false, "P", "POSITION",     "STARTS PART-WAY IN: 40 = A QUARTER, 80 = HALF" },
        { cmdLetter ('Q'), 0x80, false, "Q", "RESONANCE",    "FILTER RESONANCE FOR H, L AND W" },
        { cmdLetter ('R'), 0x30, false, "R", "ROLL",         "REPEATS EVERY X TICKS, Y FADES (1-7 DOWN, 9-F UP)" },
        { cmdLetter ('S'), 0x00, false, "S", "SLICE",        "PLAYS SLICE XX+1 OF THE SAMPLE" },
        { cmdLetter ('T'), 0x90, false, "T", "MICRO-TUNE",   "DETUNES IN CENTS, 80 = IN TUNE" },
        { cmdLetter ('U'), 0x01, false, "U", "REVERSE",      "U01 PLAYS BACKWARDS, U00 FORWARDS" },
        { cmdLetter ('V'), 0x80, false, "V", "AUTO A",       "SETS THE TRACK'S AUTO A KNOB (LEARN IT IN THE MIXER)" },
        { cmdLetter ('W'), 0x80, false, "W", "BAND-PASS",    "BAND-PASS FILTER, CENTRE 00-FF" },
        { cmdLetter ('X'), 0x02, false, "X", "RANDOMISE",    "RANDOM X: 0 NOTE, 1 VOLUME, 2 PAN, 3 START; Y AMOUNT" },
        { cmdLetter ('Y'), 0x08, false, "Y", "KNOB GLIDE",   "THE NEXT V / J / KNOB CHANGE GLIDES OVER XX TICKS" },
        { cmdLetter ('Z'), 0x04, false, "Z", "BIT CRUSH",    "CRUSHES THIS NOTE TO XX BITS (01-0F)" },
    };
    return list;
}
} // namespace prv
