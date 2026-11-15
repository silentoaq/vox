export const EQ_PRESETS = [
  { id: 'flat', eq: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0] },
  { id: 'pop', eq: [-1, -1, 0, 2, 4, 4, 2, 0, -1, -1] },
  { id: 'rock', eq: [5, 4, 3, 1, -1, -1, 1, 3, 4, 5] },
  { id: 'vocal', eq: [-3, -3, -2, 0, 3, 5, 5, 3, 0, -1] },
  { id: 'bass', eq: [5, 7, 7, 5, 2, 0, 0, 0, 0, 0] }
];

export const REVERB_PRESETS = [
  { id: 'off', reverb: null },
  { id: 'studio', reverb: { seconds: 0.8, decay: 3, wet: 0.18 } },
  { id: 'ktv', reverb: { seconds: 1.6, decay: 2.5, wet: 0.28 } },
  { id: 'hall', reverb: { seconds: 3, decay: 2, wet: 0.32 } }
];

export function effectSettings(eqId, reverbId) {
  const eq = EQ_PRESETS.find((preset) => preset.id === eqId);
  const reverb = REVERB_PRESETS.find((preset) => preset.id === reverbId);
  if (!eq || !reverb) throw new Error(`沒有這組音效：${eqId}、${reverbId}`);
  return { eq: eq.eq, reverb: reverb.reverb };
}
