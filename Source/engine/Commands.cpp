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
        case '#': return "CHORD";
        case '?': return "CONDITION";
        case '@': return "PHRASE";
        case '!': return "DUB THROW";
        case '$': return "TABLE";
        case '>': return "HOP";
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
        case '@': return head + (p ? fmt ("PLAY THE INSTRUMENT'S PHRASE %02X", (int) p) : std::string ("PLAY THE SAMPLE, NOT A PHRASE"));
        case '$': return head + (p ? fmt ("PLAY WITH INSTRUMENT %02X'S TABLE", (int) p) : std::string ("NO TABLE FOR THIS NOTE"));
        case '>': return head + (x ? fmt ("TABLE: BACK TO ROW %X, %d MORE TIME%s", y, x, x == 1 ? "" : "S") : fmt ("TABLE: BACK TO ROW %X, FOR EVER", y));
        case '!':
            if (x == 0) return head + (y ? fmt ("DUB ECHO FEEDBACK UP TO %d/15 (!00 PUTS IT BACK)", y) : std::string ("DUB ECHO FEEDBACK BACK TO ITS OWN SETTING"));
            return head + fmt ("THROW THIS TRACK INTO THE DUB ECHO AT %d%%, ", x * 100 / 15) + (y ? fmt ("FOR %d LINE%s", y, y == 1 ? "" : "S") : std::string ("TO ITS NEXT NOTE"));
        case '#': return head + (p ? fmt ("CHORD SLOT %02X, HELD (PLUG-IN / MIDI-OUT INSTRUMENTS)", p) : std::string ("CHORD SLOT 00: JUST THE NOTE"));
        case 'G': return head + fmt ("GLIDE TO THE NOTE OVER %d TICKS", std::max (1, (int) p));
        case 'H': return head + fmt ("HIGH-PASS CUTOFF %d%% (%d HZ)", pct (p), (int) cutoffHz (p));
        case 'I':
            if (p == 0 || p == 0x80) return head + "NO TRANSPOSE (I8C = +12, I74 = -12)";
            return head + fmt ("TRANSPOSE THIS NOTE %+d SEMITONES", std::clamp ((int) p - 128, -48, 48));
        case 'K': return head + fmt ("PLAY THIS STEP %d%% OF THE TIME", pct (p));
        case '?':
        {
            if (x == 0)
                return head + (y == 1 ? "ONLY IN A FILL (HOLD F2)" : y == 2 ? "NOT IN A FILL" : y == 3 ? "ONLY THE FIRST TIME THE BLOCK PLAYS" : y == 4 ? "EVERY TIME BUT THE FIRST" : "ALWAYS");
            static const char* nth[] = { "", "1ST", "2ND", "3RD", "4TH", "5TH", "6TH", "7TH", "8TH", "9TH", "10TH", "11TH", "12TH", "13TH", "14TH", "15TH" };
            return head + fmt ("PLAY ON THE %s PASS OF EVERY %d", nth[x], std::max (1, y));
        }
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

const char* choppagePadName (int pad)
{
    static const char* n[8] = { "STUTTER", "ROLL", "REVERSE", "TAPE STOP", "HALF-TIME", "AKAIZE", "FILT DIVE", "S950 DROP" };
    return pad >= 0 && pad < 8 ? n[pad] : "";
}

std::string choppageCommandName (int c)
{
    if (c == cmdLetter ('U')) return "FX PAD";
    if (c == cmdLetter ('S')) return "SLICE";
    if (c == 0x9) return "PAD SETTING";
    return {};
}

std::string choppageOverview (int c)
{
    if (c == cmdLetter ('U')) return "HOLDS FX PAD X FOR Y LINES: X 1 STUTTER, 2 ROLL, 3 REVERSE, 4 TAPE STOP, 5 HALF-TIME, 6 AKAIZE, 7 FILT DIVE, "
                                     "8 S950 DROP; Y 1-F LINES, 0 UNTIL THE NEXT U. U00 LETS GO. E.G. U23 = ROLL FOR 3 LINES";
    if (c == cmdLetter ('S')) return "PLAYS SLICE XX+1 OF THE BREAK PLAYING (00-3F = SLICES 1-64), OVER THE TOP. E.G. S13 = SLICE 20";
    if (c == 0x9) return "THE PAD'S SETTING FOR THIS HIT, BESIDE ITS U (THE OTHER EFFECT COLUMN): STUTTER / ROLL 00-03 = 1/8 1/16 1/32 1/64, "
                         "TAPE STOP 00-FF = 0.1-3 S, HALF-TIME 00-04 = 1/2 3/4 2/3 1/4 1/2 TAPE, AKAIZE 00-03 = STRETCH 1.5 2 3 4, "
                         "FILT DIVE 00-FF = RESONANCE, S950 DROP 00-FF = 4-15.6 KHZ. E.G. U23 + 903 = ROLL AT 1/64 FOR 3 LINES";
    return {};
}

