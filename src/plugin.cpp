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

	// Any other plugin initialization may go here.
	// As an alternative, consider lazy-loading assets and lookup tables when your module is created to reduce startup times of Rack.
}
