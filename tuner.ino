// Optical Strobe tuner for Guitar
// Aaron Zygmunt
// 9/8/2017

#include "OneButton.h"
#include "Volume3.h"

const double BASEFREQ = 440.0;
//Adjusted from 440 to a commercial tuner to compensate for arduino timing issues (442/440)
//const double FREQADJ = 1.004545454545;
const double FREQADJ = 441.0 / 440.0;
//const double FREQADJ = 1.0;
const double DUTY = .2;

const byte PIN_LEDS[2] = { 2, 3 };
const byte PIN_BUT = 4;
const byte PIN_SPK = 9;
int spk_vol = 512;

//note names for array indices
const byte N_C = 0, N_Cs = 1, N_D = 2, N_Ds = 3, N_E = 4, N_F = 5, N_Fs = 6,
		N_G = 7, N_Gs = 8, N_A = 9, N_As = 10, N_B = 11;

//Tuning names for array indices
const byte T_STANDARD = 0, T_CPENT = 1, T_BASS = 2, T_OCT = 3;

//tuning and note names for display
const String tunings[4] = { "Standard", "C Pentatonic", "Bass", "Octave" };
const String noteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#",
		"A", "A#", "B" };

//note names used for display
String notes[6];

// Hold time for changing tunings (2 seconds)
const unsigned long TUNINGHOLD = 2 * 1000000;

//array of note frequencies. First index is octave, second is note
double freq[9][12];

//the six frequencies in the current tuning
double frequencies[6];

//frequency of the tone to be played. Since the piezo buzzer clicks on transitions to both high and low this is half the note frequency
double freqtone;

//which note is being tuned
byte note = 0;

//which tuning the pick is in
byte tuning = 0;

//timing vales for the cycle in milliseconds
unsigned long period;
unsigned long litTime;
unsigned long phase;

//timing values needed for each cycle
//0=light 0 off
//1=light 1 on (beginning of 180deg phase)
//2=light 1 off
//3=light 0 on (end of cycle / beginning of next cycle_us)
unsigned long cycle_us[4];

//when the button was held
unsigned long h_us;

//whether or not the button is held
boolean held = false;

//whether or not the speaker is on
boolean speaker = false;
boolean vol_dir = true;

//initialize a OneButton instance
OneButton button(PIN_BUT, true);

void setup() {
	pinMode(PIN_LEDS[0], OUTPUT);
	pinMode(PIN_LEDS[1], OUTPUT);
	pinMode(PIN_SPK, OUTPUT);
	pinMode(8, OUTPUT);
	Serial.begin(9600);
	button.attachClick(nextNote);
	button.attachDoubleClick(toggleSpeaker);
	freqTable();
	setTuning();
	note = 5;
	nextNote();
}

void loop() {
	unsigned long current_us = micros();
	button.tick();

	//cycle tunings if button is held
	if (button.isLongPressed()) {
		digitalWrite(PIN_LEDS[0], LOW);
		digitalWrite(PIN_LEDS[1], LOW);
		if (speaker) {
			if (vol_dir) {
				spk_vol += 3;
				if (spk_vol > 1023) {
					spk_vol = 1023;
					vol_dir = false;
				}
			} else {
				spk_vol -= 3;
				if (spk_vol <= 0) {
					spk_vol = 0;
					vol_dir = true;
				}
			}
			playTone();
			delay(1);
		} else {
			if (!held) {
				//store the time the button was held in h_us
				h_us = current_us;
				held = true;
			}
			if (current_us > (h_us + TUNINGHOLD)) {
				++tuning;
				if (tuning >= sizeof(tunings) / sizeof(tunings[0])) {
					tuning = 0;
				}
				setTuning();
				h_us = current_us;
				note = 5;
				blink(tuning, PIN_LEDS[1]);
			}
		}
	}

	else {
		//check to see if the button was just released
		if (held) {
			nextNote();
			held = false;
		}

		//flash the lights based on the timings in cycle_us[]
		//calculate the cycle timing points at the beginning of each cycle
		if (current_us >= cycle_us[3]) {
			digitalWrite(PIN_LEDS[0], HIGH);
			digitalWrite(PIN_LEDS[1], LOW);
			cycle_us[0] = current_us + litTime;
			cycle_us[1] = current_us + phase;
			cycle_us[2] = cycle_us[1] + litTime;
			cycle_us[3] = current_us + period;
		} else if (current_us >= cycle_us[2]) {
			digitalWrite(PIN_LEDS[1], LOW);
		} else if (current_us >= cycle_us[1]) {
			digitalWrite(PIN_LEDS[1], HIGH);
			digitalWrite(PIN_LEDS[0], LOW);
		} else if (current_us >= cycle_us[0]) {
			digitalWrite(PIN_LEDS[0], LOW);
		}
	}
}

