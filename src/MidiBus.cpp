/*
	MidiBus.cpp

	Code for the OrangeLine module MidiBus

Copyright (C) 2019 Dieter Stubler

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include <string>
#include <stdio.h>

#include "MidiBus.hpp"

#define RATE_LIMITER_PERIOD (1.f / 200.f)
#define CC_ACTIVITY_DECAY 0.003f
#define SDLY_MAX_SAMPLES 999
#define SDLY_HISTORY_DEPTH 64

/**
	sDLY (Dieter): delays only the *internal* RX->TX passthrough (the "bus drives straight
	through" case when a TX infix input is left unpatched) by 0-999 real audio samples - a
	patched infix always stays live/instant, since synchronizing external patch cabling is the
	user's own job ("dafür ist der User im Patch zuständig"), not MIDIBUS's. Motivating case:
	an external quantizer sits on the V/Oct infix, but Gate is left unpatched - without a delay,
	Gate would reach the synth before the quantizer has produced a corrected V/Oct.

	Sparse frame-timestamped history rather than a dense per-sample ring buffer: the internal
	value only actually changes once per throttled tick anyway (~every 43 samples), so most of a
	dense 1000-slot buffer would just be duplicate copies of the same value. 64 (frame, value)
	entries comfortably covers the ~23 throttled ticks needed for the maximum 999 sample delay.
	Uses args.frame (already used for MIDI message timestamps elsewhere in this file) as the
	sample clock - no changes needed to OrangeLineCommon.hpp's shared throttling.
*/
struct DelayHistory
{
	int64_t frame[SDLY_HISTORY_DEPTH];
	float value[SDLY_HISTORY_DEPTH];
	int writeIdx;

	void reset()
	{
		writeIdx = 0;
		for (int i = 0; i < SDLY_HISTORY_DEPTH; i++)
		{
			frame[i] = -1000000000LL;
			value[i] = 0.f;
		}
	}

	void push(int64_t currentFrame, float v)
	{
		writeIdx = (writeIdx + 1) % SDLY_HISTORY_DEPTH;
		frame[writeIdx] = currentFrame;
		value[writeIdx] = v;
	}

	/** Most recent value that was already in effect at targetFrame. */
	float read(int64_t targetFrame) const
	{
		int idx = writeIdx;
		for (int i = 0; i < SDLY_HISTORY_DEPTH; i++)
		{
			if (frame[idx] <= targetFrame)
				return value[idx];
			idx = (idx - 1 + SDLY_HISTORY_DEPTH) % SDLY_HISTORY_DEPTH;
		}
		return value[idx];	// delay longer than the buffer covers - best effort, oldest we have
	}
};

/** Same idea as DelayHistory, but for one-shot trigger events instead of continuous values -
	queues occurrence frames and fires each one once the configured delay has elapsed. */
struct TriggerDelay
{
	static const int QUEUE_SIZE = 16;
	int64_t pending[QUEUE_SIZE];
	int head, count;

	void reset() { head = 0; count = 0; }

	void push(int64_t currentFrame)
	{
		if (count < QUEUE_SIZE)
		{
			pending[(head + count) % QUEUE_SIZE] = currentFrame;
			count++;
		}
	}

	bool pop(int64_t currentFrame, int delaySamples)
	{
		if (count > 0 && (currentFrame - pending[head]) >= delaySamples)
		{
			head = (head + 1) % QUEUE_SIZE;
			count--;
			return true;
		}
		return false;
	}
};

/**
	Pitch Bend <-> CV conversions. Raw pitch wheel values are 14 bit signed, -8192..8191 (0 =
	center/no bend). Default unipolar mapping keeps MIDIBUS's usual 0-10V convention (center at
	5V); PB_BIPOLAR_JSON switches to a classic bipolar -5V..+5V CV (center at 0V) instead.
*/
inline float pbRawToCv(int16_t pw, bool bipolar)
{
	if (bipolar)
		return clamp((float) pw / 8192.f * 5.f, -5.f, 5.f);
	return clamp(((float) pw + 8192.f) / 16383.f * 10.f, 0.f, 10.f);
}
inline int16_t pbCvToRaw(float cv, bool bipolar)
{
	if (bipolar)
		return (int16_t) clamp(std::round(cv / 5.f * 8192.f), -8192.f, 8191.f);
	return (int16_t) clamp(std::round(cv / 10.f * 16383.f - 8192.f), -8192.f, 8191.f);
}

/*
	Bus-stop metaphor (Dieter): every signal has an RX *output* (a read tap of what was just
	received) and a TX *input* (what actually gets sent - falls back to the RX value if left
	unpatched). If nothing is patched into a signal's TX infix, it just drives straight
	through; to intervene, you build an infix.

	Single MIDI channel each side (RX and TX independently selectable) - deliberately much
	lighter than the LANES family (no lane merging, no voice-stealing, no 16-channel-wide
	parser/generator arrays) since the use case here is one keyboard to one destination
	channel, with an insertion point to process along the way.
*/
struct MidiBusNoteGenerator : dsp::MidiGenerator<POLY_CHANNELS>
{
	midi::Output *output = nullptr;

	void onMessage(const midi::Message &m) override
	{
		if (output) output->sendMessage(m);
	}
};

struct MidiBus : Module
{

#include "OrangeLineCommon.hpp"

#include "MidiBusJsonLabels.hpp"

	bool widgetReady = false;

	midi::InputQueue midiInput;
	midi::Output midiOutput;
	dsp::MidiParser<POLY_CHANNELS> noteParser;
	MidiBusNoteGenerator noteGenerator;

	// RX-side CC state (raw, unmuted, unmerged).
	uint8_t ccValues[128];
	// TX-side CC bookkeeping - 1:1 the CV2CC model (see src/CV2CC.cpp), since the CC grid
	// here mirrors CV2CC's exactly.
	uint8_t lastSentValues[128];
	bool  ccEnabled[128];
	bool  ccWasEnabled[128];
	float ccActivity[128];
	float ccCvValue[128];
	bool  gridLocked = true;
	dsp::Timer rateLimiterTimer;
	bool needsInitialSeed = true;

	// Last Program Change received (0-127), held between events for RX_PROGRAM_OUTPUT.
	int lastReceivedProgram = 0;

