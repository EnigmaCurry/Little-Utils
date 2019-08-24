#include "plugin.hpp"
#include "window.hpp" // windowIsModPressed
#include "osdialog.h"
#include <GLFW/glfw3.h> // key codes
#include <algorithm> // std::min, std::swap
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h" // for reading wav files

#include <iostream>

//TODO: preiodic interp right click menu
//TODO: reinitialize buffer with onInitialize()
//TODO: prevent audio clicking at the last sample
//TODO: drawing when size > width in pixels
//TODO: add 'reset buffer' right click menu
//TODO: undo history? hard?

struct Array : Module {
	enum ParamIds {
		PHASE_RANGE_PARAM,
		OUTPUT_RANGE_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		PHASE_INPUT,
		REC_SIGNAL_INPUT,
		REC_PHASE_INPUT,
		REC_ENABLE_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		STEP_OUTPUT,
		INTERP_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		REC_LIGHT,
		NUM_LIGHTS
	};

	enum InterpBoundaryMode {
		INTERP_CONSTANT,
		INTERP_MIRROR,
		INTERP_PERIODIC,
		NUM_INTERP_MODES
	};

	float phases[MAX_POLY_CHANNELS];
	int nChannels = 1;
	float recPhase = 0.f;
	dsp::SchmittTrigger recTrigger;
	std::vector<float> buffer;
	std::string lastLoadedPath;
	bool enableEditing = true;
	InterpBoundaryMode boundaryMode = INTERP_PERIODIC;

	void initBuffer() {
		buffer.clear();
		int default_steps = 10;
		for(int i = 0; i < default_steps; i++) {
			buffer.push_back(i / (default_steps - 1.f));
		}
	}

	Array() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(Array::OUTPUT_RANGE_PARAM, 0, 2, 0, "Recording and output range");
		configParam(Array::PHASE_RANGE_PARAM, 0, 2, 2, "Position CV range");
		for(int i = 0; i < MAX_POLY_CHANNELS; i++) phases[i] = 0.f;
		initBuffer();
	}

	void process(const ProcessArgs &args) override;

	void resizeBuffer(unsigned int newSize) {
		buffer.resize(newSize, 0.f);
	}

	void loadSample(std::string path);

	// TODO: if array is large enough (how large?) encode as base64?
	// see https://stackoverflow.com/questions/45508360/quickest-way-to-encode-vector-of-floats-into-hex-or-base64binary
	// also: rack::string::to/fromBase64

	json_t *dataToJson() override {
		json_t *root = json_object();
		json_object_set_new(root, "enableEditing", json_boolean(enableEditing));
		json_object_set_new(root, "boundaryMode", json_real(boundaryMode));
		json_t *arr = json_array();
		for(float x : buffer) {
			json_array_append_new(arr, json_real(x));
		}
		json_object_set(root, "arrayData", arr);
		return root;
	}

	void dataFromJson(json_t *root) override {
		json_t *enableEditing_J = json_object_get(root, "enableEditing");
		json_t *boundaryMode_J = json_object_get(root, "boundaryMode");
		json_t *arr = json_object_get(root, "arrayData");

		if(enableEditing_J) {
			enableEditing = json_boolean_value(enableEditing_J);
		}
		if(boundaryMode_J) {
			int bm = int(json_real_value(boundaryMode_J));
			if(bm < NUM_INTERP_MODES) {
				boundaryMode = static_cast<InterpBoundaryMode>(bm);
			}
		}

		if(arr) {
			buffer.clear();
			size_t i;
			json_t *val;
			json_array_foreach(arr, i, val) {
				buffer.push_back(json_real_value(val));
			}
		}
	}

	void onReset() override {
		boundaryMode = INTERP_PERIODIC;
		enableEditing = true;
		initBuffer();
	}
};