void blink(byte n, byte pin) {
	digitalWrite(PIN_LEDS[0], LOW);
	digitalWrite(PIN_LEDS[1], LOW);
	vol.noTone();
	for (byte i = 0; i <= n; ++i) {
		digitalWrite(pin, HIGH);
		delay(20);
		digitalWrite(pin, LOW);
		delay(60);
	}
}

void toggleSpeaker() {
	speaker = !speaker;
	playTone();
}

void playTone() {
	if (speaker) {
		Serial.println("Speaker on");
		vol.tone(PIN_SPK, freqtone, spk_vol);
	} else {
		Serial.println("Speaker off");
		vol.noTone();
	}
}

void nextNote() {
	++note;
	if (note > 5) {
		note = 0;
	}
	setTimings(frequencies[note] * FREQADJ);
	Serial.println(
			"note: " + String(note) + ", " + notes[note] + ", freq: "
					+ String(frequencies[note], 4) + "Hz, period: "
					+ String(period) + "us, phase: " + String(phase)
					+ "us, lit: " + String(litTime) + "us");
	blink(note, PIN_LEDS[0]);
	playTone();
}

//calculates the lengths of the period, half-cycle phase offset, and high duty cycle in microseconds from a given frequency in Hz
void setTimings(double f) {
	period = round(1000000.00 / f);
	phase = round(period / 2.0);
	litTime = round(double(phase) * DUTY);
	freqtone = f / 2.0;
}

//Calculate note frequencies and store them in a two-dimensional array
//The first index is the octave, the second is the note
void freqTable() {
	Serial.println("Calculating frequencies");
	int step = 0;
	for (int oct = 0; oct < 9; ++oct) {
		for (int note = 0; note < 12; ++note) {
			freq[oct][note] = pow(2.0, (float(step - 57) / 12.0)) * BASEFREQ;
			++step;
		}
	}
}

//Set the frequencies for a chosen tuning
void setTuning() {
	Serial.println(tunings[tuning]);
	switch (tuning) {
	case T_STANDARD:
		frequencies[0] = freq[2][N_E];
		frequencies[1] = freq[2][N_A];
		frequencies[2] = freq[3][N_D];
		frequencies[3] = freq[3][N_G];
		frequencies[4] = freq[3][N_B];
		frequencies[5] = freq[4][N_E];
		notes[0] = "E2";
		notes[1] = "A2";
		notes[2] = "D3";
		notes[3] = "G3";
		notes[4] = "B3";
		notes[5] = "E4";
		break;
	case T_CPENT:
		frequencies[0] = freq[2][N_C];
		frequencies[1] = freq[2][N_G];
		frequencies[2] = freq[3][N_D];
		frequencies[3] = freq[3][N_A];
		frequencies[4] = freq[4][N_E];
		frequencies[5] = freq[4][N_G];
		notes[0] = "C2";
		notes[1] = "G2";
		notes[2] = "D3";
		notes[3] = "A3";
		notes[4] = "E4";
		notes[5] = "G4";
		break;
	case T_BASS:
		frequencies[0] = freq[0][N_B];
		frequencies[1] = freq[1][N_E];
		frequencies[2] = freq[1][N_A];
		frequencies[3] = freq[2][N_D];
		frequencies[4] = freq[2][N_G];
		frequencies[5] = freq[3][N_C];
		notes[0] = "B0";
		notes[1] = "E1";
		notes[2] = "A1";
		notes[3] = "D2";
		notes[4] = "G2";
		notes[5] = "C3";
		break;
	case T_OCT:
		frequencies[0] = freq[3][N_E];
		frequencies[1] = freq[3][N_A];
		frequencies[2] = freq[4][N_D];
		frequencies[3] = freq[4][N_G];
		frequencies[4] = freq[4][N_B];
		frequencies[5] = freq[5][N_E];
		notes[0] = "E3";
		notes[1] = "A3";
		notes[2] = "D4";
		notes[3] = "G4";
		notes[4] = "B4";
		notes[5] = "E5";
		break;
	}
	for (int i = 0; i < 6; ++i) {
		Serial.print(notes[i] + " ");
	}
	Serial.print("\n");
}
