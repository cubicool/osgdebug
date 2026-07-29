// vimrun! ./examples/osgx-grid

#include "../osgx/Core.hpp"
#include "../osgx/Grid.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Group>
#include <osg/MatrixTransform>
#include <osg/Quat>
#include <osg/Shape>
#include <osg/ShapeDrawable>
#include <osgViewer/Viewer>

OSGX_ENABLE_WARNINGS

namespace {

osg::ref_ptr<osg::Node> makeFrameRod(
	const osg::Vec3& start,
	const osg::Vec3& end,
	float radius,
	const osg::Vec4& color
) {
	const osg::Vec3 direction = end - start;
	const float length = direction.length();
	const osg::Vec3 midpoint = (start + end) * 0.5f;

	auto rod = osgx::make_ref<osg::ShapeDrawable>(
		new osg::Cylinder(osg::Vec3(), radius, length)
	);

	rod->setColor(color);

	auto transform = osgx::make_ref<osg::MatrixTransform>();
	osg::Quat rotation;

	rotation.makeRotate(osg::Vec3(0.0f, 0.0f, 1.0f), direction);

	// osg::Cylinder is aligned to local Z.  Rotate it to span the requested edge, then
	// move its center to the midpoint; the spheres below cover the joins cleanly.
	transform->setMatrix(
		osg::Matrix::rotate(rotation) *
		osg::Matrix::translate(midpoint)
	);

	transform->addChild(rod);

	return transform;
}

void addFrameJoint(
	osg::Group* group,
	const osg::Vec3& position,
	float radius,
	const osg::Vec4& color
) {
	auto joint = osgx::make_ref<osg::ShapeDrawable>(new osg::Sphere(position, radius));

	joint->setColor(color);
	group->addChild(joint);
}

void addGridRoom(osg::Group* root) {
	constexpr float halfWidth = 5.0f;
	constexpr float height = 8.0f;
	constexpr float rodRadius = 0.10f;
	const osg::Vec4 frameColor(0.55f, 0.60f, 0.70f, 1.0f);

	auto configureGrid = [](osgx::Grid* grid) {
		grid->setCanvasSize(osg::Vec2(500.0f, 500.0f));
		grid->setGridInterval(50.0f);
		grid->setGridIntervalStrong(250.0f);
		grid->setLineMode(osgx::Grid::LINE_GRID_UNITS);
		grid->setLineWidth(1.0f);
		grid->setEdgeMode(osgx::Grid::EDGE_HIDE);
		grid->setColorBg(osg::Vec4(0.0f, 0.0f, 0.0f, 0.0f));
		grid->setColorLine(osg::Vec4(0.22f, 0.30f, 0.42f, 1.0f));
		grid->setColorLineStrong(osg::Vec4(0.52f, 0.68f, 0.88f, 1.0f));
	};

	// An open Z-up room: the floor, rear wall, and right wall all share their seams exactly.
	// The model belongs around the origin, resting just above the floor at z = 0.
	auto floor = osgx::make_ref<osgx::Grid>(
		osg::Vec3(-halfWidth, -halfWidth, 0.0f),
		osg::Vec3(2.0f * halfWidth, 0.0f, 0.0f),
		osg::Vec3(0.0f, 2.0f * halfWidth, 0.0f)
	);
	auto backWall = osgx::make_ref<osgx::Grid>(
		osg::Vec3(-halfWidth, halfWidth, 0.0f),
		osg::Vec3(2.0f * halfWidth, 0.0f, 0.0f),
		osg::Vec3(0.0f, 0.0f, height)
	);
	auto rightWall = osgx::make_ref<osgx::Grid>(
		osg::Vec3(halfWidth, -halfWidth, 0.0f),
		osg::Vec3(0.0f, 2.0f * halfWidth, 0.0f),
		osg::Vec3(0.0f, 0.0f, height)
	);

	configureGrid(floor);
	configureGrid(backWall);
	configureGrid(rightWall);

	auto panels = osgx::make_ref<osg::Geode>();
	panels->addDrawable(floor);
	panels->addDrawable(backWall);
	panels->addDrawable(rightWall);
	root->addChild(panels);

	auto frame = osgx::make_ref<osg::Group>();
	const osg::Vec3 frontLeft(-halfWidth, -halfWidth, 0.0f);
	const osg::Vec3 frontRight(halfWidth, -halfWidth, 0.0f);
	const osg::Vec3 backLeft(-halfWidth, halfWidth, 0.0f);
	const osg::Vec3 backRight(halfWidth, halfWidth, 0.0f);
	const osg::Vec3 backLeftTop(-halfWidth, halfWidth, height);
	const osg::Vec3 backRightTop(halfWidth, halfWidth, height);
	const osg::Vec3 frontRightTop(halfWidth, -halfWidth, height);

	// Nine unique edges: six outside edges plus the three seams shared by two panels.
	for(const auto& [start, end] : std::initializer_list<std::pair<osg::Vec3, osg::Vec3>>{
		{frontLeft, frontRight}, {frontLeft, backLeft},
		{backLeft, backRight}, {frontRight, backRight}, {backRight, backRightTop},
		{backLeft, backLeftTop}, {backLeftTop, backRightTop},
		{frontRight, frontRightTop}, {frontRightTop, backRightTop}
	}) frame->addChild(makeFrameRod(start, end, rodRadius, frameColor));

	for(const auto& position : {
		frontLeft, frontRight, backLeft, backRight,
		backLeftTop, backRightTop, frontRightTop
	}) addFrameJoint(frame, position, rodRadius * 1.25f, frameColor);

	root->addChild(frame);

}

}

int main(int argc, char** argv) {
	bool orthoMode = argc > 1 && std::string(argv[1]) == "ortho";

	osg::ref_ptr<osg::Node> root;

	if(orthoMode) {
		// Fullscreen NDC quad, always drawn first via a PRE_RENDER camera. Configure the Grid
		// itself before wrapping it, since createOrthoCamera() would only hand back the camera.
		auto grid = osgx::make_ref<osgx::Grid>();

		grid->setCanvasSize(osg::Vec2(800.0f, 600.0f));
		grid->setGridInterval(10.0f);
		grid->setGridIntervalStrong(100.0f);
		grid->setLineWidthPx(1.0f);
		grid->setEdgeMode(osgx::Grid::EDGE_NUDGE);

		root = grid->orthoCamera();
	}

	else {
		auto room = osgx::make_ref<osg::Group>();

		addGridRoom(room);
		root = room;
	}

	osgViewer::Viewer viewer;

	viewer.setSceneData(root);
	viewer.getCamera()->setClearColor(osg::Vec4(0.015f, 0.020f, 0.035f, 1.0f));

	// The ortho camera's own clear IS the frame's first paint; stop the viewer's main
	// camera from immediately stomping it with a second COLOR_BUFFER_BIT clear.
	if(orthoMode) viewer.getCamera()->setClearMask(GL_DEPTH_BUFFER_BIT);

	auto r = viewer.run();

	return r;
}