std::string describeChoppageCommand (int c, uint8_t p, int pad)
{
    const int x = p >> 4, y = p & 15;
    const std::string head = std::string (1, commandChar (c)) + fmt ("%02X: ", (int) p);
    if (c == cmdLetter ('U'))
    {
        if (x == 0) return head + "LET GO OF THE FX PAD";
        if (x > 8) return head + "NO SUCH PAD (X IS 1-8)";
        return head + choppagePadName (x - 1) + (y ? fmt (" FOR %d LINE%s", y, y == 1 ? "" : "S") : std::string (" UNTIL THE NEXT U"))
               + fmt ("  (U = FX PAD, %d = %s, %d = %s)", x, choppagePadName (x - 1), y, y ? (y == 1 ? "1 LINE" : fmt ("%d LINES", y).c_str()) : "UNTIL THE NEXT U");
    }
    if (c == cmdLetter ('S')) return head + fmt ("PLAY SLICE %d OF THE BREAK PLAYING", std::min ((int) p, 63) + 1);
    if (c == 0x9)
    {
        static const char* rate[] = { "1/8", "1/16", "1/32", "1/64" };
        static const char* half[] = { "1/2", "3/4", "2/3", "1/4", "1/2 TAPE" };
        static const char* str[] = { "1.5", "2", "3", "4" };
        switch (pad)
        {
            case 0: case 1: return head + choppagePadName (pad) + " AT " + rate[std::min ((int) p, 3)];
            case 2: return head + "REVERSE HAS NO SETTING";
            case 3: return head + fmt ("TAPE STOP OVER %.1f S", 0.1 + 2.9 * p / 255.0);
            case 4: return head + "HALF-TIME " + half[std::min ((int) p, 4)];
            case 5: return head + "AKAIZE STRETCH " + str[std::min ((int) p, 3)];
            case 6: return head + fmt ("FILT DIVE RESONANCE %d%%", pct (p));
            case 7: return head + fmt ("S950 DROP TO %.1f KHZ", 4.0 + 11.6 * p / 255.0);
            default: return head + "THE FX PAD'S SETTING (PUT IT BESIDE A U)";
        }
    }
    return {};
}

namespace
{
    CommandPart whole (const char* label, std::vector<std::pair<int, std::string>> ch, bool fine = true)
    { CommandPart p; p.label = label; p.choices = std::move (ch); p.fine = fine; return p; }
    CommandPart digit (const char* label, bool high, std::vector<std::pair<int, std::string>> ch = {})
    {
        CommandPart p; p.label = label; p.shift = high ? 4 : 0; p.mask = 15;
        if (ch.empty()) for (int i = 0; i < 16; ++i) ch.push_back ({ i, fmt ("%X", i) });
        p.choices = std::move (ch);
        return p;
    }
    std::vector<std::pair<int, std::string>> hexes (int from, int to, const char* zero = nullptr)
    {
        std::vector<std::pair<int, std::string>> v;
        for (int i = from; i <= to; ++i) v.push_back ({ i, i == 0 && zero ? std::string (zero) : fmt ("%X", i) });
        return v;
    }
    std::vector<std::pair<int, std::string>> levels()   // a 00-FF amount
    { return { { 0x00, "00 (0%)" }, { 0x40, "40 (25%)" }, { 0x80, "80 (50%)" }, { 0xC0, "C0 (75%)" }, { 0xFF, "FF (100%)" } }; }
}