	// RX-side Aftertouch/Pitch Bend state (manually parsed - see moduleProcess(), NOT via
	// noteParser's own built-in aftertouches[]/pws[] tracking, since that conflates poly
	// key-pressure and channel-pressure into one array and is MPE-per-channel-oriented for
	// pitch wheel - neither matches "AT is one mono value, pAT is a separate per-note poly
	// value" that Dieter wants here).
	uint8_t rxAftertouch = 0;
	uint8_t rxPolyAftertouch[POLY_CHANNELS];
	int16_t rxPitchBend = 0;

	// Channel number display values (NumberWidget reads these directly).
	float rxChannelDisplayValue = 1.f;
	float txChannelDisplayValue = 1.f;
	char rxChannelBuffer[3];
	char txChannelBuffer[3];
	// sDLY display (0-999).
	float sdlyDisplayValue = 0.f;
	char sdlyBuffer[4];

	// sDLY delay histories - one per continuous internal-passthrough signal, plus trigger-event
	// queues for the transport/PC infixes. See DelayHistory/TriggerDelay above.
	DelayHistory voctHistory[POLY_CHANNELS];
	DelayHistory gateHistory[POLY_CHANNELS];
	DelayHistory velHistory[POLY_CHANNELS];
	DelayHistory ccHistory[128];
	DelayHistory atHistory;
	DelayHistory patHistory[POLY_CHANNELS];
	DelayHistory pbHistory;
	DelayHistory bankHistory;
	DelayHistory programHistory;
	TriggerDelay pcDelay;
	TriggerDelay clockDelay;
	TriggerDelay startDelay;
	TriggerDelay stopDelay;
	TriggerDelay continueDelay;

	MidiBus()
	{
		noteGenerator.output = &midiOutput;

		for (int cc = 0; cc < 128; cc++)
		{
			lastSentValues[cc] = 0xFF;
			ccEnabled[cc] = true;
			ccWasEnabled[cc] = true;
			ccActivity[cc] = 0.f;
			ccCvValue[cc] = 0.f;
			ccValues[cc] = 0;
			ccHistory[cc].reset();
		}
		for (int slot = 0; slot < POLY_CHANNELS; slot++)
		{
			rxPolyAftertouch[slot] = 0;
			voctHistory[slot].reset();
			gateHistory[slot].reset();
			velHistory[slot].reset();
			patHistory[slot].reset();
		}
		atHistory.reset();
		pbHistory.reset();
		bankHistory.reset();
		programHistory.reset();
		pcDelay.reset();
		clockDelay.reset();
		startDelay.reset();
		stopDelay.reset();
		continueDelay.reset();

		moduleExtraDataToJson = [this](json_t *rootJ)
		{
			json_t *enabledJ = json_array();
			for (int cc = 0; cc < 128; cc++)
				json_array_append_new(enabledJ, json_boolean(ccEnabled[cc]));
			json_object_set_new(rootJ, "enabled", enabledJ);
			json_object_set_new(rootJ, "midiIn", midiInput.toJson());
			json_object_set_new(rootJ, "midiOut", midiOutput.toJson());
		};
		moduleExtraDataFromJson = [this](json_t *rootJ)
		{
			json_t *enabledJ = json_object_get(rootJ, "enabled");
			if (enabledJ)
			{
				for (int cc = 0; cc < 128; cc++)
				{
					json_t *flagJ = json_array_get(enabledJ, cc);
					if (flagJ)
						ccEnabled[cc] = ccWasEnabled[cc] = json_boolean_value(flagJ);
				}
			}
			json_t *midiInJ = json_object_get(rootJ, "midiIn");
			if (midiInJ)
				midiInput.fromJson(midiInJ);
			json_t *midiOutJ = json_object_get(rootJ, "midiOut");
			if (midiOutJ)
				midiOutput.fromJson(midiOutJ);
		};

		initializeInstance();
	}

	bool moduleSkipProcess()
	{
		return (idleSkipCounter != 0);
	}

	void moduleInitStateTypes()
	{
		setInPoly(TX_VOCT_INPUT, true);
		setInPoly(TX_GATE_INPUT, true);
		setInPoly(TX_VEL_INPUT, true);
		setInPoly(TX_PAT_INPUT, true);
		for (int n = 0; n < NUM_CC_BANKS; n++)
			setInPoly(TX_CC_INPUT + n, true);

		setOutPoly(RX_VOCT_OUTPUT, true);
		setOutPoly(RX_GATE_OUTPUT, true);
		setOutPoly(RX_VEL_OUTPUT, true);
		setOutPoly(RX_PAT_OUTPUT, true);
		for (int n = 0; n < NUM_CC_BANKS; n++)
			setOutPoly(RX_CC_OUTPUT + n, true);

		setStateTypeInput(TX_PC_TRIGGER_INPUT, STATE_TYPE_TRIGGER);
		setStateTypeInput(TX_CLOCK_INPUT,      STATE_TYPE_TRIGGER);
		setStateTypeInput(TX_START_INPUT,      STATE_TYPE_TRIGGER);
		setStateTypeInput(TX_STOP_INPUT,       STATE_TYPE_TRIGGER);
		setStateTypeInput(TX_CONTINUE_INPUT,   STATE_TYPE_TRIGGER);
		setStateTypeInput(FORCE_INPUT,         STATE_TYPE_TRIGGER);

		setStateTypeOutput(RX_PC_TRIGGER_OUTPUT, STATE_TYPE_TRIGGER);
		setStateTypeOutput(RX_CLOCK_OUTPUT,      STATE_TYPE_TRIGGER);
		setStateTypeOutput(RX_START_OUTPUT,      STATE_TYPE_TRIGGER);
		setStateTypeOutput(RX_STOP_OUTPUT,       STATE_TYPE_TRIGGER);
		setStateTypeOutput(RX_CONTINUE_OUTPUT,   STATE_TYPE_TRIGGER);
	}

	inline void moduleInitJsonConfig()
	{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"

		for (int i = 0; i < NUM_JSONS; i++)
			setJsonLabel(i, jsonLabel[i]);

#pragma GCC diagnostic pop
	}

