// vimrun! ./examples/osgx-lights
//
// A standalone demo of osgx::pbr's typed direct lights (directional/point/sphere/spot) and their
// osgx::gizmo visualizations, without any glTF asset or baked IBL environment -- just one shaded
// osgx::Cube (6 flat, axis-aligned faces -- deliberately not a more "interesting" shape, so which
// face is lit is visually unambiguous), a hand-assembled minimal PBR fragment shader (osgx::pbr's
// GLSL snippets composed directly, the same mechanism osgx::gltf::pbribl's
// FULL_PBR_FRAGMENT_SHADER_SRC uses internally), and a small constant ambient term standing in for
// real image-based lighting (see the fragment shader below -- deliberately NOT osgx::ibl's
// prefiltered-cubemap pipeline, which would need an HDR asset and a bake pass; out of scope for
// "load nothing, just press a key").
//
// Press 'n' to cycle through one light of each type; the console prints which one is active. The
// gizmos (a wireframe plane+arrow overlay for directional, depth-tested wireframe spheres/cones
// for point/sphere/spot) read osgx::pbr::LightSet's uniforms live every frame, so cycling the demo
// is nothing more than calling LightSet's setters -- the gizmos and the shaded object update
// themselves.
//
// When built against osgx::imgui (OSGX_IMGUI -- automatic whenever the library it's linked
// against, osgx::osgx, was itself built with OSGX_WITH_IMGUI; no extra CMake wiring needed here),
// an ImGui window adds one section per light type with live sliders for every parameter that
// type's LightSet setter actually takes. Each section's "Activate" button switches which one is
// currently live (LightSet's slot 0); the others just sit there as "prepared" values you can
// switch to. Without OSGX_IMGUI, only the 'n'-key cycling is available.

#include "osgx/osgx.hpp"
#include "osgx/ImGui.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/Geode>
#include <osg/GL>
#include <osg/Group>
#include <osg/Math>
#include <osg/Program>
#include <osg/Shader>
#include <osg/StateSet>
#include <osg/Uniform>

#include <osgGA/TrackballManipulator>

#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

OSGX_ENABLE_WARNINGS

#include <array>
#include <cstddef>
#include <iostream>
#include <memory>
#include <string_view>

namespace {

constexpr std::string_view VERTEX_SHADER = R"GLSL(
#version 460 core

in vec3 position;
in vec3 normal;

uniform mat4 osg_ModelViewProjectionMatrix;
uniform mat4 osg_ModelViewMatrix;
uniform mat3 osg_NormalMatrix;

out vec3 vNormal;
out vec3 vPosition;

void main() {
	vNormal = osg_NormalMatrix * normal;
	vPosition = (osg_ModelViewMatrix * vec4(position, 1.0)).xyz;
	gl_Position = osg_ModelViewProjectionMatrix * vec4(position, 1.0);
}
)GLSL";

// Mirrors osgx::gltf::pbribl::FULL_PBR_FRAGMENT_SHADER_SRC's own per-light loop (see
// src/gltf/PBRIBL.cpp) almost verbatim, minus the IBL diffuse/specular terms and diagnostics --
// this is the same osgx::pbr type-dispatch, just without a glTF material or environment feeding
// it. `ambientColor`/`ambientIntensity` is a flat fill light, not real IBL (see file header).
constexpr std::string_view FRAGMENT_SHADER = R"GLSL(
#version 460 core

// PI must precede the pragma below -- it's textually replaced in place by the spliced snippet
// source, and D_GGX/DIRECT_DIFFUSE/etc. reference `PI` assuming it's already in scope (see
// PBR.hpp's file-level contract note). PBRIBL.cpp's own FULL_PBR_FRAGMENT_SHADER_SRC gets this
// ordering right; this shader originally didn't.
const float PI = 3.14159265359;

// This shader no longer touches lightCount/lightType/DIRECT_LIGHT/etc. directly -- it only needs
// the osgx_DirectLighting() CONTRACT declaration (DIRECT_LIGHTING_DECL) plus a call site. The actual
// per-light dispatch loop lives in a SEPARATE compiled shader object (see makeProgram() below,
// DIRECT_LIGHTING_HOOK_DEFAULT), linked in at Program-link time -- see PBR.hpp's own comment on
// DIRECT_LIGHTING_DECL/DIRECT_LIGHTING_HOOK_DEFAULT for the full rationale (mirrors osgSlug's hook
// pattern). This is the validation vehicle for that new contract, live-tested 2026-08-16.
#pragma osgx::pbr MATERIAL_STRUCT, DIRECT_LIGHTING_DECL

