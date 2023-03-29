#include <EEPROM.h>
#include <TimerOne.h>
#include "version.h"
#include "globals.h"
#include "ui_leds.h"
#include "midi.h"
#include "arp.h"
#include "ui_pots.h"
#include "voice_state.hpp"
#include "midi_pedal.hpp"

static byte mStatus;
static byte mData;
static byte mChannel;

static byte syncLfoCounter;
static float lfoClockRates[] = {2.6562, 5.3125, 7.96875, 10.625, 21.25, 31.875, 42.5, 85};

static float lfoStepF[3];
static byte lfoClockSpeed[3];
static int clockCount;
static bool thru;

static int dumpCounter = 0;
static byte dump;
static bool alternator;
static byte flash;

static byte val[] = {0, 128};

static void HandleNoteOn(byte channel, byte note, byte velocity);
static void HandleNoteOff(byte channel, byte note);

static MidiPedalAdapter pedal_adapter(HandleNoteOn, HandleNoteOff);

static void HandleNoteOn(byte channel, byte note, byte velocity) {
	if (note < 12 || note >= 107)
		return;

	note -= 12;

	if (velocity) {
		for (int i = 0; i < 3; i++) {
			if (preset_data.lfo[i].retrig) {
				lfoStep[i] = lfoStepF[i] = 0;
			}
		}
	}

	if (channel == masterChannel) {
		if (velocity) {
			velocityLast = velocity;
		}

		switch (voice_state.note_on(note, velocity)) {
			case VoiceStateEvent::LAST_NOTE_OFF:
				envState = 4;
				arpRound = 0;
				break;
			case VoiceStateEvent::FIRST_NOTE_ON:
				envState = 1;
				env = 0;
				clockCount = preset_data.arp_rate;

				if (preset_data.arp_mode)
					arpReset();
				break;
			default:
				break;
		}
	} else if (!preset_data.is_polyphonic() &&
	           (channel == voice1Channel || channel == voice2Channel || channel == voice3Channel)) {
		auto voice = 0;

		if (channel == voice2Channel) {
			voice = 1;
		} else if (channel == voice3Channel) {
			voice = 2;
		}

		if (velocity) {
			voice_state.note_on_individual(voice, note);
			voice_state.note_on_individual(voice + 3, note);
		} else {
			voice_state.note_off_individual(voice, note);
			voice_state.note_off_individual(voice + 3, note);
		}

		if (velocity) {
			voice_state.note_on_individual(voice, note);
			voice_state.note_on_individual(voice + 3, note);
		} else {
			voice_state.note_off_individual(voice, note);
			voice_state.note_off_individual(voice + 3, note);
		}
	}
	leftDot();
}

static void HandleNoteOff(byte channel, byte note) { HandleNoteOn(channel, note, 0); }

byte lastData1, lastData2; // keep track of last CC and Value for handshake with tool.

