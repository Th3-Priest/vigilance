#ifndef __VIGILANCE_STANDBY_H__
#define __VIGILANCE_STANDBY_H__

// Vigilance standby: the idle / boot scene. A sentinel eye living inside a
// radar, watching the real RF environment, with a mood that shifts with the
// threat level. Doubles as the Flipper-style screensaver (triggered after
// inactivity on the home screen) and as a startup app.
//
// Runs its own loop and returns on any input.
void vigStandbyRun(bool fromStartup);

#endif // __VIGILANCE_STANDBY_H__
