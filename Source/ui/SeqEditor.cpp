// Paula's Revenge - editing a highlighted range of song positions
#include "SeqEditor.h"
#include "../engine/SongOps.h"

namespace prv::ui
{
int SeqEditor::anchor() const
{
    const auto& st = proc.editorState;
    // anything else that moves the position (playback, loading, the BLOCK box ...) drops the range
    const int n = (int) proc.song.sequence.size();
    return st.seqAnchor >= 0 && st.seqSelCursor == st.seqPos && st.seqAnchor < n ? st.seqAnchor : st.seqPos;
}
int SeqEditor::selStart() const { return std::min (anchor(), proc.editorState.seqPos); }
int SeqEditor::selEnd() const   { return std::max (anchor(), proc.editorState.seqPos); }

void SeqEditor::clamp()
{
    auto& st = proc.editorState;
    const int n = (int) proc.song.sequence.size();
    const bool ranged = st.seqAnchor >= 0 && st.seqSelCursor == st.seqPos;
    st.seqPos = juce::jlimit (0, n - 1, st.seqPos);
    st.seqAnchor = juce::jlimit (0, n - 1, st.seqAnchor);
    if (ranged) st.seqSelCursor = st.seqPos;
    else st.seqAnchor = st.seqSelCursor = -1;
}

void SeqEditor::select (int anchor, int cursor)
{
    auto& st = proc.editorState;
    st.seqAnchor = anchor;
    st.seqPos = cursor;
    st.seqSelCursor = cursor;
    clamp();
    st.block = proc.song.safeBlock (proc.song.sequence[(size_t) st.seqPos]);
}

void SeqEditor::edit (std::function<void (std::vector<int>&)> fn, const juce::String& status,
                      std::function<void (std::vector<SongSection>&, int)> secFn)
{
    proc.history.structure (status.isNotEmpty() ? status : juce::String ("ARRANGE"));
    std::vector<int> seq = proc.song.sequence;
    fn (seq);
    if (seq.empty()) seq.push_back (0);
    if (seq.size() > (size_t) kMaxSequence) seq.resize ((size_t) kMaxSequence);
    auto secs = proc.song.sections;
    if (secFn) secFn (secs, (int) seq.size());
    tidySections (secs, (int) seq.size());
    proc.withLock ([&] { std::swap (proc.song.sequence, seq); std::swap (proc.song.sections, secs); });
    clamp();
}

void SeqEditor::copy()
{
    auto& st = proc.editorState;
    st.seqClip.assign (proc.song.sequence.begin() + selStart(), proc.song.sequence.begin() + selEnd() + 1);
    done ("COPIED " + positions ((int) st.seqClip.size()));
}

void SeqEditor::cut()
{
    const int a = selStart(), b = selEnd();
    if (b - a + 1 >= (int) proc.song.sequence.size()) { done ("A SONG NEEDS AT LEAST ONE POSITION"); return; }
    proc.editorState.seqClip.assign (proc.song.sequence.begin() + a, proc.song.sequence.begin() + b + 1);
    edit ([&] (std::vector<int>& s) { s.erase (s.begin() + a, s.begin() + b + 1); }, "CUT " + positions (b - a + 1),
          [a, b] (std::vector<SongSection>& x, int n) { sectionsRemoved (x, a, b - a + 1, n); });
    select (a, a);
    done ("CUT " + positions (b - a + 1) + " - PASTE PUTS THEM BACK AFTER THE HIGHLIGHT");
}

void SeqEditor::paste()
{
    const auto clip = proc.editorState.seqClip;
    if (clip.empty()) { done ("NOTHING TO PASTE"); return; }
    const int at = std::min (selEnd() + 1, (int) proc.song.sequence.size());
    const int count = (int) clip.size();
    if ((int) proc.song.sequence.size() + count > kMaxSequence) { done ("THE SONG IS FULL (" + juce::String (kMaxSequence) + " POSITIONS)"); return; }
    edit ([&] (std::vector<int>& s) { s.insert (s.begin() + at, clip.begin(), clip.end()); }, "PASTED " + positions (count),
          [at, count] (std::vector<SongSection>& x, int n) { sectionsInserted (x, at, count, n); });
    select (at, at + count - 1);
    done ("PASTED " + positions (count));
}

void SeqEditor::duplicate()
{
    const int a = selStart(), b = selEnd(), len = b - a + 1;
    if ((int) proc.song.sequence.size() + len > kMaxSequence) { done ("THE SONG IS FULL (" + juce::String (kMaxSequence) + " POSITIONS)"); return; }
    edit ([&] (std::vector<int>& s)
    {
        std::vector<int> part (s.begin() + a, s.begin() + b + 1);
        s.insert (s.begin() + b + 1, part.begin(), part.end());
    }, "DUPLICATED " + positions (len),
    [b, len] (std::vector<SongSection>& x, int n) { sectionsInserted (x, b + 1, len, n); });
    select (b + 1, b + len);
    done ("DUPLICATED " + positions (len));
}

void SeqEditor::remove()
{
    const int a = selStart(), b = selEnd();
    if (b - a + 1 >= (int) proc.song.sequence.size()) { done ("A SONG NEEDS AT LEAST ONE POSITION"); return; }
    edit ([&] (std::vector<int>& s) { s.erase (s.begin() + a, s.begin() + b + 1); }, "DELETED " + positions (b - a + 1),
          [a, b] (std::vector<SongSection>& x, int n) { sectionsRemoved (x, a, b - a + 1, n); });
    const int p = std::min (a, (int) proc.song.sequence.size() - 1);
    select (p, p);
    done ("DELETED " + positions (b - a + 1));
}

void SeqEditor::moveBy (int delta)
{
    const int a = selStart(), b = selEnd(), n = (int) proc.song.sequence.size();
    if (a + delta < 0 || b + delta >= n) return;
    const bool anchorFirst = anchor() <= proc.editorState.seqPos;
    edit ([&] (std::vector<int>& s)
    {
        std::vector<int> part (s.begin() + a, s.begin() + b + 1);
        s.erase (s.begin() + a, s.begin() + b + 1);
        s.insert (s.begin() + a + delta, part.begin(), part.end());
    }, "MOVED " + positions (b - a + 1));
    if (anchorFirst) select (a + delta, b + delta); else select (b + delta, a + delta);
    done ({});
}

void SeqEditor::moveTo (int insertBefore)
{
    const int a = selStart(), b = selEnd(), len = b - a + 1;
    if (insertBefore >= a && insertBefore <= b + 1) return;   // dropped on itself
    int target = insertBefore > b ? insertBefore - len : insertBefore;
    edit ([&] (std::vector<int>& s)
    {
        std::vector<int> part (s.begin() + a, s.begin() + b + 1);
        s.erase (s.begin() + a, s.begin() + b + 1);
        target = juce::jlimit (0, (int) s.size(), target);
        s.insert (s.begin() + target, part.begin(), part.end());
    }, "MOVED " + positions (len));
    select (target, target + len - 1);
    done ("MOVED " + positions (len));
}

void SeqEditor::insertBlock (int blk)
{
    blk = proc.song.safeBlock (blk);
    const int at = proc.editorState.seqPos + 1;
    edit ([&] (std::vector<int>& s) { s.insert (s.begin() + std::min (at, (int) s.size()), blk); }, "INSERTED BLOCK " + juce::String (blk).paddedLeft ('0', 3),
          [at] (std::vector<SongSection>& x, int n) { sectionsInserted (x, at, 1, n); });
    select (at, at);
    done ("INSERTED BLOCK " + juce::String (blk).paddedLeft ('0', 3));
}

void SeqEditor::setBlock (int blk)
{
    blk = proc.song.safeBlock (blk);
    const int a = selStart(), b = selEnd();
    edit ([&] (std::vector<int>& s) { for (int i = a; i <= b; ++i) s[(size_t) i] = blk; }, "NOW PLAYS BLOCK " + juce::String (blk).paddedLeft ('0', 3));
    proc.editorState.block = blk;
    done ("NOW PLAYS BLOCK " + juce::String (blk).paddedLeft ('0', 3));
}
} // namespace prv::ui
