#ifndef __VIGILANCE_IR_GHOST_H__
#define __VIGILANCE_IR_GHOST_H__

// Vigilance - IR Ghost-Hunter (#13). An infrared activity monitor: a bar and the
// LED rise when IR is picked up nearby (remotes, leaky IR sensors, some cameras
// with modulated IR). Honest limit: the receiver is a 38 kHz demodulator, so it
// only really "sees" modulated IR. A steady, unmodulated IR LED can slip past it
// (check by eye or with a phone camera too).
void ir_ghost_setup();

#endif // __VIGILANCE_IR_GHOST_H__