in vec3 vNormal;
in vec3 vPosition;

uniform mat4 osg_ViewMatrix;
uniform mat4 osg_ViewMatrixInverse;

uniform vec3 albedo;
uniform float roughness;
uniform float metallic;
uniform vec3 ambientColor;
uniform float ambientIntensity;

out vec4 fragColor;

void main() {
	osgx_Material mat;

	mat.albedo = albedo;
	mat.ao = 1.0;
	mat.roughness = roughness;
	mat.metallic = metallic;
	mat.F0 = mix(vec3(0.04), albedo, metallic);

	mat3 invViewRot = transpose(mat3(osg_ViewMatrix));
	vec3 N = invViewRot * normalize(vNormal);
	vec3 V = invViewRot * normalize(-vPosition);
	vec3 worldPos = (osg_ViewMatrixInverse * vec4(vPosition, 1.0)).xyz;

	vec3 color = ambientColor * ambientIntensity * mat.albedo * mat.ao;

	color += osgx_DirectLighting(N, V, worldPos, mat);

	color = pow(clamp(color, vec3(0.0), vec3(1.0)), vec3(1.0 / 2.2));

	fragColor = vec4(color, 1.0);
}
)GLSL";

osg::ref_ptr<osg::Program> makeProgram() {
	osgx::pbr::registerShaderLibs();

	auto program = osgx::make_ref<osg::Program>();
	// Only the fragment shader carries `#pragma osgx::pbr ...` directives -- matches
	// osgx::gltf::pbribl::createPBRIBLScene()'s own split (PBRIBL.cpp), which resolves its
	// fragment shader but adds its vertex shader unchanged.
	auto fragmentSrc = osgx::resolveShaderLibs(std::string(FRAGMENT_SHADER));
	// The osgx_DirectLighting() CONTRACT's default definition -- a second, separately compiled
	// FRAGMENT shader object with no main() of its own, added alongside fragmentSrc above. GLSL
	// cross-shader-object linking resolves fragmentSrc's forward-declared osgx_DirectLighting() call
	// against this at Program-link time. Self-contained (carries its own #pragma), so it needs no
	// further resolveShaderLibs() dependency wiring beyond this one call. Swap this shader object
	// out for a different one (defining osgx_DirectLighting() differently) to override direct-light
	// shading without touching fragmentSrc at all -- see PBR.hpp's DIRECT_LIGHTING_DECL comment.
	auto hookSrc = osgx::resolveShaderLibs(std::string(osgx::pbr::DIRECT_LIGHTING_HOOK_DEFAULT));

	program->setName("osgx_lights_demo");
	program->addShader(new osg::Shader(osg::Shader::VERTEX, std::string(VERTEX_SHADER)));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, fragmentSrc));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, hookSrc));
	program->addBindAttribLocation("position", 0);
	program->addBindAttribLocation("normal", 1);

	return program;
}

// One light of each type, cycled with 'n' or an ImGui section's "Activate" button. Positions/
// angles are sized against the icosahedron's radius=1.2 (see main()) so every one reads clearly
// without further tuning. Kept deliberately close to the object (~2-3x its radius, not further) --
// TrackballManipulator's default "home" view fits the WHOLE scene bound, gizmos included, so a
// light placed far out pushes the camera back and makes the object itself look small/distant even
// though nothing is actually wrong.
enum class Demo: std::size_t {
	Directional,
	Point,
	Sphere,
	Spot,
	Count
};

constexpr std::array<std::string_view, static_cast<std::size_t>(Demo::Count)> DEMO_NAMES = {
	"Directional",
	"Point",
	"Sphere (point + sourceRadius)",
	"Spot"
};

// Spot's initial apex-to-target distance, in world units -- SPOT_DISTANCE below sizes
// createLightGizmos()'s spotConeLength (main()) to match, so the cone gizmo reaches the object
// instead of stopping short of it (LightMarkers' own default spotConeLength=1.0 is unit-scene-
// scale, too short here) -- generous enough to still mostly cover the ImGui position slider's
// range (see LightsState below) as the light is dragged around live.
constexpr float SPOT_DISTANCE = 3.5f;

// Persistent, live-editable parameters for every light type -- backs both the ImGui sliders
// (OSGX_IMGUI) and the 'n'-key defaults. Only `activeDemo`'s own fields are ever written into
// LightSet slot 0 (see applyState() below); the rest just sit here as "prepared" values, so
// switching the active type doesn't lose whatever you'd already dialed in on the others.
struct LightsState {
	Demo activeDemo = Demo::Directional;