	inline void moduleParamConfig()
	{
		configParam(RX_CHANNEL_PARAM, 1.f, 16.f, 1.f, "RX MIDI Channel");
		paramQuantities[RX_CHANNEL_PARAM]->snapEnabled = true;
		configParam(TX_CHANNEL_PARAM, 1.f, 16.f, 1.f, "TX MIDI Channel");
		paramQuantities[TX_CHANNEL_PARAM]->snapEnabled = true;
		configParam(GRID_LOCK_PARAM, 0.f, 1.f, 0.f, "Lock CC grid editing (click to toggle)");
		configParam(FLUSH_PARAM, 0.f, 1.f, 0.f, "Force resend all CCs");
		configParam(SDLY_PARAM, 0.f, (float) SDLY_MAX_SAMPLES, 0.f, "Internal passthrough delay (samples)");
		paramQuantities[SDLY_PARAM]->snapEnabled = true;

		configInput(RX_CHANNEL_CV_INPUT, "RX channel CV (1 semitone = 1 channel)");
		configInput(TX_CHANNEL_CV_INPUT, "TX channel CV (1 semitone = 1 channel)");
		configInput(TX_VOCT_INPUT, "TX V/Oct infix");
		configInput(TX_GATE_INPUT, "TX Gate infix");
		configInput(TX_VEL_INPUT,  "TX Velocity infix");
		configInput(TX_AT_INPUT,  "TX Aftertouch infix");
		configInput(TX_PAT_INPUT, "TX Poly Aftertouch infix");
		configInput(TX_PB_INPUT,  "TX Pitch Bend infix");
		for (int n = 0; n < NUM_CC_BANKS; n++)
			configInput(TX_CC_INPUT + n, string::f("TX CC %d-%d infix", n * 16, n * 16 + 15));
		configInput(TX_BANK_INPUT, "TX Bank infix");
		configInput(TX_PROGRAM_INPUT, "TX Program infix");
		configInput(TX_PC_TRIGGER_INPUT, "TX Program Change trigger infix");
		configInput(TX_CLOCK_INPUT, "TX Clock infix");
		configInput(TX_START_INPUT, "TX Start infix");
		configInput(TX_STOP_INPUT, "TX Stop infix");
		configInput(TX_CONTINUE_INPUT, "TX Continue infix");
		configInput(FORCE_INPUT, "Force resend all CCs");

		configOutput(RX_VOCT_OUTPUT, "RX V/Oct");
		configOutput(RX_GATE_OUTPUT, "RX Gate");
		configOutput(RX_VEL_OUTPUT,  "RX Velocity");
		configOutput(RX_AT_OUTPUT,  "RX Aftertouch");
		configOutput(RX_PAT_OUTPUT, "RX Poly Aftertouch");
		configOutput(RX_PB_OUTPUT,  "RX Pitch Bend");
		for (int n = 0; n < NUM_CC_BANKS; n++)
			configOutput(RX_CC_OUTPUT + n, string::f("RX CC %d-%d", n * 16, n * 16 + 15));
		configOutput(RX_BANK_OUTPUT, "RX Bank");
		configOutput(RX_PROGRAM_OUTPUT, "RX Program");
		configOutput(RX_PC_TRIGGER_OUTPUT, "RX Program Change trigger");
		configOutput(RX_CLOCK_OUTPUT, "RX Clock");
		configOutput(RX_START_OUTPUT, "RX Start");
		configOutput(RX_STOP_OUTPUT, "RX Stop");
		configOutput(RX_CONTINUE_OUTPUT, "RX Continue");
	}

	inline void moduleCustomInitialize() {}

	inline void moduleInitialize()
	{
		noteParser.reset();
		noteParser.setChannels(POLY_CHANNELS);
		noteGenerator.reset();
		lastReceivedProgram = 0;
	}

	void moduleReset()
	{
		for (int cc = 0; cc < 128; cc++)
		{
			lastSentValues[cc] = 0xFF;
			ccEnabled[cc] = true;
			ccWasEnabled[cc] = true;
			ccActivity[cc] = 0.f;
			ccCvValue[cc] = 0.f;
			ccValues[cc] = 0;
			ccHistory[cc].reset();
		}
		for (int slot = 0; slot < POLY_CHANNELS; slot++)
		{
			rxPolyAftertouch[slot] = 0;
			voctHistory[slot].reset();
			gateHistory[slot].reset();
			velHistory[slot].reset();
			patHistory[slot].reset();
		}
		rxAftertouch = 0;
		rxPitchBend = 0;
		atHistory.reset();
		pbHistory.reset();
		bankHistory.reset();
		programHistory.reset();
		pcDelay.reset();
		clockDelay.reset();
		startDelay.reset();
		stopDelay.reset();
		continueDelay.reset();
		midiInput.reset();
		midiOutput.reset();
		rateLimiterTimer.reset();
		needsInitialSeed = true;
		setStateJson(FLUSH_ON_START_JSON, 1.f);
		setStateJson(SYSEX_PASSTHROUGH_JSON, 0.f);
		setStateJson(CLOCK_PASSTHROUGH_JSON, 1.f);
		setStateJson(TRANSPORT_PASSTHROUGH_JSON, 1.f);
		setStateJson(PB_BIPOLAR_JSON, 0.f);
		gridLocked = true;
		styleChanged = true;
	}

	/**
		System messages (status 0xf: SysEx/Clock/Start/Continue/Stop/...) bypass MIDI channel
		filtering entirely in Rack's own driver layer (verified in Rack/src/midi.cpp:53-57),
		so they arrive here regardless of the selected RX channel. Only the sub-types Dieter
		asked for are handled; everything else (MTC, Song Position/Select, Active Sensing,
		System Reset) is deliberately left untouched.
	*/
	inline void handleIncomingSystemMessage(const midi::Message &msg, bool &rxClockThisTick, bool &rxStartThisTick, bool &rxStopThisTick, bool &rxContinueThisTick)
	{
		uint8_t sub = msg.getChannel();	// lower nibble of the status byte - the message sub-type here, not a channel
		switch (sub)
		{
		case 0x0:	// SysEx start (0xF0)
		case 0x7:	// SysEx end (0xF7)
			if (getStateJson(SYSEX_PASSTHROUGH_JSON) != 0.f)
				midiOutput.sendMessage(msg);
			break;
		case 0x8:	// Clock
			setStateOutput(RX_CLOCK_OUTPUT, 10.f);
			rxClockThisTick = true;
			break;
		case 0xa:	// Start
			setStateOutput(RX_START_OUTPUT, 10.f);
			rxStartThisTick = true;
			break;
		case 0xb:	// Continue
			setStateOutput(RX_CONTINUE_OUTPUT, 10.f);
			rxContinueThisTick = true;
			break;
		case 0xc:	// Stop
			setStateOutput(RX_STOP_OUTPUT, 10.f);
			rxStopThisTick = true;
			break;
		default:
			// MTC Quarter Frame (0x1), Song Position (0x2), Song Select (0x3),
			// Active Sensing (0xe), System Reset (0xf) - out of scope, ignored.
			break;
		}
	}

