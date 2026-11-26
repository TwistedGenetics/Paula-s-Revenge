/* Paula's Revenge's copy of HELIX: the TAPE, BBD and reverb effects that HELIX's FX rack ports from another project
 * aren't part of this build - in Paula's Revenge those slots pass the sound through untouched (their send is silent). */
#include "dn_fx.h"

uint32_t dn_fxc_ram(uint32_t type) { (void) type; return 64u; }
void dn_fxc_init(dn_fx_s_t *s) { (void) s; }
void dn_fxc_run(dn_fx_s_t *s, const dn_fx_p_t *p, const int32_t *mono, int32_t *wl, int32_t *wr, uint32_t n, uint32_t bpm)
{
    uint32_t i;
    (void) s; (void) p; (void) mono; (void) bpm;
    for (i = 0; i < n; i++) { wl[i] = 0; wr[i] = 0; }
}