	// Axis-aligned on purpose, for unambiguous verification: travel direction (0,0,-1) means the
	// light source is directly above (+Z), so exactly one cube face -- the top (+Z-normal) face --
	// should receive direct light; every other face should show only the flat ambient fill.
	// Each type gets its own strongly saturated, mutually distinct color (red/green/blue/yellow)
	// against the now-neutral-gray shape (see the `albedo` uniform below) -- makes it immediately
	// obvious BY EYE which light is contributing, and separates Sphere's color from Point's own
	// (they used to share one color, making the two easy to visually conflate while comparing).
	osg::Vec3 directionalDirection{0.0f, 0.0f, -1.0f};
	osg::Vec3 directionalColor{0.90f, 0.15f, 0.15f}; // red
	float directionalIntensity = 3.0f;

	osg::Vec3 pointPosition{1.5f, -1.1f, 1.2f};
	osg::Vec3 pointColor{0.15f, 0.85f, 0.25f}; // green
	float pointIntensity = 12.0f;

	osg::Vec3 spherePosition{1.5f, -1.1f, 1.2f};
	osg::Vec3 sphereColor{0.20f, 0.45f, 0.95f}; // blue
	float sphereIntensity = 12.0f;
	float sphereRadius = 0.7f;

	osg::Vec3 spotPosition{1.8f, -1.8f, 1.8f};
	osg::Vec3 spotDirection{-1.0f, 1.0f, -1.0f}; // aimed at the origin
	osg::Vec3 spotColor{0.95f, 0.85f, 0.10f}; // yellow
	float spotIntensity = 30.0f;
	float spotInnerDegrees = 15.0f;
	float spotOuterDegrees = 32.0f;
	float spotSourceRadius = 0.0f;
};

void applyState(const osgx::pbr::LightSet& lights, const LightsState& state) {
	lights.setCount(1);

	switch(state.activeDemo) {
	case Demo::Directional:
		lights.setDirectional(0, state.directionalDirection, state.directionalColor, state.directionalIntensity);

		break;

	case Demo::Point:
		lights.setPoint(0, state.pointPosition, state.pointColor, state.pointIntensity);

		break;

	case Demo::Sphere:
		lights.setPoint(0, state.spherePosition, state.sphereColor, state.sphereIntensity, state.sphereRadius);

		break;

	case Demo::Spot:
		lights.setSpot(
			0,
			state.spotPosition,
			state.spotDirection,
			state.spotColor,
			state.spotIntensity,
			osg::DegreesToRadians(state.spotInnerDegrees),
			osg::DegreesToRadians(state.spotOuterDegrees),
			state.spotSourceRadius
		);

		break;

	default: break;
	}
}

}

