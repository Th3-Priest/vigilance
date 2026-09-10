#ifndef __VIGILANCE_RF_TESTGEN_H__
#define __VIGILANCE_RF_TESTGEN_H__

// Vigilance - RF test signal generator (#42). Emits a known OOK pattern at a
// chosen frequency, or sweeps the common ISM bands, to AUDIT your own receivers
// (check they react, identify their frequency). Audit tool: use only on your own
// hardware and an allowed band.
void rf_testgen_setup();

#endif // __VIGILANCE_RF_TESTGEN_H__