	/**
		Sends a single-byte System Realtime message (Clock/Start/Continue/Stop). Matches
		dsp::MidiGenerator::setClock()/setStart()/setContinue()/setStop()'s exact byte layout
		(status 0xf, sub-type in the channel nibble) - status 0xf means midi::Output::
		sendMessage() never overwrites it with the configured TX channel (see Rack/src/
		midi.cpp's "Set channel if message is not a system MIDI message").
	*/
	inline void sendSystemRealtime(uint8_t sub, int64_t frame)
	{
		midi::Message m;
		m.setSize(1);
		m.setStatus(0xf);
		m.setChannel(sub);
		m.setFrame(frame);
		midiOutput.sendMessage(m);
	}

	inline void moduleProcess(const ProcessArgs &args)
	{
		if (styleChanged && widgetReady)
		{
			switch (int(getStateJson(STYLE_JSON)))
			{
			case STYLE_ORANGE: brightPanel->visible = false; darkPanel->visible = false; break;
			case STYLE_BRIGHT: brightPanel->visible = true;  darkPanel->visible = false; break;
			case STYLE_DARK:   brightPanel->visible = false; darkPanel->visible = true;  break;
			}
			styleChanged = false;
		}

		// --- Channel selection (knob, CV overrides if patched) ---
		int rxChannelIndex = getInputConnected(RX_CHANNEL_CV_INPUT)
			? (int) clamp(round(getStateInput(RX_CHANNEL_CV_INPUT) * 12.f), 0.f, 15.f)
			: (int) getStateParam(RX_CHANNEL_PARAM) - 1;
		int txChannelIndex = getInputConnected(TX_CHANNEL_CV_INPUT)
			? (int) clamp(round(getStateInput(TX_CHANNEL_CV_INPUT) * 12.f), 0.f, 15.f)
			: (int) getStateParam(TX_CHANNEL_PARAM) - 1;
		midiInput.channel = rxChannelIndex;
		midiOutput.channel = txChannelIndex;
		rxChannelDisplayValue = (float) (rxChannelIndex + 1);
		txChannelDisplayValue = (float) (txChannelIndex + 1);

		// --- sDLY: delay amount for the internal RX->TX passthrough path (patched infixes stay live) ---
		int sdlySamples = (int) clamp(getStateParam(SDLY_PARAM), 0.f, (float) SDLY_MAX_SAMPLES);
		sdlyDisplayValue = (float) sdlySamples;
		int64_t targetFrame = args.frame - (int64_t) sdlySamples;
		bool pbBipolar = getStateJson(PB_BIPOLAR_JSON) != 0.f;

		// --- Drain RX queue: notes (via MidiParser), CCs, Program Change, system messages ---
		bool pcReceivedThisTick = false;
		bool rxClockThisTick = false, rxStartThisTick = false, rxStopThisTick = false, rxContinueThisTick = false;
		midi::Message msg;
		while (midiInput.tryPop(&msg, args.frame))
		{
			uint8_t status = msg.getStatus();
			if (status == 0xf)
			{
				handleIncomingSystemMessage(msg, rxClockThisTick, rxStartThisTick, rxStopThisTick, rxContinueThisTick);
				continue;
			}
			noteParser.processMessage(msg);
			if (status == 0xb && msg.getSize() >= 3)
			{
				ccValues[msg.getNote()] = msg.getValue();
			}
			else if (status == 0xc)
			{
				lastReceivedProgram = msg.getNote();
				pcReceivedThisTick = true;
			}
			else if (status == 0xa)
			{
				// Poly key pressure: match against whichever poly slot noteParser currently
				// has this note assigned to (same note-to-slot match noteParser's own,
				// unused-here, aftertouches[] tracking uses internally).
				uint8_t note = msg.getNote();
				uint8_t val = msg.getValue();
				for (int slot = 0; slot < POLY_CHANNELS; slot++)
					if (noteParser.notes[slot] == note)
						rxPolyAftertouch[slot] = val;
			}
			else if (status == 0xd)
			{
				rxAftertouch = msg.getNote();	// 2-byte message: value lives in getNote()
			}
			else if (status == 0xe)
			{
				int16_t pw = ((int16_t) msg.getValue() << 7) | msg.getNote();
				rxPitchBend = pw - 8192;
			}
		}
		if (pcReceivedThisTick)
			setStateOutput(RX_PC_TRIGGER_OUTPUT, 10.f);
		setStateOutput(RX_BANK_OUTPUT,    ccValues[0] * 10.f / 127.f);
		setStateOutput(RX_PROGRAM_OUTPUT, lastReceivedProgram * 10.f / 127.f);

		// --- Note infix: RX (MidiParser) <-> TX (MidiGenerator), per poly slot ---
		bool txVoctConnected = getInputConnected(TX_VOCT_INPUT);
		bool txGateConnected = getInputConnected(TX_GATE_INPUT);
		bool txVelConnected  = getInputConnected(TX_VEL_INPUT);
		bool txPatConnected  = getInputConnected(TX_PAT_INPUT);
		int txVoctChannels = txVoctConnected ? inputs[TX_VOCT_INPUT].getChannels() : 0;
		int txGateChannels = txGateConnected ? inputs[TX_GATE_INPUT].getChannels() : 0;
		int txVelChannels  = txVelConnected  ? inputs[TX_VEL_INPUT].getChannels()  : 0;
		int txPatChannels  = txPatConnected  ? inputs[TX_PAT_INPUT].getChannels()  : 0;

		for (int slot = 0; slot < POLY_CHANNELS; slot++)
		{
			float rxVoct = noteParser.getPitchVoltage(slot);
			bool  rxGate = noteParser.gates[slot];
			uint8_t rxVelRaw = (uint8_t) noteParser.velocities[slot];
			float rxVelCv = rxVelRaw * 10.f / 127.f;
			float rxPatCv = rxPolyAftertouch[slot] * 10.f / 127.f;

			setStateOutPoly(RX_VOCT_OUTPUT, slot, rxVoct);
			setStateOutPoly(RX_GATE_OUTPUT, slot, rxGate ? 10.f : 0.f);
			setStateOutPoly(RX_VEL_OUTPUT,  slot, rxVelCv);
			setStateOutPoly(RX_PAT_OUTPUT,  slot, rxPatCv);

			// sDLY: internal passthrough candidates are always pushed into history (so it's
			// ready the moment something gets unpatched), delayed value only actually used
			// when that slot's infix isn't live-patched.
			voctHistory[slot].push(args.frame, rxVoct);
			gateHistory[slot].push(args.frame, rxGate ? 10.f : 0.f);
			velHistory[slot].push(args.frame, rxVelCv);
			patHistory[slot].push(args.frame, rxPatCv);

			float txVoct = (txVoctConnected && slot < txVoctChannels) ? OL_statePoly[TX_VOCT_INPUT * POLY_CHANNELS + slot] : voctHistory[slot].read(targetFrame);
			bool  txGate = (txGateConnected && slot < txGateChannels) ? (OL_statePoly[TX_GATE_INPUT * POLY_CHANNELS + slot] > 5.f) : (gateHistory[slot].read(targetFrame) > 5.f);
			float txVelCv = (txVelConnected && slot < txVelChannels) ? OL_statePoly[TX_VEL_INPUT * POLY_CHANNELS + slot] : velHistory[slot].read(targetFrame);
			float txPatCv = (txPatConnected && slot < txPatChannels) ? OL_statePoly[TX_PAT_INPUT * POLY_CHANNELS + slot] : patHistory[slot].read(targetFrame);

			int midiNote = (int) clamp((float) (60 + 12 * octave(txVoct) + note(txVoct)), 0.f, 127.f);
			uint8_t midiVel = (uint8_t) clamp(std::round(txVelCv / 10.f * 127.f), 0.f, 127.f);
			uint8_t midiPat = (uint8_t) clamp(std::round(txPatCv / 10.f * 127.f), 0.f, 127.f);

			noteGenerator.setVelocity((int8_t) midiVel, slot);
			noteGenerator.setNoteGate((int8_t) midiNote, txGate, slot);
			noteGenerator.setKeyPressure((int8_t) midiPat, slot);
		}
		setOutPolyChannels(RX_VOCT_OUTPUT, POLY_CHANNELS);
		setOutPolyChannels(RX_GATE_OUTPUT, POLY_CHANNELS);
		setOutPolyChannels(RX_VEL_OUTPUT,  POLY_CHANNELS);
		setOutPolyChannels(RX_PAT_OUTPUT,  POLY_CHANNELS);

		// --- Aftertouch (mono) / Pitch Bend infix ---
		float rxAtCv = rxAftertouch * 10.f / 127.f;
		float rxPbCv = pbRawToCv(rxPitchBend, pbBipolar);
		setStateOutput(RX_AT_OUTPUT, rxAtCv);
		setStateOutput(RX_PB_OUTPUT, rxPbCv);
		atHistory.push(args.frame, rxAtCv);
		pbHistory.push(args.frame, rxPbCv);

		bool txAtConnected = getInputConnected(TX_AT_INPUT);
		bool txPbConnected = getInputConnected(TX_PB_INPUT);
		float txAtCv = txAtConnected ? getStateInput(TX_AT_INPUT) : atHistory.read(targetFrame);
		float txPbCv = txPbConnected ? getStateInput(TX_PB_INPUT) : pbHistory.read(targetFrame);
		uint8_t midiAt = (uint8_t) clamp(std::round(txAtCv / 10.f * 127.f), 0.f, 127.f);
		noteGenerator.setChannelPressure((int8_t) midiAt);
		noteGenerator.setPitchWheel(pbCvToRaw(txPbCv, pbBipolar));

		// --- CC infix: RX side (raw receive, always updates the output taps) ---
		// History pushed here (every throttled ~1kHz tick) rather than down in the 200Hz
		// rate-limited send block below, so sDLY's delay lookup has ~1ms resolution instead
		// of the send loop's coarser ~5ms - the send loop still only *reads* from history.
		for (int cc = 0; cc < 128; cc++)
		{
			float rxCcCv = ccValues[cc] * 10.f / 127.f;
			setStateOutPoly(RX_CC_OUTPUT + cc / 16, cc % 16, rxCcCv);
			ccHistory[cc].push(args.frame, rxCcCv);
		}
		for (int bank = 0; bank < NUM_CC_BANKS; bank++)
			setOutPolyChannels(RX_CC_OUTPUT + bank, POLY_CHANNELS);

		// --- CC infix: TX side (send), 1:1 CV2CC's model - grid lock light, rate limiter, change detection ---
		if (inChangeParam(GRID_LOCK_PARAM))
			gridLocked = !gridLocked;
		setStateLight(GRID_LOCK_LIGHT, gridLocked ? 0.f : 255.f);

		bool forceFlush = changeInput(FORCE_INPUT) || (changeParam(FLUSH_PARAM) && getStateParam(FLUSH_PARAM) > 0.f);
		if (forceFlush)
		{
			for (int cc = 0; cc < 128; cc++)
				lastSentValues[cc] = 0xFF;
			needsInitialSeed = false;
		}

		float elapsed = args.sampleTime * float(samplesSkipped + 1);
		if (rateLimiterTimer.process(elapsed) >= RATE_LIMITER_PERIOD)
		{
			rateLimiterTimer.time -= RATE_LIMITER_PERIOD;

			bool suppressInitialFlush = needsInitialSeed && getStateJson(FLUSH_ON_START_JSON) == 0.f;
			needsInitialSeed = false;

			for (int bank = 0; bank < NUM_CC_BANKS; bank++)
			{
				bool connected = getInputConnected(TX_CC_INPUT + bank);
				int channels = connected ? inputs[TX_CC_INPUT + bank].getChannels() : 0;
				for (int idx = 0; idx < 16; idx++)
				{
					int cc = bank * 16 + idx;

					// Re-enabling a CC does NOT force an immediate resend/activity-flash
					// (Dieter: "es soll einfach nicht aufblitzen wenn man editiert") - it
					// just resumes normal change-detection against whatever was last sent
					// before it was disabled.
					ccWasEnabled[cc] = ccEnabled[cc];

					ccActivity[cc] = std::max(0.f, ccActivity[cc] - CC_ACTIVITY_DECAY);

					float raw = (idx < channels) ? OL_statePoly[(TX_CC_INPUT + bank) * POLY_CHANNELS + idx] : ccHistory[cc].read(targetFrame);
					ccCvValue[cc] = raw;
					uint8_t value = (uint8_t) clamp(std::round(raw / 10.f * 127.f), 0.f, 127.f);

					if (suppressInitialFlush)
					{
						lastSentValues[cc] = value;
						continue;
					}

					bool changed = (value != lastSentValues[cc]);

					if (!ccEnabled[cc])
					{
						if (changed)
						{
							lastSentValues[cc] = value;
							ccActivity[cc] = 1.f;
						}
						continue;
					}

					if (!changed)
						continue;
					lastSentValues[cc] = value;
					ccActivity[cc] = 1.f;

					midi::Message m;
					m.setStatus(0xb);
					m.setNote(cc);
					m.setValue(value);
					m.setFrame(args.frame);
					midiOutput.sendMessage(m);
				}
			}
		}

		// --- Program Change / Bank infix (TX side) ---
		float rxBankCv = ccValues[0] * 10.f / 127.f;
		float rxProgramCv = lastReceivedProgram * 10.f / 127.f;
		bankHistory.push(args.frame, rxBankCv);
		programHistory.push(args.frame, rxProgramCv);
		if (pcReceivedThisTick)
			pcDelay.push(args.frame);

		bool pcTxConnected = getInputConnected(TX_PC_TRIGGER_INPUT);
		bool firePc = false;
		if (pcTxConnected)
			firePc = changeInput(TX_PC_TRIGGER_INPUT);
		else
			while (pcDelay.pop(args.frame, sdlySamples))
				firePc = true;

		if (firePc)
		{
			int bank = getInputConnected(TX_BANK_INPUT)
				? (int) clamp(std::round(getStateInput(TX_BANK_INPUT) / 10.f * 127.f), 0.f, 127.f)
				: (int) clamp(std::round(bankHistory.read(targetFrame) / 10.f * 127.f), 0.f, 127.f);
			int program = getInputConnected(TX_PROGRAM_INPUT)
				? (int) clamp(std::round(getStateInput(TX_PROGRAM_INPUT) / 10.f * 127.f), 0.f, 127.f)
				: (int) clamp(std::round(programHistory.read(targetFrame) / 10.f * 127.f), 0.f, 127.f);

			midi::Message bankMsg;
			bankMsg.setStatus(0xb);
			bankMsg.setNote(0);
			bankMsg.setValue((uint8_t) bank);
			bankMsg.setFrame(args.frame);
			midiOutput.sendMessage(bankMsg);

			midi::Message pcMsg;
			pcMsg.setSize(2);
			pcMsg.setStatus(0xc);
			pcMsg.setNote((uint8_t) program);
			pcMsg.setFrame(args.frame);
			midiOutput.sendMessage(pcMsg);
		}

		// --- Clock / Start / Stop / Continue infix (TX side) ---
		bool clockOn = getStateJson(CLOCK_PASSTHROUGH_JSON) != 0.f;
		bool transportOn = getStateJson(TRANSPORT_PASSTHROUGH_JSON) != 0.f;

		if (clockOn && rxClockThisTick) clockDelay.push(args.frame);
		if (transportOn && rxStartThisTick) startDelay.push(args.frame);
		if (transportOn && rxContinueThisTick) continueDelay.push(args.frame);
		if (transportOn && rxStopThisTick) stopDelay.push(args.frame);

		bool txClockConnected = getInputConnected(TX_CLOCK_INPUT);
		bool sendClock = txClockConnected ? changeInput(TX_CLOCK_INPUT) : false;
		if (!txClockConnected)
			while (clockDelay.pop(args.frame, sdlySamples))
				sendClock = true;
		if (sendClock)
			sendSystemRealtime(0x8, args.frame);

		bool txStartConnected = getInputConnected(TX_START_INPUT);
		bool sendStart = txStartConnected ? changeInput(TX_START_INPUT) : false;
		if (!txStartConnected)
			while (startDelay.pop(args.frame, sdlySamples))
				sendStart = true;
		if (sendStart)
			sendSystemRealtime(0xa, args.frame);

		bool txContinueConnected = getInputConnected(TX_CONTINUE_INPUT);
		bool sendContinue = txContinueConnected ? changeInput(TX_CONTINUE_INPUT) : false;
		if (!txContinueConnected)
			while (continueDelay.pop(args.frame, sdlySamples))
				sendContinue = true;
		if (sendContinue)
			sendSystemRealtime(0xb, args.frame);

		bool txStopConnected = getInputConnected(TX_STOP_INPUT);
		bool sendStop = txStopConnected ? changeInput(TX_STOP_INPUT) : false;
		if (!txStopConnected)
			while (stopDelay.pop(args.frame, sdlySamples))
				sendStop = true;
		if (sendStop)
			sendSystemRealtime(0xc, args.frame);
	}