int main() {
	auto root = osgx::make_ref<osg::Group>();
	auto geode = osgx::make_ref<osg::Geode>();
	// A cube, not an icosahedron -- 6 flat, axis-aligned faces make "which side is lit" visually
	// unambiguous, unlike 20 near-identical triangular facets.
	auto shape = osgx::make_ref<osgx::Cube>(osg::Vec3(), 1.2f);
	auto* ss = geode->getOrCreateStateSet();

	geode->addDrawable(shape.get());
	ss->setMode(GL_CULL_FACE, osg::StateAttribute::ON);
	ss->setAttributeAndModes(makeProgram().get(), osg::StateAttribute::ON);
	// Neutral gray (R=G=B) -- any color tint the shape shows is entirely the active light's own
	// color, not mixed with a pre-tinted albedo.
	ss->addUniform(new osg::Uniform("albedo", osg::Vec3(0.5f, 0.5f, 0.5f)));
	ss->addUniform(new osg::Uniform("roughness", 0.35f));
	ss->addUniform(new osg::Uniform("metallic", 0.1f));
	ss->addUniform(new osg::Uniform("ambientColor", osg::Vec3(1.0f, 1.0f, 1.0f)));
	ss->addUniform(new osg::Uniform("ambientIntensity", 0.05f));

	auto lights = osgx::pbr::LightSet::create(ss);

	// A shared_ptr, not a `mutable` lambda -- LambdaKeyHandler::_wrap() (Callbacks.hpp) invokes the
	// stored callable through a const call path, so a `mutable` lambda's non-const operator() can't
	// bind there; ImGui section callables (Panel::draw()) are likewise called through a const
	// std::function. The pointee stays mutable through a by-value-captured (and therefore const)
	// shared_ptr, which sidesteps that without needing `mutable` anywhere.
	auto state = std::make_shared<LightsState>();

	applyState(lights, *state);

	// minMarkerRadius bumped up from the library default (0.05, sized for a unit-scale scene) so
	// the point/sphere marker reads clearly against a radius-1.2 object; spotConeLength set to
	// SPOT_DISTANCE so the cone gizmo reaches the object instead of stopping short of it the way
	// the library default (unit-scene-scale) would here.
	auto gizmos = osgx::gizmo::createLightGizmos(lights, geode.get(), 0.15f, SPOT_DISTANCE);

	root->addChild(geode.get());
	root->addChild(gizmos.markers.get());
	root->addChild(gizmos.overlay.get());

	auto printActive = [state]() {
		std::cout << "osgx-lights: showing "
			<< DEMO_NAMES[static_cast<std::size_t>(state->activeDemo)] << std::endl;
	};

	std::cout << "osgx-lights: showing " << DEMO_NAMES[static_cast<std::size_t>(state->activeDemo)]
		<< " (press 'n' for next)" << std::endl;

	auto viewer = osgViewer::Viewer();

#ifdef OSGX_IMGUI
	// Dear ImGui's single global context isn't safe to touch from more than one OSG draw thread --
	// osgx::imgui::Widget requires this (see its own class comment); harmless to set unconditionally
	// even when OSGX_IMGUI is off.
	viewer.setThreadingModel(osgViewer::Viewer::SingleThreaded);
#endif

	viewer.addEventHandler(new osgx::LambdaKeyHandler('n', [lights, state, printActive](auto&, auto&) {
		auto next = (static_cast<std::size_t>(state->activeDemo) + 1) % static_cast<std::size_t>(Demo::Count);

		state->activeDemo = static_cast<Demo>(next);

		applyState(lights, *state);
		printActive();

		return true;
	}));

	viewer.setSceneData(root.get());

	auto* manip = new osgGA::TrackballManipulator();

	viewer.setCameraManipulator(manip);
	// An explicit home position/orientation, not TrackballManipulator's auto-fit default -- looking
	// down from above and to the side keeps the top (+Z) face and two side faces all visible at
	// once (useful for judging which face a directional light is actually hitting), and sidesteps
	// auto-fit's own habit of zooming out to include the light gizmos scattered around the object,
	// which was making the cube itself look small/distant for no reason (see LightsState's comment
	// on SPOT_DISTANCE above for the same issue on the light-placement side).
	manip->setHomePosition(osg::Vec3(3.5, -4.5, 3.5), osg::Vec3(0.0, 0.0, 0.0), osg::Vec3(0.0, 0.0, 1.0));
	manip->home(0.0);
	viewer.getCamera()->setClearColor(osg::Vec4(0.04f, 0.05f, 0.10f, 1.0f));
	viewer.addEventHandler(new osgViewer::StatsHandler());

#ifdef OSGX_IMGUI
	// activateButton() draws the "Activate"/"Active" button shared by every section below, and
	// applies+prints immediately on click -- switching light types must take effect right away even
	// though nothing else in the newly-active section re-applies it this frame, and even if that
	// section happens to be collapsed (Panel::draw() only calls a collapsed section's fn -- see
	// ImGui.cpp's CollapsingHeader-gated dispatch -- once it's open).
	auto activateButton = [lights, state, printActive](Demo demo) {
		bool active = state->activeDemo == demo;

		if(active) ImGui::BeginDisabled();

		if(ImGui::Button(active ? "Active" : "Activate")) {
			state->activeDemo = demo;

			applyState(lights, *state);
			printActive();
		}

		if(active) ImGui::EndDisabled();
	};

	auto* gui = new osgx::imgui::Widget(viewer);

	// Every section's fn() runs directly in Panel's window ID stack -- Panel::draw() (ImGui.cpp)
	// only auto-wraps `expand`-mode sections (the Profiler's scrolling table) in their own
	// PushID(label); ours are all expand=false. Without an explicit PushID/PopID pair per section
	// here, identical widget labels across sections ("Position", "Color", "Intensity", "Source
	// Radius" all appear in more than one of these) hash to the SAME Dear ImGui id, so e.g. every
	// "Color" picker across all four sections fought over one shared popup-open/drag state --
	// confirmed live: dragging one section's slider visibly disturbed another's, and only one
	// Color swatch would ever open its picker.
	gui->addSection("Directional", [lights, state, activateButton](osg::RenderInfo&) {
		ImGui::PushID("Directional");
		activateButton(Demo::Directional);
		ImGui::SameLine();
		ImGui::TextDisabled("no position, no falloff -- parallel rays");

		bool changed = false;

		changed |= ImGui::SliderFloat3("Direction", state->directionalDirection.ptr(), -1.0f, 1.0f);
		changed |= ImGui::ColorEdit3("Color", state->directionalColor.ptr());
		changed |= ImGui::SliderFloat("Intensity", &state->directionalIntensity, 0.0f, 10.0f);

		if(changed && state->activeDemo == Demo::Directional) applyState(lights, *state);

		ImGui::PopID();
	}, osgx::imgui::makeSectionOptions(false, true));

	gui->addSection("Point", [lights, state, activateButton](osg::RenderInfo&) {
		ImGui::PushID("Point");
		activateButton(Demo::Point);
		ImGui::SameLine();
		ImGui::TextDisabled("inverse-square falloff, ideal (zero-size) specular highlight");

		bool changed = false;

		changed |= ImGui::SliderFloat3("Position", state->pointPosition.ptr(), -5.0f, 5.0f);
		changed |= ImGui::ColorEdit3("Color", state->pointColor.ptr());
		changed |= ImGui::SliderFloat("Intensity", &state->pointIntensity, 0.0f, 60.0f);

		if(changed && state->activeDemo == Demo::Point) applyState(lights, *state);

		ImGui::PopID();
	}, osgx::imgui::makeSectionOptions(false, true));

	gui->addSection("Sphere", [lights, state, activateButton](osg::RenderInfo&) {
		ImGui::PushID("Sphere");
		activateButton(Demo::Sphere);
		ImGui::SameLine();
		ImGui::TextDisabled("a Point light with sourceRadius > 0 -- not a separate light type");

		bool changed = false;

		changed |= ImGui::SliderFloat3("Position", state->spherePosition.ptr(), -5.0f, 5.0f);
		changed |= ImGui::ColorEdit3("Color", state->sphereColor.ptr());
		changed |= ImGui::SliderFloat("Intensity", &state->sphereIntensity, 0.0f, 60.0f);
		changed |= ImGui::SliderFloat("Source Radius", &state->sphereRadius, 0.0f, 2.0f);

		if(changed && state->activeDemo == Demo::Sphere) applyState(lights, *state);

		ImGui::PopID();
	}, osgx::imgui::makeSectionOptions(false, true));

	gui->addSection("Spot", [lights, state, activateButton](osg::RenderInfo&) {
		ImGui::PushID("Spot");
		activateButton(Demo::Spot);
		ImGui::SameLine();
		ImGui::TextDisabled("cone-attenuated point light; sourceRadius still applies");

		bool changed = false;

		changed |= ImGui::SliderFloat3("Position", state->spotPosition.ptr(), -5.0f, 5.0f);
		changed |= ImGui::SliderFloat3("Direction", state->spotDirection.ptr(), -1.0f, 1.0f);
		changed |= ImGui::ColorEdit3("Color", state->spotColor.ptr());
		changed |= ImGui::SliderFloat("Intensity", &state->spotIntensity, 0.0f, 80.0f);
		changed |= ImGui::SliderFloat("Inner Cone (deg)", &state->spotInnerDegrees, 0.0f, 89.0f);
		changed |= ImGui::SliderFloat("Outer Cone (deg)", &state->spotOuterDegrees, 0.0f, 89.0f);
		changed |= ImGui::SliderFloat("Source Radius", &state->spotSourceRadius, 0.0f, 2.0f);

		// GLSL's smoothstep() has undefined results if edge0 >= edge1 -- SPOT_LIGHT_RADIANCE
		// (PBR.hpp) calls it as smoothstep(cos(outer), cos(inner), ...), so inner must stay
		// strictly less than outer here (cosine is decreasing, so this ordering is what keeps
		// cos(outer) < cos(inner) true no matter what the sliders above just did).
		if(state->spotInnerDegrees >= state->spotOuterDegrees) {
			state->spotOuterDegrees = state->spotInnerDegrees + 1.0f;
		}

		if(changed && state->activeDemo == Demo::Spot) applyState(lights, *state);

		ImGui::PopID();
	}, osgx::imgui::makeSectionOptions(false, true));
#endif

	return viewer.run();
}
