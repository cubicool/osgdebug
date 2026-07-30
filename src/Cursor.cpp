#include "osgx/Cursor.hpp"

#include "osgx/Warnings.hpp"

OSGX_DISABLE_WARNINGS

#include <osgViewer/GraphicsWindow>

OSGX_ENABLE_WARNINGS

namespace osgx::platform {

namespace {

osgViewer::GraphicsWindow* graphicsWindow(osgViewer::View& view) {
	auto* camera = view.getCamera();

	if(!camera) return nullptr;

	return dynamic_cast<osgViewer::GraphicsWindow*>(camera->getGraphicsContext());
}

}

void setCursorVisible(osgViewer::View& view, bool visible) {
	auto* gw = graphicsWindow(view);

	if(gw) gw->useCursor(visible);
}

void warpPointer(osgViewer::View& view, float x, float y) {
	view.requestWarpPointer(x, y);
}

void PointerCapture::setCaptured(bool captured) {
	if(captured == _captured) return;

	_captured = captured;

	if(auto* view = _view.get()) setCursorVisible(*view, !captured);

	_accum.set(0.0f, 0.0f);
	_recenterPending = captured;
}

osg::Vec2 PointerCapture::consume() {
	osg::Vec2 result = _accum;

	_accum.set(0.0f, 0.0f);

	return result;
}

bool PointerCapture::handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa) {
	if(ea.getEventType() == osgGA::GUIEventAdapter::RESIZE) _recenterPending = _captured;

	if(!_captured) return false;

	if(_recenterPending) {
		_centerX = 0.5f * (ea.getXmin() + ea.getXmax());
		_centerY = 0.5f * (ea.getYmin() + ea.getYmax());
		_recenterPending = false;

		aa.requestWarpPointer(_centerX, _centerY);

		return false;
	}

	if(
		ea.getEventType() != osgGA::GUIEventAdapter::MOVE &&
		ea.getEventType() != osgGA::GUIEventAdapter::DRAG
	) return false;

	_accum.x() += ea.getX() - _centerX;
	_accum.y() += ea.getY() - _centerY;

	aa.requestWarpPointer(_centerX, _centerY);

	return true;
}

}