static void HandleControlChange(byte channel, byte data1, byte data2) {
	leftDot();

	if ((channel == 16) && (lastData1 == 19) && (lastData2 == 82) && (data1 == 19) && (data2 == 82)) {
		// transmit all the settings to tool. Tool expects noteOff messages on CH16 (yeah I couldn't get webMidi to
		// parse sysex... )
		sendNoteOff(1, version, 16);
		sendNoteOff(2, versionDecimal, 16);
		sendNoteOff(3, masterChannel, 16);
		sendNoteOff(4, voice1Channel, 16);
		sendNoteOff(5, voice2Channel, 16);
		sendNoteOff(6, voice3Channel, 16);
		sendNoteOff(7, masterChannelOut, 16);
		sendNoteOff(8, volume, 16);

		sendNoteOff(9, modToLfo, 16);
		sendNoteOff(10, aftertouchToLfo, 16);
		sendNoteOff(11, velocityToLfo, 16);
		sendNoteOff(12, sendLfo, 16);
		sendNoteOff(13, sendArp, 16);
		sendNoteOff(14, pwLimit, 16);

		toolMode = true; // therapSid is listening to new settings (CC on CH16)
	}
	// did we receive 1982 twice on channel 16?

	lastData1 = data1;
	lastData2 = data2;
	if ((channel == 16) && (toolMode) && (data1 == 85)) {
		if (data2) {
			EEPROM.update(3994, 1);
			modToLfo = 1;
		} else {
			EEPROM.update(3994, 0);
			modToLfo = 0;
		}
	} // mod wheel -> lfo depth1
	else if ((channel == 16) && (toolMode) && (data1 == 86)) {
		if (data2) {
			EEPROM.update(3993, 1);
			aftertouchToLfo = 1;
		} else {
			EEPROM.update(3993, 0);
			aftertouchToLfo = 0;
		}
	} // aftertouch -> lfo depth2
	else if ((channel == 16) && (toolMode) && (data1 == 87)) {
		if (data2) {
			EEPROM.update(3992, 1);
			velocityToLfo = 1;
		} else {
			EEPROM.update(3992, 0);
			velocityToLfo = 0;
		}
	} // velocity -> lfo depth3
	else if ((channel == 16) && (toolMode) && (data1 == 88)) {
		if (data2) {
			EEPROM.update(3990, 1);
			pwLimit = 1;
		} else {
			EEPROM.update(3990, 0);
			pwLimit = 0;
		}
	} // pwLimit

	else if ((channel == 16) && (toolMode) && (data1 == 89)) {
		volumeChanged = true;
		volume = data2;
		if ((volume > 15) || (volume < 1)) {
			volume = 15;
		}
		EEPROM.update(3991, volume);
	} // master volume

	else if ((channel == 16) && (toolMode) && (data1 == 90)) {
		if (data2 < 16) {
			EEPROM.update(3998, data2 + 1);
			masterChannel = data2 + 1;
		}
	} // master input channel

	else if ((channel == 16) && (toolMode) && (data1 == 91)) {
		if (data2 < 16) {
			EEPROM.update(3997, data2 + 1);
			masterChannelOut = data2 + 1;
		}
	} // master output channel
	else if ((channel == 16) && (toolMode) && (data1 == 92)) {
		if (data2) {
			EEPROM.update(3996, 1);
			sendLfo = 1;
		} else {
			EEPROM.update(3996, 0);
			sendLfo = 0;
		}
	} // lfo transmits CC
	else if ((channel == 16) && (toolMode) && (data1 == 94)) {
		if (data2 < 16) {
			EEPROM.update(3989, data2 + 1);
			voice1Channel = data2 + 1;
		}
	} // master output channel voice1
	else if ((channel == 16) && (toolMode) && (data1 == 95)) {
		if (data2 < 16) {
			EEPROM.update(3988, data2 + 1);
			voice2Channel = data2 + 1;
		}
	} // master output channel voice2
	else if ((channel == 16) && (toolMode) && (data1 == 96)) {
		if (data2 < 16) {
			EEPROM.update(3987, data2 + 1);
			voice3Channel = data2 + 1;
		}
	} // master output channel voice3

	else if ((channel == 16) && (toolMode) && (data1 == 93)) {
		if (data2) {
			EEPROM.update(3995, 1);
			sendArp = 1;
		} else {
			EEPROM.update(3995, 0);
			sendArp = 0;
		}
	} // arp transmits MIDI notes
	else if (channel == masterChannel) {
		if (data1 == 59)
			data1 = 32;

		if (data1 == 1) {
			modWheelLast = data2;
		}

		if (1 <= data1 && data1 <= 36) {
			int mapping[] = {-1, -1, 4,  6,  14, 1,  5, 15, 13, 16, 24, 26, 17, 27, 22, 25, 23, 20, 30,
			                 21, 31, 19, 29, 18, 28, 3, 11, 12, 10, 9,  36, 2,  8,  0,  7,  41, 32};
			movedPot(mapping[data1], data2 << 3, true);
		} else if (37 <= data1 && data1 <= 48) {
			int offset = data1 - 37;
			static const PresetVoice::Shape mapping[] = {PresetVoice::PULSE, PresetVoice::TRI, PresetVoice::SAW,
			                                             PresetVoice::NOISE};
			preset_data.voice[offset / 4].set_shape(mapping[offset % 4], data2);
		} else {
			switch (data1) {
				case 49: // sync1
					bitWrite(preset_data.voice[0].reg_control, 1, data2);
					break;
				case 50: // ring1
					bitWrite(preset_data.voice[0].reg_control, 2, data2);
					break;

				case 51: // sync2
					bitWrite(preset_data.voice[1].reg_control, 1, data2);
					break;
				case 52: // ring2
					bitWrite(preset_data.voice[1].reg_control, 2, data2);
					break;

				case 53: // sync3
					bitWrite(preset_data.voice[2].reg_control, 1, data2);
					break;
				case 54: // ring3
					bitWrite(preset_data.voice[2].reg_control, 2, data2);
					break;

				case 55:
					preset_data.filter_mode = static_cast<FilterMode>(map(data2, 0, 127, 0, 4));
					break;

				case 64:
					pedal_adapter.set_pedal(channel, data2 >= 64);
					break;

				case 68:
					if (data2) {
						sendLfo = true;
						EEPROM.update(3996, 0);
					} else {
						sendLfo = false;
						EEPROM.update(3996, 1);
					}
					break; // lfo send
				case 69:
					if (data2) {
						sendArp = true;
						EEPROM.update(3995, 0);
					} else {
						sendArp = false;
						EEPROM.update(3995, 1);
					}
					break; // arp send

				case 85:
					if (data2) {
						sendArp = true;
						EEPROM.update(3995, 0);
					} else {
						sendArp = false;
						EEPROM.update(3995, 1);
					}
					break; // arp send
			}
		}
	}
}

