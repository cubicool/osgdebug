#pragma once

#include "osgx/Warnings.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Callback>
#include <osg/Node>
#include <osg/ref_ptr>

OSGX_ENABLE_WARNINGS

#include <cstddef>
#include <limits>
#include <string>

namespace osgx::gltf {

// Private implementations supplied by the glTF reader implement this small
// interface. Applications should use SimplePlayer rather than retaining it.
class SimplePlayerControl {
public:
	virtual ~SimplePlayerControl() = default;

	virtual std::size_t getNumAnimations() const = 0;
	virtual std::string getAnimationName(std::size_t index) const = 0;
	virtual bool playAnimation(std::size_t index) = 0;
	virtual bool playAnimation(const std::string& name) = 0;
	virtual std::size_t getCurrentAnimationIndex() const = 0;
	virtual std::string getCurrentAnimationName() const = 0;
	virtual void setPlaying(bool playing) = 0;
	virtual bool getPlaying() const = 0;
	virtual void restart() = 0;
};

class SimplePlayer {
public:
	static constexpr std::size_t NoAnimation = std::numeric_limits<std::size_t>::max();

	SimplePlayer() = default;

	explicit SimplePlayer(osg::Node* model):
		_model(model) {
		for(osg::Callback* callback = model ? model->getUpdateCallback() : nullptr;
			callback;
			callback = callback->getNestedCallback()) {
			if(auto* control = dynamic_cast<SimplePlayerControl*>(callback)) {
				_callback = callback;
				_control = control;
				break;
			}
		}
	}

	explicit operator bool() const { return _control != nullptr; }

	std::size_t getNumAnimations() const {
		return _control ? _control->getNumAnimations() : 0;
	}

	std::string getAnimationName(std::size_t index) const {
		return _control ? _control->getAnimationName(index) : std::string();
	}

	bool playAnimation(std::size_t index) {
		return _control && _control->playAnimation(index);
	}

	bool playAnimation(const std::string& name) {
		return _control && _control->playAnimation(name);
	}

	std::size_t getCurrentAnimationIndex() const {
		return _control ? _control->getCurrentAnimationIndex() : NoAnimation;
	}

	std::string getCurrentAnimationName() const {
		return _control ? _control->getCurrentAnimationName() : std::string();
	}

	void setPlaying(bool playing) {
		if(_control) _control->setPlaying(playing);
	}

	bool getPlaying() const {
		return _control && _control->getPlaying();
	}

	void togglePlaying() {
		if(_control) _control->setPlaying(!_control->getPlaying());
	}

	void restart() {
		if(_control) _control->restart();
	}

private:
	osg::ref_ptr<osg::Node> _model;
	osg::ref_ptr<osg::Callback> _callback;
	SimplePlayerControl* _control = nullptr;
};

}
