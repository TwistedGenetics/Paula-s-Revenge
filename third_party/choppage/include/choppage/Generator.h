#pragma once
/*
    Generator — the PATTERN and FX dice.

    PATTERN rebuilds the play order from the break's own slices. Each style is
    a couple of one-bar templates of roles (K S G H O, '.' rest, '-' keep); for
    every unlocked step, with probability AMOUNT the template's role is used
    and a slice with that badge is picked at random, otherwise the step keeps
    what the original break plays there. AMOUNT 0 = the original order.

    FX scatters stutter / reverse / stretch / tape-stop events over the FX lane;
    the more AMOUNT, the more (and the wilder) events.
*/
#include "choppage/Model.h"

namespace bc::Generator
{
    struct Input
    {
        const SlotContent* content = nullptr;
        Pattern original;      // the break as recorded
        Pattern current;       // locked steps survive from here
        Style   style = Style::jungle;
        float   amount = 0.35f;
        bool    layers = true; // extra ghost / hat fills on empty steps
        int64_t seed = 1;
    };

    Pattern pattern (const Input&);
    Pattern fx (const Pattern& current, float amount, int64_t seed);

    /** A slice of `badge` (or the closest stand-in); -1 if the break has nothing usable. */
    int pickSlice (const SlotContent&, Badge, Rng&);
    /** The next slice after `from` with the same badge (right-click swap in the SEQ tab). */
    int nextWithSameBadge (const SlotContent&, int from);
}