static void handleBend(byte channel, int value) {
	//-8192 to 8191
	float value_f = (value - 64.f) / 64.f;
	if (value_f > 1)
		value_f = 1;
	if (value_f < -1)
		value_f = -1;

	if (masterChannel == 1) {
		if (channel == 1)
			bend = value_f;
		else if (channel == 2)
			bend1 = value_f;
		else if (channel == 3)
			bend2 = value_f;
		else if (channel == 4)
			bend3 = value_f;
	} else {
		if (channel == masterChannel) {
			bend = value_f;
		}
	}
}

void sendMidiButt(byte number, int value) {
	rightDot();
	sendControlChange(number, !!value);
}

void sendCC(byte number, int value) {
	rightDot();
	sendControlChange(number, value >> 3);
}

void sendControlChange(byte number, byte value) {
	if (!thru) {
		Serial.write(175 + masterChannelOut);
		Serial.write(number);
		Serial.write(value);
	}
}

void sendNoteOff(byte note, byte velocity, byte channel) {
	if (!thru) {
		Serial.write(127 + channel);
		Serial.write(note);
		Serial.write(velocity);
	}
}

void sendNoteOn(byte note, byte velocity, byte channel) {
	if (!thru) {
		Serial.write(143 + channel);
		Serial.write(note);
		Serial.write(velocity);
	}
}

