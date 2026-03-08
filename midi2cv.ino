/*
  Francesco Verdoja
  2026

  Monophonic - Last-note priority, with 20 notes in buffer

  Note CV output (1V/octave, 88 keys if amplified, 60 keys if not)
  Velocity CV output (0 to 5V)

  Pitch bend CV output (0 to 1V, witch 0.5 central possition)
  4x Control Change CV output (0 to 5V, CC1-4)

  Gate output (5V on noteOn until noteOff)
  Trigger output (5V, 20 msec pulse)
  Clock output (5V, 20 msec pulse on quarter notes)
*/
#include <MIDI.h>
#include <Adafruit_MCP4728.h>

// -----------------------------------------------------------------------------
// DEFINITIONS
// -----------------------------------------------------------------------------
#define UNASSIGNED -1

// MIDI Channel (1-16, 0 for omni)
#define MIDI_CHANNEL 3

// Arduino digital pin mappings
#define GATE_PIN 4
#define TRIGGER_PIN UNASSIGNED
#define CLOCK_PIN 5

// DAC pin mappings
#define PITCH_CH MCP4728_CHANNEL_A
#define BEND_CH UNASSIGNED
#define VELO_CH MCP4728_CHANNEL_B
#define CC1_CH MCP4728_CHANNEL_C
#define CC2_CH MCP4728_CHANNEL_D
#define CC3_CH UNASSIGNED
#define CC4_CH UNASSIGNED

// is pitch amplified? (60 keys from C0 if false, 88 from A-1 if true)
#define PITCH_AMP false

// trigger and clock lengths in milliseconds
#define TRIGGER_LENGTH 20
#define CLOCK_LENGTH 20

// -----------------------------------------------------------------------------
// INITIALIZATION
// -----------------------------------------------------------------------------
MIDI_CREATE_DEFAULT_INSTANCE();

Adafruit_MCP4728 mcp;

const byte minPitch = PITCH_AMP ? 21 : 24; // A-1 or C0
const byte noteRange = PITCH_AMP ? 88 : 60;
const byte bufferSize = 20;
byte notesBuffer[bufferSize] = {};
byte velocities[noteRange] = {};
uint16_t pitchTable[noteRange] = {};
int currentIndex = -1;
unsigned long triggerTimer = 0, clockTimer = 0, clockTimeout = 0, now = 0;
unsigned int clockCount = 0;

// -----------------------------------------------------------------------------
// CALLBACKS
// -----------------------------------------------------------------------------
void callbackNoteOn(byte channel, byte pitch, byte velocity)
{
  byte note = pitch - minPitch;
  if (note >= 0 && note < noteRange)
  {
    velocities[note] = velocity;
    if (currentIndex < (bufferSize - 1))
      currentIndex++;
    notesBuffer[currentIndex] = note;
    emitNote(note);
  }
}

void callbackNoteOff(byte channel, byte pitch, byte velocity)
{
  byte note = pitch - minPitch;
  if (note >= 0 && note < noteRange)
  {
    int i;
    // find note to noteOff in buffer
    for (i = currentIndex; i >= 0; i--)
    {
      if (notesBuffer[i] == note)
      {
        notesBuffer[i] = 255;
        break;
      }
    }

    // find last-priority note in buffer to emit
    for (i = currentIndex; i >= 0; i--)
    {
      if (notesBuffer[i] != 255)
      {
        emitNote(notesBuffer[i]);
        break;
      }
    }
    currentIndex = i;

    // if buffer is empty, send gate off
    if (currentIndex == -1 && GATE_PIN != UNASSIGNED)
    {
      digitalWrite(GATE_PIN, LOW);
    }
  }
}

void callbackPitchBend(byte channel, int bend)
{
  // Pitch Bend range from 0 to 16383
  // Right shift by 3 to scale from 0 to 2047
  unsigned int val = (bend << 7);
  mcp.setChannelValue(BEND_CH, val >> 3, MCP4728_VREF_INTERNAL, MCP4728_GAIN_1X);
}

void callbackControlChange(byte channel, byte number, byte value)
{
  // CC range from 0 to 127
  // Left shift value by 5 to scale from 0 to 4095
  byte activeChannel = UNASSIGNED;
  if (number == 1 && CC1_CH != UNASSIGNED)
    activeChannel = CC1_CH;
  else if (number == 2 && CC2_CH != UNASSIGNED)
    activeChannel = CC2_CH;
  else if (number == 3 && CC3_CH != UNASSIGNED)
    activeChannel = CC3_CH;
  else if (number == 4 && CC4_CH != UNASSIGNED)
    activeChannel = CC4_CH;
  if (activeChannel != UNASSIGNED)
    mcp.setChannelValue(activeChannel, value << 5, MCP4728_VREF_VDD, MCP4728_GAIN_1X);
}

void callbackClock(void)
{
  now = millis();
  if (now > clockTimeout + 300)
    clockCount = 0; // Prevents Clock from starting in between quarter notes after clock is restarted!
  clockTimeout = now;

  if (clockCount == 0)
  {
    digitalWrite(CLOCK_PIN, HIGH); // Start clock pulse
    clockTimer = now;
  }
  clockCount++;
  if (clockCount == 24)
  { // MIDI timing clock sends 24 pulses per quarter note.  Sent pulse only once every 24 pulses
    clockCount = 0;
  }
}