	inline void moduleProcessState() {}
	inline void moduleReflectChanges() {}
};

// ********************************************************************************************************************************
/*
	Widget implementation
*/

/**
	Main Module Widget

	v1 panel: plain functional layout, no custom artwork yet (see res/MidiBusWork.svg). Wide
	panel (RX jacks left half, TX jacks right half, CC grid centered at the bottom) since
	MidiBus needs far more I/O than any other OrangeLine module so far.
*/
struct MidiBusWidget : ModuleWidget
{
	MidiBusWidget(MidiBus *module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/MidiBusOrange.svg")));

		if (module)
		{
			SvgPanel *brightPanel = new SvgPanel();
			brightPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/MidiBusBright.svg")));
			brightPanel->visible = false;
			module->brightPanel = brightPanel;
			addChild(brightPanel);
			SvgPanel *darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/MidiBusDark.svg")));
			darkPanel->visible = false;
			module->darkPanel = darkPanel;
			addChild(darkPanel);
		}

		// Coordinates extracted directly from res/MidibusWork.svg's Controls/TextWork layers
		// (proper path-command-aware parser - see project memory for why the naive regex
		// approach corrupted the ring jacks by misreading SVG arc-command parameters).

		MidiDisplay *rxDisplay = createWidget<MidiDisplay>(calculateCoordinates(2.028f, 10.411f, 0.f));
		rxDisplay->box.size = mm2px(Vec(43.688f, 18.8f));
		rxDisplay->setMidiPort(module ? &module->midiInput : NULL);
		rxDisplay->removeChild(rxDisplay->channelChoice);
		delete rxDisplay->channelChoice;
		rxDisplay->channelChoice = nullptr;
		addChild(rxDisplay);

		MidiDisplay *txDisplay = createWidget<MidiDisplay>(calculateCoordinates(75.942f, 10.412f, 0.f));
		txDisplay->box.size = mm2px(Vec(43.688f, 18.8f));
		txDisplay->setMidiPort(module ? &module->midiOutput : NULL);
		txDisplay->removeChild(txDisplay->channelChoice);
		delete txDisplay->channelChoice;
		txDisplay->channelChoice = nullptr;
		addChild(txDisplay);

		// CC grid sits right below the TX MIDI Display (TX-side only, mirrors CV2CC's own grid).
		CCGridWidget *grid = CCGridWidget::create(calculateCoordinates(76.207f, 33.043f, 0.f), mm2px(Vec(43.688f, 22.0f)),
			module ? &module->ccEnabled[0] : NULL, module ? &module->ccActivity[0] : NULL, module ? &module->ccCvValue[0] : NULL,
			module ? &module->gridLocked : NULL);
		addChild(grid);

		// Channel/delay block: three knob+display columns (CH/IN, CH/OUT, sDLY) on the left.
		addParam(createParamCentered<RoundSmallBlackKnob>(calculateCoordinates(8.185f, 50.293f, 0.f), module, RX_CHANNEL_PARAM));
		if (module)
		{
			module->rxChannelBuffer[0] = '\0';
			NumberWidget *rxNum = NumberWidget::create(calculateCoordinates(5.7f, 42.4f, 0.f), module, &module->rxChannelDisplayValue, 1.f, "%2.0f", module->rxChannelBuffer, 2);
			rxNum->pStyle = &(module->OL_state[STYLE_JSON]);
			rxNum->fontSize = 16.f;
			addChild(rxNum);
		}

		addParam(createParamCentered<RoundSmallBlackKnob>(calculateCoordinates(25.400f, 50.293f, 0.f), module, TX_CHANNEL_PARAM));
		if (module)
		{
			module->txChannelBuffer[0] = '\0';
			NumberWidget *txNum = NumberWidget::create(calculateCoordinates(22.9f, 42.4f, 0.f), module, &module->txChannelDisplayValue, 1.f, "%2.0f", module->txChannelBuffer, 2);
			txNum->pStyle = &(module->OL_state[STYLE_JSON]);
			txNum->fontSize = 16.f;
			addChild(txNum);
		}

		addParam(createParamCentered<RoundSmallBlackKnob>(calculateCoordinates(39.624f, 50.293f, 0.f), module, SDLY_PARAM));
		if (module)
		{
			module->sdlyBuffer[0] = '\0';
			NumberWidget *sdlyNum = NumberWidget::create(calculateCoordinates(33.7f, 42.4f, 0.f), module, &module->sdlyDisplayValue, 1.f, "%3.0f", module->sdlyBuffer, 3);
			sdlyNum->pStyle = &(module->OL_state[STYLE_JSON]);
			sdlyNum->fontSize = 16.f;
			addChild(sdlyNum);
		}

		// FLUSH: button + trigger jack, top center.
		addParam(createParamCentered<LEDButton>(calculateCoordinates(55.118f, 15.749f, 0.f), module, FLUSH_PARAM));
		addInput(createInputCentered<PJ301MPort>(calculateCoordinates(66.802f, 15.749f, 0.f), module, FORCE_INPUT));

		// Center Infix block: two columns (x=55.118 RX/left, x=66.802 TX/right), nine rows.
		static const float INFIX_RX_X = 55.118f;
		static const float INFIX_TX_X = 66.802f;
		int infixRxIds[9] = {
			RX_VOCT_OUTPUT, RX_GATE_OUTPUT, RX_VEL_OUTPUT, RX_AT_OUTPUT, RX_PAT_OUTPUT, RX_PB_OUTPUT,
			RX_PC_TRIGGER_OUTPUT, RX_BANK_OUTPUT, RX_PROGRAM_OUTPUT
		};
		int infixTxIds[9] = {
			TX_VOCT_INPUT, TX_GATE_INPUT, TX_VEL_INPUT, TX_AT_INPUT, TX_PAT_INPUT, TX_PB_INPUT,
			TX_PC_TRIGGER_INPUT, TX_BANK_INPUT, TX_PROGRAM_INPUT
		};
		float infixRowY[9] = { 33.286f, 43.181f, 53.100f, 63.004f, 72.910f, 82.816f, 94.754f, 104.491f, 114.312f };
		for (int i = 0; i < 9; i++)
		{
			addOutput(createOutputCentered<PJ301MPort>(calculateCoordinates(INFIX_RX_X, infixRowY[i], 0.f), module, infixRxIds[i]));
			addInput(createInputCentered<PJ301MPort>(calculateCoordinates(INFIX_TX_X, infixRowY[i], 0.f), module, infixTxIds[i]));
		}

		// CV OUT ring (RX CC bank outputs, left, clockwise from top) + channel CV jack at center.
		static const float ringOutX[8] = { 25.400f, 36.830f, 41.402f, 36.830f, 25.400f, 13.970f, 9.398f, 13.970f };
		static const float ringOutY[8] = { 66.803f, 71.375f, 82.805f, 94.235f, 98.807f, 94.235f, 82.805f, 71.375f };
		for (int n = 0; n < NUM_CC_BANKS; n++)
			addOutput(createOutputCentered<PJ301MPort>(calculateCoordinates(ringOutX[n], ringOutY[n], 0.f), module, RX_CC_OUTPUT + n));
		addInput(createInputCentered<PJ301MPort>(calculateCoordinates(25.400f, 82.805f, 0.f), module, RX_CHANNEL_CV_INPUT));

		// CV IN ring (TX CC bank inputs, right, mirrored) + channel CV jack at center.
		static const float ringInX[8] = { 97.282f, 108.712f, 113.284f, 108.712f, 97.282f, 85.852f, 81.280f, 86.106f };
		static const float ringInY[8] = { 66.953f, 71.525f, 82.955f, 94.385f, 98.957f, 94.385f, 82.955f, 71.779f };
		for (int n = 0; n < NUM_CC_BANKS; n++)
			addInput(createInputCentered<PJ301MPort>(calculateCoordinates(ringInX[n], ringInY[n], 0.f), module, TX_CC_INPUT + n));
		addInput(createInputCentered<PJ301MPort>(calculateCoordinates(96.520f, 82.805f, 0.f), module, TX_CHANNEL_CV_INPUT));

		// CLK/START/STOP/CONT rows, below each ring.
		static const float transportY = 109.844f;
		static const float rxTransportX[4] = { 9.376f, 20.044f, 30.712f, 41.380f };
		static const float txTransportX[4] = { 81.280f, 91.948f, 102.616f, 113.284f };
		int rxTransportIds[4] = { RX_CLOCK_OUTPUT, RX_START_OUTPUT, RX_STOP_OUTPUT, RX_CONTINUE_OUTPUT };
		int txTransportIds[4] = { TX_CLOCK_INPUT, TX_START_INPUT, TX_STOP_INPUT, TX_CONTINUE_INPUT };
		for (int i = 0; i < 4; i++)
		{
			addOutput(createOutputCentered<PJ301MPort>(calculateCoordinates(rxTransportX[i], transportY, 0.f), module, rxTransportIds[i]));
			addInput(createInputCentered<PJ301MPort>(calculateCoordinates(txTransportX[i], transportY, 0.f), module, txTransportIds[i]));
		}

		// Grid lock, co-located button+light (matches every other module's convention).
		addParam(createParamCentered<VCVLatch>(calculateCoordinates(117.855f, 61.215f, 0.f), module, GRID_LOCK_PARAM));
		addChild(createLightCentered<LargeLight<RedLight>>(calculateCoordinates(117.855f, 61.215f, 0.f), module, GRID_LOCK_LIGHT));

		if (module)
			module->widgetReady = true;
	}