void Array::loadSample(std::string path) {
	unsigned int channels, sampleRate;
	drwav_uint64 totalPCMFrameCount;
	float* pSampleData = drwav_open_file_and_read_pcm_frames_f32(path.c_str(), &channels, &sampleRate, &totalPCMFrameCount);

	if (pSampleData != NULL) {
		int newSize = std::min(totalPCMFrameCount, buffer.size());
		buffer.clear();
		buffer.reserve(newSize);
		for(unsigned int i = 0; i < newSize * channels; i = i + channels) {
			float s = pSampleData[i];
			if(channels == 2) {
				s = (s + pSampleData[i + 1]) * 0.5f; // mix stereo channels, good idea?
			}
			buffer.push_back((s + 1.f) * 0.5f); // rescale from -1 .. 1 to 0..1
		}
		enableEditing = false;
	}

	drwav_free(pSampleData);
}

void Array::process(const ProcessArgs &args) {
	float deltaTime = args.sampleTime;

	float phaseMin, phaseMax;
	float prange = params[PHASE_RANGE_PARAM].getValue();
	if(prange > 1.5f) {
		phaseMin = 0.f;
		phaseMax = 10.f;
	} else if(prange > 0.5f) {
		phaseMin = -5.f;
		phaseMax =  5.f;
	} else {
		phaseMin = -10.f;
		phaseMax =  10.f;
	}

	int size = buffer.size();


	float inOutMin, inOutMax;
	float orange = params[OUTPUT_RANGE_PARAM].getValue();
	if(orange > 1.5f) {
		inOutMin = 0.f;
		inOutMax = 10.f;
	} else if(orange > 0.5f) {
		inOutMin = -5.f;
		inOutMax =  5.f;
	} else {
		inOutMin = -10.f;
		inOutMax =  10.f;
	}

	// recording
	recPhase = clamp(rescale(inputs[REC_PHASE_INPUT].getVoltage(), phaseMin, phaseMax, 0.f, 1.f), 0.f, 1.f);
	int ri = int(recPhase * size);
	recTrigger.process(rescale(inputs[REC_ENABLE_INPUT].getVoltage(), 0.1f, 2.f, 0.f, 1.f));
	bool rec = recTrigger.isHigh();
	if(rec) {
		buffer[ri] = clamp(rescale(inputs[REC_SIGNAL_INPUT].getVoltage(), inOutMin, inOutMax, 0.f, 1.f), 0.f, 1.f);
	}
	lights[REC_LIGHT].setSmoothBrightness(rec, deltaTime);

	nChannels = inputs[PHASE_INPUT].getChannels();
	outputs[STEP_OUTPUT].setChannels(nChannels);
	outputs[INTERP_OUTPUT].setChannels(nChannels);
	for(int chan = 0; chan < nChannels; chan++) {
		float phase = clamp(rescale(inputs[PHASE_INPUT].getVoltage(chan), phaseMin, phaseMax, 0.f, 1.f), 0.f, 1.f);
		phases[chan] = phase;
		// direct output
		int i = clamp(int(phase * size), 0, size - 1);
		outputs[STEP_OUTPUT].setVoltage(rescale(buffer[i], 0.f, 1.f, inOutMin, inOutMax), chan);

		// interpolated output, based on tabread4_tilde_perform() in
		// https://github.com/pure-data/pure-data/blob/master/src/d_array.c
		int ia, ib, ic, id;
		switch(boundaryMode) {
			case INTERP_CONSTANT:
				{
					//TODO: make symmetric (based on range polarity (?)) -- possible?
					ia = clamp(i - 1, 0, size - 1);
					ib = clamp(i + 0, 0, size - 1);
					ic = clamp(i + 1, 0, size - 1);
					id = clamp(i + 2, 0, size - 1);
					break;
				}
			case INTERP_MIRROR:
				{
					ia = i < 1 ? 1 : i - 1;
					ib = i;
					ic = i + 1 < size ? i + 1 : size - 1;
					id = i + 2 < size ? i + 2 : 2*size - (i + 3);
					break;
				}
			case INTERP_PERIODIC:
			default:
				{
					ia = (i - 1 + size) % size;
					ib = (i + 0) % size;
					ic = (i + 1) % size;
					id = (i + 2) % size;
					break;
				}
		}
		float a = buffer[ia];
		float b = buffer[ib];
		float c = buffer[ic];
		float d = buffer[id];

		float frac = phase * size - i; // fractional part of phase
		float y = b + frac * (
				c - b - 0.1666667f * (1.f - frac) * (
					(d - a - 3.f * (c - b)) * frac + (d + 2.f * a - 3.f * b)
					)
				);
		outputs[INTERP_OUTPUT].setVoltage(rescale(y, 0.f, 1.f, inOutMin, inOutMax), chan);
	}

}