// -----------------------------------------------------------------------------
// SETUP
// -----------------------------------------------------------------------------
void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);

  // Init DAC
  if (!mcp.begin())
  {
    bool on = 0;
    while (1)
    {
      on = !on;
      digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
      delay(500);
    }
  }

  float pitchFactor = 4095.0f / (noteRange - 1);
  for (int i = 0; i < noteRange; i++)
  {
    pitchTable[i] = i * pitchFactor;
  }
  for (int i = 0; i < bufferSize; i++)
  {
    notesBuffer[i] = 255;
  }

  // Clock
  if (CLOCK_PIN != UNASSIGNED)
  {
    MIDI.setHandleClock(callbackClock);
    pinMode(CLOCK_PIN, OUTPUT);
    digitalWrite(CLOCK_PIN, LOW);
  }

  if (GATE_PIN != UNASSIGNED || TRIGGER_PIN != UNASSIGNED || PITCH_CH != UNASSIGNED || VELO_CH != UNASSIGNED)
  {
    MIDI.setHandleNoteOn(callbackNoteOn);
    MIDI.setHandleNoteOff(callbackNoteOff);
    // Gate
    if (GATE_PIN != UNASSIGNED)
    {
      pinMode(GATE_PIN, OUTPUT);
      digitalWrite(GATE_PIN, LOW);
    }
    // Trigger
    if (TRIGGER_PIN != UNASSIGNED)
    {
      pinMode(TRIGGER_PIN, OUTPUT);
      digitalWrite(TRIGGER_PIN, LOW);
    }
    // Pitch
    if (PITCH_CH != UNASSIGNED)
      mcp.setChannelValue(PITCH_CH, 0);
    // Velocity
    if (VELO_CH != UNASSIGNED)
      mcp.setChannelValue(VELO_CH, 0);
  }

  // Pitch bend
  if (BEND_CH != UNASSIGNED)
  {
    MIDI.setHandlePitchBend(callbackPitchBend);
    // Set initial pitch bend voltage to 0.5V (mid point)
    mcp.setChannelValue(BEND_CH, 1023, MCP4728_VREF_INTERNAL, MCP4728_GAIN_1X);
  }

  // CC
  if (CC1_CH != UNASSIGNED || CC2_CH != UNASSIGNED || CC3_CH != UNASSIGNED || CC4_CH != UNASSIGNED)
  {
    MIDI.setHandleControlChange(callbackControlChange);
    if (CC1_CH != UNASSIGNED)
      mcp.setChannelValue(CC1_CH, 0);
    if (CC2_CH != UNASSIGNED)
      mcp.setChannelValue(CC2_CH, 0);
    if (CC3_CH != UNASSIGNED)
      mcp.setChannelValue(CC3_CH, 0);
    if (CC4_CH != UNASSIGNED)
      mcp.setChannelValue(CC4_CH, 0);
  }

  // Init MIDI
  MIDI.begin(MIDI_CHANNEL);

  digitalWrite(LED_BUILTIN, HIGH);
}

// -----------------------------------------------------------------------------
// LOOP
// -----------------------------------------------------------------------------
void loop()
{
  MIDI.read();

  now = millis();
  if (triggerTimer > 0 && (now - triggerTimer) > TRIGGER_LENGTH)
  {
    triggerTimer = 0;
    digitalWrite(TRIGGER_PIN, LOW);
  }

  if ((clockTimer > 0) && (now - clockTimer > CLOCK_LENGTH))
  {
    clockTimer = 0;
    digitalWrite(CLOCK_PIN, LOW);
  }
}

void emitNote(byte note)
{
  if (PITCH_CH != UNASSIGNED)
    if (PITCH_AMP)
    {
      // Volts per note = 4095 / (88 - 1) = 47.06896551724138
      // The OPAMP need amplifier 1,77x -> A-1 -- C7 (88 keys)
      mcp.setChannelValue(PITCH_CH, pitchTable[note], MCP4728_VREF_INTERNAL, MCP4728_GAIN_2X);
    }
    else
    {
      // Volts per note = 4095 / (60 - 1) = 69.40677966101694
      // With VREF_VDD -> C0 -- B4 (60 keys)
      mcp.setChannelValue(PITCH_CH, pitchTable[note], MCP4728_VREF_VDD, MCP4728_GAIN_1X);
    }

  // Velocity range from 0 to 4095 mV
  // Left shift by 5 to scale from 0 to 4095
  if (VELO_CH != UNASSIGNED)
    mcp.setChannelValue(VELO_CH, velocities[note] << 5, MCP4728_VREF_VDD, MCP4728_GAIN_1X);

  if (GATE_PIN != UNASSIGNED)
    digitalWrite(GATE_PIN, HIGH);

  if (TRIGGER_PIN != UNASSIGNED)
  {
    digitalWrite(TRIGGER_PIN, HIGH);
    triggerTimer = millis();
  }
}