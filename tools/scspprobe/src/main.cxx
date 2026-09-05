/*----------------------
 | main.cxx
 | Description: A disc that boots straight into the tune and nothing else, so a
 |   change to the music can be heard in a thirty-second build instead of the
 |   two-and-a-half-minute one the real targets take. Built with -DNETBIN so
 |   synth_target_init raises the master volume itself, because no sound driver
 |   runs here -- the same reason the netbin has to.
 | Author: suinevere
 | Dependencies: srl.hpp, synth.h, synth_target.h
 ----------------------*/
#include <srl.hpp>
extern "C" {
#include "synth.h"
#include "synth_target.h"
}
using namespace SRL::Types;

int main() {
    SRL::Core::Initialize(HighColor::Colors::Black);
    synth_target_init();
    synth_set_level(7);
    synth_start();
    while (1) { SRL::Core::Synchronize(); }
    return 0;
}