struct ArrayDisplay : OpaqueWidget {
	Array *module;
	Vec dragPosition;
	bool dragging = false;

	ArrayDisplay(Array *module): OpaqueWidget() {
		this->module = module;
		box.size = Vec(230, 205);
	}

	void draw(const DrawArgs &args) override {
		OpaqueWidget::draw(args);
		const auto vg = args.vg;

		if(module) {
			// show phase
			int nc = module->nChannels;
			int alpha = int(0xff * rescale(1.0f/nc, 0.f, 1.f, 0.5f, 1.0f));
			for(int c = 0; c < nc; c++) {
				float px =  module->phases[c] * box.size.x;
				nvgBeginPath(vg);
				nvgStrokeWidth(vg, 2.f);
				nvgStrokeColor(vg, nvgRGBA(0x23, 0x23, 0x87, alpha));
				nvgMoveTo(vg, px, 0);
				nvgLineTo(vg, px, box.size.y);
				nvgStroke(vg);
			}

			// phase of recording
			if(module->inputs[Array::REC_PHASE_INPUT].isConnected()) {
				float rpx = module->recPhase * box.size.x;
				nvgBeginPath(vg);
				nvgStrokeWidth(vg, 2.f);
				nvgStrokeColor(vg, nvgRGB(0x87, 0x23, 0x23));
				nvgMoveTo(vg, rpx, 0);
				nvgLineTo(vg, rpx, box.size.y);
				nvgStroke(vg);
			}

			//TODO: optimize drawing when buffer size > pixels
			//TODO: move this to be before phase drawing
			int s = module->buffer.size();
			float w = box.size.x * 1.f / s;
			nvgBeginPath(vg);
			for(int i = 0; i < s; i++) {
				float x1 = i * w;
				float x2 = (i + 1) * w;
				float y = (1.f - module->buffer[i]) * box.size.y;

				if(i == 0) nvgMoveTo(vg, x1, y);
				else nvgLineTo(vg, x1, y);

				nvgLineTo(vg, x2, y);
			}
			nvgStrokeWidth(vg, 2.f);
			nvgStrokeColor(vg, nvgRGB(0x0, 0x0, 0x0));
			nvgStroke(vg);
		}

		nvgBeginPath(vg);
		nvgStrokeColor(vg, nvgRGB(0x0, 0x0, 0x0));
		nvgStrokeWidth(vg, 2.f);
		nvgRect(vg, 0, 0, box.size.x, box.size.y);
		nvgStroke(vg);

	}

	void onButton(const event::Button &e) override {
		if(e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS
				&& module->enableEditing) {
			e.consume(this);
			dragPosition = e.pos;
		}
	}

	void onDragStart(const event::DragStart &e) override {
		OpaqueWidget::onDragStart(e);
		dragging = true;
	}

	void onDragEnd(const event::DragEnd &e) override {
		OpaqueWidget::onDragEnd(e);
		dragging = false;
	}

