/* the gs wavetable synth behind midi output device 0. */
#ifndef WINMM_SYNTH_H
#define WINMM_SYNTH_H

typedef struct SYNTH SYNTH;

MMRESULT SynthOpen(SYNTH **out);
void SynthClose(SYNTH *s);
void SynthShort(SYNTH *s, DWORD message);
void SynthLong(SYNTH *s, const BYTE *data, DWORD size);
void SynthReset(SYNTH *s);
void SynthSetVolume(SYNTH *s, float level);
float SynthGetVolume(SYNTH *s);

#endif
