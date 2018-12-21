#include "LittleUtils.hpp"
#include "dsp/digital.hpp"
//#include "app.hpp"
//#include "helpers.hpp"


struct ButtonModule : Module {
	enum ParamIds {
		BUTTON_PARAM,
		//TOGGLE_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		TRIG_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		TRIG_OUTPUT,
		GATE_OUTPUT,
		TOGGLE_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		TRIG_LIGHT,
		GATE_LIGHT,
		TOGGLE_LIGHT,
		NUM_LIGHTS
	};

	//bool gate = false;
	bool toggle = false; // TODO: serialize to json
	SchmittTrigger inputTrigger;
	PulseGenerator triggerGenerator;

	ButtonModule() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {
		//params[TOGGLE_PARAM].value = 0.f;
	}
	void step() override;


	// For more advanced Module features, read Rack's engine.hpp header file
	// - toJson, fromJson: serialization of internal data
	// - onSampleRateChange: event triggered by a change of sample rate
	// - onReset, onRandomize, onCreate, onDelete: implements special behavior when user clicks these from the context menu
};


void ButtonModule::step() {
	// Implement a simple sine oscillator
	float deltaTime = engineGetSampleTime();

	float gateVoltage = rescale(inputs[TRIG_INPUT].value, 0.1f, 2.f, 0.f, 1.f);

	bool gate = (bool(params[BUTTON_PARAM].value)
	             || gateVoltage >= 1.f);

	bool triggered = inputTrigger.process(gate ? 1.0f : 0.0f);

	bool trigger = triggerGenerator.process(deltaTime);

	if(triggered) {
		triggerGenerator.trigger(1e-3f);
		toggle = !toggle;
	}

	outputs[TRIG_OUTPUT].value = trigger ? 10.0f : 0.0f;
	lights[TRIG_LIGHT].setBrightnessSmooth(trigger);

	outputs[GATE_OUTPUT].value = gate ? 10.0f : 0.0f;
	lights[GATE_LIGHT].setBrightnessSmooth(gate);

	outputs[TOGGLE_OUTPUT].value = toggle ? 10.0f : 0.0f;
	lights[TOGGLE_LIGHT].setBrightnessSmooth(toggle);
}


//struct ButtonWidget : SVGButton {
	//ButtonModule *module = NULL;

	////template <typename T = ButtonWidget>
	////static T *create(Vec pos, Module *module)
	////onAction()

	//ButtonWidget() {
	//	//TODO: own button images here
	//	setSVGs(SVG::load(assetGlobal("res/ComponentLibrary/BefacoPush_0.svg")),
	//			SVG::load(assetGlobal("res/ComponentLibrary/BefacoPush_1.svg")));
	//}

	//template <typename T = ButtonWidget>
	//static T* create(Vec pos, ButtonModule *module) {
	//	ButtonWidget *o = Widget::create<ButtonWidget>(pos);
	//	o->module = module;
	//	return o;
	//}

	//void onAction(EventAction &e) override {
	//	module->triggerGenerator.trigger(1e-3f);
	//}
//};

struct ButtonWidget : SVGSwitch, MomentarySwitch {
	ButtonWidget() {
		addFrame(SVG::load(assetPlugin(plugin, "res/Button_button_0.svg")));
		addFrame(SVG::load(assetPlugin(plugin, "res/Button_button_1.svg")));
		//addFrame(SVG::load(assetGlobal("res/ComponentLibrary/CDK_0.svg")));
		//addFrame(SVG::load(assetGlobal("res/ComponentLibrary/CDK_1.svg")));
	}
};

struct ButtonModuleWidget : ModuleWidget {
	ButtonModuleWidget(ButtonModule *module) : ModuleWidget(module) {
		setPanel(SVG::load(assetPlugin(plugin, "res/Button_background.svg")));

		addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		//addParam(ParamWidget::create<TL1105>(Vec(10, 30), module, ButtonModule::BUTTON_PARAM, 0.0f, 1.0f, 0.0f));
		//addParam(ParamWidget::create<Davies1900hBlackKnob>(Vec(28, 87), module, ButtonModule::PITCH_PARAM, -3.0, 3.0, 0.0));
		addChild(ParamWidget::create<ButtonWidget>(Vec(7.5, 7.5 + RACK_GRID_WIDTH), module, ButtonModule::BUTTON_PARAM, 0.0f, 1.0f, 0.0f));
		//addChild(createParamCentered<TL1105>(Vec(30, 100), module, ButtonModule::BUTTON_PARAM, 0.0f, 1.0f, 0.0f));
		//addChild(Widget::create<BefacoPush>(Vec(10, 30)));
		//addChild(Widget::create<ButtonWidget>(Vec(10, 30), module));

		//addInput(Port::create<PJ301MPort>(Vec(33, 186), Port::INPUT, module, ButtonModule::PITCH_INPUT));

		//addOutput(Port::create<PJ301MPort>(Vec(33, 275), Port::OUTPUT, module, ButtonModule::SINE_OUTPUT));
		//addOutput(Port::create<PJ301MPort>(Vec(18, 217), Port::OUTPUT, module, ButtonModule::TRIG_OUTPUT));
		addInput(createInputCentered<PJ301MPort>(Vec(22.5, 135), module, ButtonModule::TRIG_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(22.5, 195), module, ButtonModule::TRIG_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(22.5, 255), module, ButtonModule::GATE_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(22.5, 315), module, ButtonModule::TOGGLE_OUTPUT));

		//addChild(ModuleLightWidget::create<MediumLight<RedLight>>(Vec(41, 59), module, ButtonModule::TRIG_LIGHT));
		double offset = 3.6;
		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(37.5 - offset, 180 + offset), module, ButtonModule::TRIG_LIGHT));
		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(37.5 - offset, 240 + offset), module, ButtonModule::GATE_LIGHT));
		addChild(createLightCentered<TinyLight<GreenLight>>(Vec(37.5 - offset, 300 + offset), module, ButtonModule::TOGGLE_LIGHT));
	}
};


// Specify the Module and ModuleWidget subclass, human-readable
// author name for categorization per plugin, module slug (should never
// change), human-readable module name, and any number of tags
// (found in `include/tags.hpp`) separated by commas.
Model *modelButtonModule = Model::create<ButtonModule, ButtonModuleWidget>("Little Utils", "Button", "Button", UTILITY_TAG);