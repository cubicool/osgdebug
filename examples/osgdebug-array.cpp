//vimrun! ./examples/osgdebug-array

#include "../osgx.hpp"

#include <cassert>

using namespace osgx::literals;
// using osgx::literals::operator""_v;

int main(int argc, char** argv) {
	auto av3_3 = osgx::Vec3Array({
		{1_v, 2_v, 3_v},
		{10_v, 20_v, 30_v},
		{100_v, 200_v, 300_v}
	});

	assert(av3_3.size() == 3);
	assert(av3_3[0] == osg::Vec3(1_v, 2_v, 3_v));

	av3_3.append_range({
		{1000_v, 2000_v, 3000_v},
		{10000_v, 20000_v, 30000_v}
	});

	assert(av3_3.size() == 5);
	assert(av3_3[3] == osg::Vec3(1000_v, 2000_v, 3000_v));

	auto nav3_2 = osgx::make_ref<osgx::Vec3Array>(
		osg::Vec3(1_v, 2_v, 3_v),
		osg::Vec3(4_v, 5_v, 6_v)
	);

	assert(nav3_2->size() == 2);
	assert((*nav3_2)[1] == osg::Vec3(4_v, 5_v, 6_v));

	nav3_2->append_n<1>({7_v, 8_v, 9_v});

	assert(nav3_2->size() == 3);
	assert((*nav3_2)[2] == osg::Vec3(7_v, 8_v, 9_v));

	auto nav2_3 = osgx::make_ref<osgx::Vec2Array>(nullptr);

	// nav2_3 = osgx::Vec2Array::create({
	nav2_3 = new osgx::Vec2Array({
		{11_v, 22_v},
		{33_v, 44_v},
		{55_v, 66_v},
	});

	assert(nav2_3->size() == 3);
	assert((*nav2_3)[2] == osg::Vec2(55_v, 66_v));

	return 0;
}