	void onDragMove(const event::DragMove &e) override {
		OpaqueWidget::onDragMove(e);
		if(!module->enableEditing) return;
		Vec dragPosition_old = dragPosition;
		dragPosition = dragPosition.plus(e.mouseDelta); //TODO: check

		// int() rounds down, so the upper limit of rescale is buffer.size() without -1.
		int s = module->buffer.size();
		int boxx = box.size.x;
		int boxy = box.size.y;
		int i1 = clamp(int(rescale(dragPosition_old.x, 0, boxx, 0, s)), 0, s - 1);
		int i2 = clamp(int(rescale(dragPosition.x,     0, boxx, 0, s)), 0, s - 1);

		if(abs(i1 - i2) < 2) {
			float y = clamp(rescale(dragPosition.y, 0, boxy, 1.f, 0.f), 0.f, 1.f);
			module->buffer[i2] = y;
		} else {
			float y1 = clamp(rescale(dragPosition_old.y, 0, boxy, 1.f, 0.f), 0.f, 1.f);
			float y2 = clamp(rescale(dragPosition.y,     0, boxy, 1.f, 0.f), 0.f, 1.f);
			if(i2 < i1) {
				std::swap(i1, i2);
				std::swap(y1, y2);
			}
			for(int i = i1; i <= i2; i++) {
				float y = y1 + rescale(i, i1, i2, 0.f, 1.0f) * (y2 - y1);
				module->buffer[i] = y;
			}
		}
	}

	void step() override {
		OpaqueWidget::step();
	}
};


// TextField that only allows inputting numbers
struct NumberTextField : TextField {
	int maxCharacters = 6;
	Array *module;
	std::string validText = "1";

	NumberTextField(Array *m) : TextField() {
		module = m;
		validText = string::f("%u", module ? module->buffer.size() : 1);
		text = validText;
	};

	bool isNumber(const std::string& s) {
		// shamelessly copypasted from https://stackoverflow.com/questions/4654636/how-to-determine-if-a-string-is-a-number-with-c
		std::string::const_iterator it = s.begin();
		while (it != s.end() && std::isdigit(*it)) ++it;
		return !s.empty() && it == s.end();
	}

	void onAction(const event::Action &e) override {
		if(text.size() > 0) {
			int n = stoi(text); // text should always contain only digits
			if(n > 0) {
				validText = string::f("%u", n);
				module->resizeBuffer(n);
			}
		}
		text = validText;
		//if(gFocusedWidget == this) gFocusedWidget = NULL;
		if(APP->event->selectedWidget == this) APP->event->selectedWidget = NULL; //TODO: replace with onSelect / onDeselect (?) -- at least emit onDeselect, compare to TextField (?)
		e.consume(this);
	}

	void onSelectKey(const event::SelectKey &e) override {
		//TODO: compare this to onSelectKey in Little-Utils/src/Widgets.cpp
		if(e.key == GLFW_KEY_V && (e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL) {
			// prevent pasting too long text
			int pasteLength = maxCharacters - TextField::text.size();
			if(pasteLength > 0) {
				std::string newText(glfwGetClipboardString(APP->window->win));
				if(newText.size() > pasteLength) newText.erase(pasteLength);
				if(isNumber(newText)) insertText(newText);
			}
			e.consume(this);

		} else if(e.key == GLFW_KEY_ESCAPE) {
			// same as pressing enter
			event::Action eAction;
			onAction(eAction);
			event::Deselect eDeselect;
			onDeselect(eDeselect);
			APP->event->selectedWidget = NULL;
			e.consume(this);

		} else {
			TextField::onSelectKey(e);
		}
	}

	void onSelectText(const event::SelectText &e) override {
		// assuming GLFW number keys are contiguous
		if(text.size() < maxCharacters
				&& GLFW_KEY_0  <= e.codepoint
				&& e.codepoint <= GLFW_KEY_9) {
			TextField::onSelectText(e);
		}
	}
	void step() override {
		TextField::step();
		// eh, kinda hacky - is there any way to do this just once after the module has been initialized? after dataFromJson?
		if(module) {
			if(APP->event->selectedWidget != this) {
				validText = string::f("%u", module->buffer.size());
				text = validText;
			}
		}
	}

};

// file selection dialog, based on PLAYERItem in cf
// https://github.com/cfoulc/cf/blob/master/src/PLAYER.cpp
struct ArrayFileSelectItem : MenuItem {
	Array *module;
	void onAction(const event::Action &e) override {
		std::string dir = module->lastLoadedPath.empty() ? asset::user("") : rack::string::directory(module->lastLoadedPath);
		char *path = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, NULL);
		if(path) {
			module->loadSample(path);
			module->lastLoadedPath = path;
			free(path);
		}
	}
};