void midiRead() {
	while (Serial.available()) {
		byte input = Serial.read();

		if (thru)
			Serial.write(input);

		if (input & 0x80) {

			switch (input) {

				case 0xf8: // clock

					sync = 1;
					if ((preset_data.arp_mode) && arping()) {
						clockCount++;
						if (clockCount >= preset_data.arp_rate) {
							clockCount = 0;
							arpTick();
						}
					}

					syncLfoCounter++;
					if (syncLfoCounter == 24) {
						syncLfoCounter = 0;
						for (int i = 0; i < 3; i++) {
							if (lfoClockSpeedPending[i]) {
								lfoClockSpeed[i] = lfoClockSpeedPending[i] - 1;
								lfoClockSpeedPending[i] = 0;
								lfoStepF[i] = 0;
							}
						}
					}

					for (int i = 0; i < 3; i++) {
						lfoStepF[i] += lfoClockRates[lfoClockSpeed[i]];
						if (lfoStepF[i] > 254) {
							if (preset_data.lfo[i].looping) {
								lfoStepF[i] = 0;
								lfoNewRand[i] = 1;
							} else {
								lfoStepF[i] = 255;
							}
						}
						lfoStep[i] = int(lfoStepF[i]);
					}

					break;
				case 0xfa: // start
					syncLfoCounter = 0;
					sync = 1;
					lfoStepF[0] = lfoStepF[1] = lfoStepF[2] = 0;
					break;
				case 0xfb: // continue
					break;
				case 0xfc: // stop
					sync = 0;
					break;

				case 128 ... 143:
					mChannel = input - 127;
					mStatus = 2;
					mData = 255;
					break; // noteOff
				case 144 ... 159:
					mChannel = input - 143;
					mStatus = 1;
					mData = 255;
					break; // noteOn
				case 176 ... 191:
					mChannel = input - 175;
					mStatus = 3;
					mData = 255;
					break; // CC
				case 192 ... 207:
					mChannel = input - 191;
					mStatus = 6;
					mData = 0;
					break; // program Change
				case 208 ... 223:
					mChannel = input - 207;
					mStatus = 5;
					mData = 0;
					break; // Aftertouch
				case 224 ... 239:
					mChannel = input - 223;
					mStatus = 4;
					mData = 255;
					break; // Pitch Bend

				default:
					mStatus = 0;
					mData = 255;
					break;
			}
		}

		// status
		else {
			if (mData == 255) {
				mData = input;
			} // data byte 1
			else {

				// data byte 2
				switch (mStatus) {
					case 1:
						if (mChannel == masterChannel) {
							pedal_adapter.note_on(mChannel, mData, input);
						} else {
							HandleNoteOn(mChannel, mData, input);
						}
						mData = 255;
						break; // noteOn
					case 2:
						if (mChannel == masterChannel) {
							pedal_adapter.note_off(mChannel, mData);
						} else {
							HandleNoteOff(mChannel, mData);
						}
						mData = 255;
						break; // noteOff
					case 3:
						HandleControlChange(mChannel, mData, input);
						mData = 255;
						break; // CC
					case 4:
						handleBend(mChannel, input);
						mData = 255;
						break; // bend
					case 5:
						aftertouch = input;
						mData = 255;
						break; // AT
					case 6:
						if (mChannel == masterChannel) {
							preset = input + 1;
							if (preset > 99) {
								preset = 1;
							}
						}
						mData = 0;
						break; // PC
				}
			} // data
		}
	}
}
//////////////////////////////////////////////////////////////////////////////////////////////////

void midiOut(byte note) {
	rightDot();
	sendNoteOff(lastNote + 1, 127, masterChannelOut);
	sendNoteOn(note + 1, velocityLast, masterChannelOut);
	lastNote = note;
}

void sendDump() {

	digit(0, 5);
	digit(1, 18);

	byte mem[4000];
	for (int i = 0; i < 4000; i++) {
		mem[i] = EEPROM.read(i);
	}
	byte nill = 0;

	Serial.write(240);
	delay(1);
	Serial.write(100);
	delay(1);

	for (int i = 0; i < 4000; i++) {

		if (mem[i] > 127) {
			Serial.write(mem[i] - 128);
			delay(1);
			Serial.write(1);
			delay(1);
		} else {
			Serial.write(mem[i]);
			delay(1);
			Serial.write(nill);
			delay(1);
		}
	}
	Serial.write(247);
	Serial.flush();
}

void recieveDump() {
	byte mem[4000];
	digit(0, 16);
	digit(1, 18);

	Timer1.stop();

	while (dump != 247) {

		if (Serial.available() > 0) {

			dump = Serial.read();

			if (dumpCounter == 0) {
				if (dump == 240) {
					dumpCounter++;
				} else {
					dumpCounter = 247;
				}
			} // must be sysex or cancel
			else if (dumpCounter == 1) {
				if (dump == 100) {
					dumpCounter++;
				} else {
					dumpCounter = 247;
				}
			} // must be 100 manuCode or cancel

			else {

				alternator = !alternator;

				if (alternator) {
					flash = dump;
				} else {
					flash += val[dump];
					//
					mem[dumpCounter - 2] = flash;
					dumpCounter++;
				}
			}
		}
	}

	byte ledLast = 0;
	for (int i = 0; i < 4000; i++) {

		if ((i != 3998) && (i != 3997)) { // don't overWrite MIDI channels!!
			EEPROM.update(i, mem[i]);
			if (ledLast != i / 40) {
				ledLast = i / 40;
				ledNumber(ledLast);
			}
		}
	}
	digit(0, 99);
	digit(1, 99);

	Timer1.resume();

	setup();
}