	struct MidiBusStyleItem : MenuItem
	{
		MidiBus *module;
		int style;
		void onAction(const event::Action &e) override
		{
			module->OL_setOutState(STYLE_JSON, float(style));
			module->styleChanged = true;
		}
		void step() override
		{
			if (module)
				rightText = (module != nullptr && module->OL_state[STYLE_JSON] == style) ? "✔" : "";
		}
	};

	struct MidiBusFlushOnStartItem : MenuItem
	{
		MidiBus *module;
		void onAction(const event::Action &e) override
		{
			module->setStateJson(FLUSH_ON_START_JSON, module->getStateJson(FLUSH_ON_START_JSON) == 0.f ? 1.f : 0.f);
		}
		void step() override
		{
			if (module)
				rightText = (module->getStateJson(FLUSH_ON_START_JSON) != 0.f) ? "✔" : "";
		}
	};

	struct MidiBusPassthroughItem : MenuItem
	{
		MidiBus *module;
		int jsonId;
		void onAction(const event::Action &e) override
		{
			module->setStateJson(jsonId, module->getStateJson(jsonId) == 0.f ? 1.f : 0.f);
		}
		void step() override
		{
			if (module)
				rightText = (module->getStateJson(jsonId) != 0.f) ? "✔" : "";
		}
	};

