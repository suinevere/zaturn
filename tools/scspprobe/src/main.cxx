/*----------------------
 | main.cxx
 | Description: A disc that boots straight into one tune and nothing else, so a
 |   change to the music can be heard in a thirty-second build instead of the
 |   two-and-a-half-minute one the real targets take. Built with -DNETBIN so
 |   synth_target_init raises the master volume itself, because no sound driver
 |   runs here -- the same reason the netbin has to.
 |
 |   Which tune comes from probe_song.h, which drums-chip.bat writes from the
 |   id on its command line. Generated rather than passed as a -D so the probe's
 |   makefile stays the stock one.
 | Author: suinevere
 | Dependencies: srl.hpp, synth.h, synth_target.h, probe_song.h
 ----------------------*/
#include <srl.hpp>
extern "C" {
#include "synth.h"
#include "synth_target.h"
}
#include "probe_song.h"
using namespace SRL::Types;

int main() {
    SRL::Core::Initialize(HighColor::Colors::Black);
    synth_target_init();
    synth_set_level(7);
    synth_start_song(PROBE_SONG);
    while (1) { SRL::Core::Synchronize(); }
    return 0;
}