std::vector<CommandPart> commandParts (const CommandInfo& c, bool chop, int pad)
{
    const int L = c.cmd;
    auto is = [L] (char ch) { return L == cmdLetter (ch); };
    if (chop && is ('U'))
    {
        std::vector<std::pair<int, std::string>> pads { { 0, "0 LET GO" } };
        for (int i = 0; i < 8; ++i) pads.push_back ({ i + 1, fmt ("%d ", i + 1) + choppagePadName (i) });
        std::vector<std::pair<int, std::string>> lines { { 0, "0 UNTIL NEXT U" }, { 1, "1 LINE" } };
        for (int i = 2; i < 16; ++i) lines.push_back ({ i, fmt ("%X", i) + (i == 15 ? " (15)" : i >= 10 ? fmt (" (%d)", i) : std::string()) });
        return { digit ("X - WHICH PAD", true, pads), digit ("Y - HOW MANY LINES", false, lines) };
    }
    if (chop && is ('S'))
        return { whole ("XX - WHICH SLICE (00 = SLICE 1)", { { 0x00, "00 SLICE 1" }, { 0x03, "03 SLICE 4" }, { 0x07, "07 SLICE 8" }, { 0x0F, "0F SLICE 16" }, { 0x1F, "1F SLICE 32" }, { 0x3F, "3F SLICE 64" } }) };
    if (chop && L == 0x9)
    {
        switch (pad)
        {
            case 0: case 1: return { whole ("XX - SIZE", { { 0, "00 1/8" }, { 1, "01 1/16" }, { 2, "02 1/32" }, { 3, "03 1/64" } }, false) };
            case 4:  return { whole ("XX - HALF-TIME MODE", { { 0, "00 1/2" }, { 1, "01 3/4" }, { 2, "02 2/3" }, { 3, "03 1/4" }, { 4, "04 1/2 TAPE" } }, false) };
            case 5:  return { whole ("XX - AKAIZE STRETCH", { { 0, "00 x1.5" }, { 1, "01 x2" }, { 2, "02 x3" }, { 3, "03 x4" } }, false) };
            case 3:  return { whole ("XX - HOW LONG THE STOP TAKES", { { 0x00, "00 0.1 S" }, { 0x40, "40 0.8 S" }, { 0x80, "80 1.6 S" }, { 0xFF, "FF 3 S" } }) };
            case 6:  return { whole ("XX - RESONANCE", levels()) };
            case 7:  return { whole ("XX - HOW LOW IT DROPS", { { 0x00, "00 4 KHZ" }, { 0x80, "80 9.8 KHZ" }, { 0xFF, "FF 15.6 KHZ" } }) };
            default: return { whole ("XX - THE SETTING (PUT A U IN THE OTHER COLUMN FIRST)", levels()) };
        }
    }
    if (c.extended)
    {
        switch (c.param >> 4)
        {
            case 0x0: return { digit ("Y - LED FILTER", false, { { 0, "0 ON" }, { 1, "1 OFF" } }) };
            case 0x4: case 0x7: return { digit ("Y - WAVE", false, { { 0, "0 SINE" }, { 1, "1 RAMP" }, { 2, "2 SQUARE" } }) };
            case 0x5: return { digit ("Y - TUNE (8-F FLAT, 1-7 SHARP)", false) };
            case 0x6: return { digit ("Y - REPEATS (0 = MARK THE START)", false, hexes (0, 15, "0 START")) };
            case 0x8: return { digit ("Y - PAN (0 LEFT, F RIGHT)", false) };
            case 0x9: case 0xC: case 0xD: return { digit ("Y - TICKS", false) };
            case 0xE: return { digit ("Y - LINES", false) };
            default:  return { digit ("Y - AMOUNT", false) };
        }
    }
    switch (L)
    {
        case 0x0: return { digit ("X - FIRST STEP UP (SEMITONES)", true), digit ("Y - SECOND STEP UP (SEMITONES)", false) };
        case 0x4: case 0x7: return { digit ("X - SPEED", true), digit ("Y - DEPTH", false) };
        case 0x5: case 0x6: case 0xA: return { digit ("X - VOLUME UP (USE X OR Y)", true), digit ("Y - VOLUME DOWN", false) };
        case 0x8: return { whole ("XX - PAN", { { 0x00, "00 LEFT" }, { 0x40, "40" }, { 0x80, "80 CENTRE" }, { 0xC0, "C0" }, { 0xFF, "FF RIGHT" } }) };
        case 0xC: return { whole ("XX - VOLUME", { { 0x00, "00 SILENT" }, { 0x10, "10 QUARTER" }, { 0x20, "20 HALF" }, { 0x30, "30" }, { 0x40, "40 FULL" } }) };
        case 0xF: return { whole ("XX - SPEED (01-1F) OR BPM (20-FF)", { { 0x03, "03 SPEED 3" }, { 0x06, "06 SPEED 6" }, { 0x78, "78 = 120 BPM" }, { 0x8C, "8C = 140 BPM" }, { 0xAA, "AA = 170 BPM" }, { 0xAF, "AF = 175 BPM" }, { 0x00, "00 STOP" } }) };
        default: break;
    }
    if (L == kCmdDub) return { digit ("X - HOW MUCH (0 = RIDE THE FEEDBACK)", true), digit ("Y - LINES (0 = TO THE NEXT NOTE)", false) };
    if (L == kCmdCond)
        return { digit ("X - WHICH PASS (0 = A SPECIAL ONE)", true),
                 digit ("Y - OF EVERY (WITH X = 0: 1 FILL, 2 NOT FILL, 3 FIRST, 4 NOT FIRST)", false) };
    if (is ('R')) return { digit ("X - EVERY HOW MANY TICKS", true), digit ("Y - 0 SAME, 1-7 QUIETER, 9-F LOUDER", false) };
    if (is ('X')) return { digit ("X - WHAT", true, { { 0, "0 NOTE" }, { 1, "1 VOLUME" }, { 2, "2 PAN" }, { 3, "3 START" } }), digit ("Y - HOW MUCH", false) };
    if (is ('U')) return { whole ("XX - DIRECTION", { { 0, "00 FORWARDS" }, { 1, "01 BACKWARDS" } }, false) };
    if (is ('K')) return { whole ("XX - HOW OFTEN", { { 0x20, "20 (13%)" }, { 0x40, "40 (25%)" }, { 0x80, "80 (50%)" }, { 0xC0, "C0 (75%)" }, { 0xFF, "FF ALWAYS" } }) };
    if (is ('I')) return { whole ("XX - TRANSPOSE (80 = NONE)", { { 0x74, "74 -12" }, { 0x7B, "7B -5" }, { 0x80, "80 NONE" }, { 0x87, "87 +7" }, { 0x8C, "8C +12" } }) };
    if (is ('T')) return { whole ("XX - CENTS (80 = IN TUNE)", { { 0x60, "60 -32" }, { 0x80, "80 IN TUNE" }, { 0xA0, "A0 +32" } }) };
    if (is ('M')) return { whole ("XX - TIMING (01-7F LATE, 81-FF EARLY)", { { 0x00, "00 ON TIME" }, { 0x20, "20 A BIT LATE" }, { 0x40, "40 LATE" }, { 0xA0, "A0 A BIT EARLY" }, { 0xC0, "C0 EARLY" } }) };
    if (is ('Z')) return { whole ("XX - BITS", { { 0x00, "00 OFF" }, { 0x04, "04 BITS" }, { 0x06, "06" }, { 0x08, "08" }, { 0x0C, "0C" } }) };
    if (is ('N') || is ('G') || is ('Y')) return { whole ("XX - TICKS", { { 0x01, "01" }, { 0x02, "02" }, { 0x03, "03" }, { 0x06, "06" }, { 0x0C, "0C" } }) };
    return { whole ("XX - VALUE", levels()) };
}

