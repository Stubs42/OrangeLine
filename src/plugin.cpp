#include "plugin.hpp"


Plugin *pluginInstance;


void init(Plugin *p) {
	pluginInstance = p;

	// Add modules here
	p->addModel(modelFence);
	p->addModel(modelSwing);
	p->addModel(modelMother);
	p->addModel(modelPhrase);
	p->addModel(modelDejavu);
	p->addModel(modelGator);
	p->addModel(modelResc);
	p->addModel(modelMorph);
	p->addModel(modelMorpheus);
	p->addModel(modelBuckets);
	p->addModel(modelCron);
	p->addModel(modelHold);
	p->addModel(modelCVLanes);
	p->addModel(modelMidiLanes);
	p->addModel(modelLanesCV);
	p->addModel(modelLanesMidi);
	p->addModel(modelK2C);
	p->addModel(modelCC14);
	p->addModel(modelD2D);
	p->addModel(modelCC2CV);
	p->addModel(modelCV2CC);
	p->addModel(modelRECALL);
	// p->addModel(modelTemplate);
	// p->addModel(modelWidgetTest);

	// Any other plugin initialization may go here.
	// As an alternative, consider lazy-loading assets and lookup tables when your module is created to reduce startup times of Rack.
}