struct ArrayEnableEditingMenuItem : MenuItem {
	Array *module;
	bool valueToSet;
	void onAction(const event::Action &e) override {
		module->enableEditing = valueToSet;
	}
};

struct ArrayInterpModeMenuItem : MenuItem {
	Array *module;
	Array::InterpBoundaryMode mode;
	ArrayInterpModeMenuItem(Array *pModule,
			Array::InterpBoundaryMode pMode,
			std::string label):
		MenuItem() {
			module = pModule;
			mode = pMode;
			text = label;
			rightText = CHECKMARK(module->boundaryMode == mode);
	}
	void onAction(const event::Action &e) override {
		module->boundaryMode = mode;
	}
};

struct ArrayModuleWidget : ModuleWidget {
	ArrayDisplay *display;
	NumberTextField *sizeSelector;
	Array *module;

	ArrayModuleWidget(Array *module) {
		setModule(module);
		this->module = module;

		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Array.svg"))); //TODO: fix ugly panel...

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		//TPort *createInputCentered(Vec pos, Module *module, int inputId) {
		addInput(createInputCentered<PJ301MPort>(Vec(27.5f, 247.5f), module, Array::PHASE_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(97.5f, 247.5f), module, Array::STEP_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(175.f, 247.5f), module, Array::INTERP_OUTPUT));

		addInput(createInputCentered<PJ301MPort>(Vec(27.5f, 347.5f), module, Array::REC_PHASE_INPUT));
		addInput(createInputCentered<PJ301MPort>(Vec(97.5f, 347.5f), module, Array::REC_SIGNAL_INPUT));
		addInput(createInputCentered<PJ301MPort>(Vec(175.f, 347.5f), module, Array::REC_ENABLE_INPUT));

		addParam(createParam<CKSSThree>(Vec(107.5f, 290), module, Array::OUTPUT_RANGE_PARAM));
		addParam(createParam<CKSSThree>(Vec(27.5f, 290), module, Array::PHASE_RANGE_PARAM));

		addChild(createLightCentered<MediumLight<RedLight>>(Vec(207.5f, 355), module, Array::REC_LIGHT));

		display = new ArrayDisplay(module);
		display->box.pos = Vec(5, 20);
		addChild(display);

		sizeSelector = new NumberTextField(module);
		sizeSelector->box.pos = Vec(175, 295);
		sizeSelector->box.size.x = 50;
		addChild(sizeSelector);
	}

	void appendContextMenu(ui::Menu *menu) override {

		Array *arr = dynamic_cast<Array*>(module);
		if(arr){
			menu->addChild(new MenuLabel()); // spacer
			auto *fsItem = new ArrayFileSelectItem();
			fsItem->text = "Load .wav file";
			fsItem->module = arr;
			menu->addChild(fsItem);

			auto *edItem = new ArrayEnableEditingMenuItem();
			edItem->text = "Disable drawing"; //TODO: better description (mention mouse)
			edItem->module = arr;
			edItem->rightText = CHECKMARK(!arr->enableEditing);
			edItem->valueToSet = !arr->enableEditing;
			menu->addChild(edItem);

			//TODO: replace with sub-menu
			auto *interpModeLabel = new MenuLabel();
			interpModeLabel->text = "Interpolation at boundary";
			menu->addChild(new MenuLabel());
			menu->addChild(interpModeLabel);
			menu->addChild(new ArrayInterpModeMenuItem(this->module, Array::INTERP_CONSTANT, "Constant"));
			menu->addChild(new ArrayInterpModeMenuItem(this->module, Array::INTERP_MIRROR, "Mirror"));
			menu->addChild(new ArrayInterpModeMenuItem(this->module, Array::INTERP_PERIODIC, "Periodic"));

		}

	}

};


Model *modelArray = createModel<Array, ArrayModuleWidget>("Array");