const std::vector<CommandInfo>& commandList()
{
    static const std::vector<CommandInfo> list {
        { kCmdChord, 0x01, false, "#", "CHORD",        "PLAYS CHORD XY FROM THE SONG'S CHORD TABLE, HELD - PLUG-IN / MIDI-OUT INSTRUMENTS; ON A SAMPLE, JUST THE NOTE" },
        { kCmdPhrase, 0x01, false, "@", "PHRASE",      "THIS NOTE PLAYS THE INSTRUMENT'S PHRASE XX (@01-@10); @00 PLAYS ITS SAMPLE, NOT A PHRASE" },
        { kCmdTable, 0x01, false, "$", "TABLE",       "THIS NOTE PLAYS WITH INSTRUMENT XX'S TABLE (ITS ARPS, RATCHETS, STEPS); $00: NO TABLE" },
        { kCmdDub, 0xC0, false, "!", "DUB THROW",     "THROWS THIS HIT INTO THE DUB ECHO (SEND B): X HOW MUCH (1-F), Y FOR HOW MANY LINES (0 = TO THE NEXT NOTE); !0Y RIDES THE ECHO'S FEEDBACK UP" },
        { kCmdCond, 0x12, false, "?", "CONDITION",    "PLAYS ON PASS X OF EVERY Y (?12 1ST OF 2, ?44 4TH OF 4); ?01 ONLY IN A FILL, ?02 NOT IN ONE, ?03 FIRST TIME ONLY, ?04 NOT THE FIRST" },
        { 0x0, 0x37, false, "0",  "ARPEGGIO",          "NOTE, +X, +Y SEMITONES IN TURN EACH TICK" },
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
        { cmdLetter ('J'), 0x80, false, "J", "AUTO B",       "SETS THE TRACK'S AUTO B KNOB (LEARN IT IN THE MIXER); ON/OFF KNOBS: 00 OFF, 01-FF ON" },
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
        { cmdLetter ('V'), 0x80, false, "V", "AUTO A",       "SETS THE TRACK'S AUTO A KNOB (LEARN IT IN THE MIXER); ON/OFF KNOBS: 00 OFF, 01-FF ON" },
        { cmdLetter ('W'), 0x80, false, "W", "BAND-PASS",    "BAND-PASS FILTER, CENTRE 00-FF" },
        { cmdLetter ('X'), 0x02, false, "X", "RANDOMISE",    "RANDOM X: 0 NOTE, 1 VOLUME, 2 PAN, 3 START; Y AMOUNT" },
        { cmdLetter ('Y'), 0x08, false, "Y", "KNOB GLIDE",   "THE NEXT V / J / KNOB CHANGE GLIDES OVER XX TICKS" },
        { cmdLetter ('Z'), 0x04, false, "Z", "BIT CRUSH",    "CRUSHES THIS NOTE TO XX BITS (01-0F)" },
    };
    return list;
}
} // namespace prv