	void appendContextMenu(Menu *menu) override
	{
		MenuLabel *spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		MidiBus *module = dynamic_cast<MidiBus *>(this->module);
		assert(module);

		MenuLabel *styleLabel = new MenuLabel();
		styleLabel->text = "Style";
		menu->addChild(styleLabel);

		MidiBusStyleItem *style1Item = new MidiBusStyleItem();
		style1Item->text = "Orange";
		style1Item->module = module;
		style1Item->style = STYLE_ORANGE;
		menu->addChild(style1Item);

		MidiBusStyleItem *style2Item = new MidiBusStyleItem();
		style2Item->text = "Bright";
		style2Item->module = module;
		style2Item->style = STYLE_BRIGHT;
		menu->addChild(style2Item);

		MidiBusStyleItem *style3Item = new MidiBusStyleItem();
		style3Item->text = "Dark";
		style3Item->module = module;
		style3Item->style = STYLE_DARK;
		menu->addChild(style3Item);

		spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		MidiBusFlushOnStartItem *flushOnStartItem = new MidiBusFlushOnStartItem();
		flushOnStartItem->module = module;
		flushOnStartItem->text = "Flush On Start";
		menu->addChild(flushOnStartItem);

		MidiBusPassthroughItem *sysexItem = new MidiBusPassthroughItem();
		sysexItem->module = module;
		sysexItem->jsonId = SYSEX_PASSTHROUGH_JSON;
		sysexItem->text = "Pass Through SysEx";
		menu->addChild(sysexItem);

		MidiBusPassthroughItem *clockItem = new MidiBusPassthroughItem();
		clockItem->module = module;
		clockItem->jsonId = CLOCK_PASSTHROUGH_JSON;
		clockItem->text = "Pass Through Clock";
		menu->addChild(clockItem);

		MidiBusPassthroughItem *transportItem = new MidiBusPassthroughItem();
		transportItem->module = module;
		transportItem->jsonId = TRANSPORT_PASSTHROUGH_JSON;
		transportItem->text = "Pass Through Transport";
		menu->addChild(transportItem);

		MidiBusPassthroughItem *pbBipolarItem = new MidiBusPassthroughItem();
		pbBipolarItem->module = module;
		pbBipolarItem->jsonId = PB_BIPOLAR_JSON;
		pbBipolarItem->text = "Bipolar Pitch Bend CV";
		menu->addChild(pbBipolarItem);
	}
};

Model *modelMidiBus = createModel<MidiBus, MidiBusWidget>("MidiBus");
