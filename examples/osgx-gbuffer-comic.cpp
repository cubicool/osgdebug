// vimrun! ./examples/osgx-gbuffer-comic model.gltf
//
// A "comic book" cel-shaded rendering style built entirely on osgx::Hook::DeferredLighting: a
// caller-supplied fragment shader REPLACING PBRIBLLightingScene::create()'s entire lighting-pass
// main(), not one leaf function -- the "I know what I'm doing, rewrite the whole pipeline" escape
// hatch described in Shader.hpp's own Hook::DeferredLighting comment and
// PBRIBLLightingPassOptions' own comment (PBRIBL.hpp).
//
// The custom shader below never calls osgx_DirectLighting()/evaluateIBL() at all -- it reads the
// G-buffer via osgx_GetGBuffer() (#pragma osgx::gltf DEFERRED_LIGHTING_INPUTS, GET_GBUFFER --
// the same structured decode PBRIBLLightingScene::create()'s own built-in default uses internally)
// and renders: quantized diffuse bands with a warm-lit/cool-shadow color-graded tint; procedural
// cross-hatching (scale-calibrated from the loaded model's own bounding radius, no texture asset
// needed); a metal-aware specular highlight, anti-aliased via osgx_SpecularAA()
// (gb.roughness/gb.metallic -- this shader looks best on non-metallic/low-reflectivity materials,
// since it has no real reflections to fall back on for glossy metal); black ink outlines from
// G-buffer edge detection (comparing each fragment's view-space normal/depth against its
// 4-neighborhood -- the standard deferred-renderer silhouette/crease technique, no extra
// geometry pass needed); and a soft-clamped emissive glow. It DOES still gamma-encode its final
// output (a display-referred correctness requirement, not a style choice -- osgx_Tonemap() itself
// stays unused, see
// PBRIBLLightingPassOptions' own tonemap/hooks comment for why those are two separate decisions).
//
// No environment/IBL, no LightSet, no ShadowMap -- PBRIBLLightingScene::create()'s `environment`
// parameter is OPTIONAL specifically so a Hook::DeferredLighting override like this one, which
// never samples envMap/brdfLUT/diffuseEnv, doesn't have to pay for an HDR bake or KTX2 load that
// would go entirely unused (see that function's own comment, PBRIBL.hpp/.cpp). The light itself is
// a single plain `sunDirection` uniform, live-draggable via ImGui -- not a real osgx::LightSet.
//
// Deliberately skips osgx-gbuffer.cpp's shadow camera, floor, and G-buffer channel viewer. DOES
// include an ImGui panel (OSGX_IMGUI builds only) for live tuning of every knob below, same pattern
// osgx-gbuffer.cpp/osgx-shadow.cpp already use. See TODO.md's stylized-rendering entry for where
// this is headed next.
//
// NOT YET IMPLEMENTED -- "natural hatch noise" (design intent only, 2026-08-21, do not confuse
// with the domain-warping wood-grain effect discovered and removed the same night -- that one
// bends the PATTERN'S SHAPE via a large-amplitude coordinate warp; this is a completely different
// idea, keeping each line's straight geometry intact and randomizing individual line PROPERTIES
// instead, closer to how a human hand's ink hatching is imperfect):
//   1. Gaps/"cut-outs" along an individual hatch line -- interrupt a line's own continuity at
//      random points instead of drawing it unbroken end to end. Likely approach: hash a
//      position-along-the-line index (not just the stripe index hatchStripe() already computes)
//      and zero out the hatch value for some fraction of those segments.
//   2. Per-line thickness variation -- each stripe (by its own stripeIndex, same identifier
//      hatchStripe()'s removed blur feature used) gets a randomly perturbed thickness rather than
//      one uniform value for every line.
//   3. Irregular spacing between adjacent lines -- NOT expressible as a single sin(coord*frequency)
//      the way the current pattern is; needs either explicit per-stripe position accumulation
//      (walk stripe positions one at a time, each with its own randomized offset from where a
//      perfectly periodic grid would put it) or some other generation approach entirely. The
//      hardest of the three to retrofit onto the current sin()-based hatchStripe().
//   4. KNOWN ISSUE, confirmed live 2026-08-21, not yet fixed (deprioritized for that session):
//      `hatch *= mix(1.0, ao, aoMask)` DIMS hatch lines in occluded crevices rather than truly
//      ERASING them (the design intent aoMask's own comment describes) -- even with aoMask=1.0,
//      a partially-occluded pixel makes `hatch` a fractional value, which `shaded *= mix(1.0,
//      hatchDarken, hatch)` then renders as a lighter line, not no line. Needs a THRESHOLD/gate
//      on `ao` (e.g. `hatch *= smoothstep(loThreshold, hiThreshold, ao)`, both scaled by
//      aoMask) so a pixel below the cutoff drops to hatch=0 exactly, instead of a continuous
//      multiply that can never fully reach zero except where ao itself is exactly 0.
//
// NOTE on "AO Mask": now combines TWO genuinely different occlusion signals, multiplicatively --
// same convention the built-in lighting shader's own `mat.ao *= texture(aoTex, vUV).r;` uses
// (PBRIBL.cpp). `gb.ao` is the glTF material's BAKED occlusion texture value
// (osgx_gltf_GetMaterial()'s mat.ao, PBRIBL.cpp), defaulting to a flat 1.0 (fully unoccluded) when
// the loaded model has no dedicated occlusion texture -- CONFIRMED 2026-08-21 as why the slider
// once looked dead on Batman (a non-Khronos custom asset with no occlusion texture at all), not a
// pipeline bug: it works correctly on models that actually carry one, e.g. the Khronos
// `CompareAmbientOcclusion` sample (purpose-built for exactly this A/B) or `DamagedHelmet`. `aoTex`
// (gated behind `OSGX_PBRIBL_AO`, set automatically when `PBRIBLLightingPassOptions::aoTexture` is
// non-null) is real-time screen-space AO from `osgx::SSAO::create()` (GBuffer.hpp) -- catches
// self/contact occlusion the static bake can't (this model resting against itself, or against
// nothing else in this shadowless/lightless scratchpad, but the mechanism is general). Combining
// both into one `hatch *= mix(1.0, ao, aoMask);` mask means a crevice reads as fully occluded if
// EITHER signal says so, matching how real ink illustration treats "this area is dark" as one
// binary decision regardless of why it's dark.
// osgx-gbuffer.cpp's channel viewer still doesn't expose gb.ao (gAlbedo's alpha) as its own mode
// -- worth adding once/if it's useful for debugging which models have baked occlusion.

#include "osgx/Core.hpp"
#include "osgx/GBuffer.hpp"
#include "osgx/gltf/PBRIBL.hpp"
#include "osgx/ImGui.hpp"

OSGX_DISABLE_WARNINGS

#include <osg/ArgumentParser>
#include <osg/ComputeBoundsVisitor>
#include <osg/DisplaySettings>
#include <osg/Program>
#include <osg/Shader>
#include <osgDB/ReadFile>
#include <osgDB/Registry>
#include <osgGA/TrackballManipulator>
#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

OSGX_ENABLE_WARNINGS

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

constexpr int WIDTH = 1280;
constexpr int HEIGHT = 800;

std::filesystem::path findModelFile(std::string_view filename) {
	if(auto path = osgx::findDataFile(filename); !path.empty()) return path;

	const std::filesystem::path requested(filename);

	return osgx::findDataFile(
		requested.stem().string(), {"glTF-Sample-Assets/Models/{}/glTF/{}.gltf"}
	);
}

// Refreshes the lighting pass's view-matrix uniforms every frame -- same requirement as
// osgx-gbuffer.cpp's own UpdateLightingPassCallback, just as a lambda-driven NodeCallback here
// since there's no shadow camera in this scratchpad to hang it off of as a preDrawCallback.
// `ssaoProjection` mirrors osgx-gbuffer.cpp's own addition -- see osgx::SSAO::create()'s doc
// comment (GBuffer.hpp) for why SSAO needs its own freshly-updated forward-projection uniform.
class UpdateLightingPassCallback: public osg::Camera::DrawCallback {
public:
	UpdateLightingPassCallback(
		osgx::gltf::pbribl::PBRIBLLightingScene* scene,
		osg::Camera* mainCamera,
		osg::Uniform* ssaoProjection=nullptr
	):
		_scene(scene), _mainCamera(mainCamera), _ssaoProjection(ssaoProjection) {}

	void operator()(osg::RenderInfo&) const override {
		_scene->update(_mainCamera.get());

		if(_ssaoProjection.valid()) {
			_ssaoProjection->set(osg::Matrixf(_mainCamera->getProjectionMatrix()));
		}
	}

private:
	osgx::gltf::pbribl::PBRIBLLightingScene* _scene;
	osg::observer_ptr<osg::Camera> _mainCamera;
	osg::observer_ptr<osg::Uniform> _ssaoProjection;
};

// The osgx::Hook::DeferredLighting override itself. Requires nothing but the G-buffer contract --
// no LightSet, no ShadowMap, no IBL environment -- see the file-level comment above.
constexpr const char CUSTOM_DEFERRED_LIGHTING_FRAGMENT_SHADER[] = R"GLSL(
#version 460 core
#pragma osgx::gltf DEFERRED_LIGHTING_INPUTS, GET_GBUFFER
// osgx_SpecularAA() -- see its call site in main() below for why the specular highlight needs it.
#pragma osgx::pbr SPECULAR_AA

// World-space "sun" direction, live-draggable via ImGui -- NOT an osgx::LightSet (this
// scratchpad still has none), just a single plain uniform main() reads directly. Default matches
// the original hardcoded value: normalize(vec3(1, 1, 2)).
uniform vec3 sunDirection;
const int BANDS = 4;
// Classic comic/illustration color grading -- shadow areas tint slightly cool (as if lit by
// ambient sky/bounce light), lit areas tint slightly warm (as if lit by a warm key light). A
// small, cheap multiplicative color shift on top of the existing brightness bands; NOT a real
// ambient light source (no LightSet, see the file header), just a stylized tint.
const vec3 SHADOW_TINT = vec3(0.55, 0.65, 0.85);
const vec3 LIGHT_TINT = vec3(1.05, 1.0, 0.92);
// Soft-clamp exposure for emissive -- without osgx_Tonemap() at all in this shader, a raw HDR
// emissive value fed straight to the framebuffer can blow out past 1.0 and clip harshly. A
// simple Reinhard-style 1-exp(-x) curve keeps bright glow readable without a hard clip, without
// pulling in the full osgx_Tonemap() machinery this shader deliberately doesn't use.
const float EMISSIVE_EXPOSURE = 1.5;
const float EDGE_NORMAL_THRESHOLD = 0.4;
const float EDGE_DEPTH_THRESHOLD = 0.05;
// Outline width, in texels -- widening the neighbor-sample radius means any fragment within
// this many texels of a real discontinuity has a neighbor sample that crosses it, thickening
// the resulting ink line proportionally (not a separate blur/dilate pass).
const float EDGE_THICKNESS = 4.0;
// Hatch stripe spacing, in WORLD units (not screen pixels -- see hatchCoord's own comment in
// main() for why). WORLD units means this can't be one fixed constant across models of wildly
// different physical scale (confirmed live: DamagedHelmet vs. Fox vs. a unit Cube all needed
// completely different values at the same raw number) -- so this is now a uniform, set once at
// startup in main() from the loaded model's own bounding radius (--hatch-density scales it).
uniform float hatchFrequency;
// Line thickness (fraction of one stripe period) and darkening strength (1.0 = no darkening,
// i.e. an easy way to A/B hatching on/off without recompiling) -- also uniforms, also CLI-fed
// (--hatch-thickness/--hatch-darken), same reasoning as hatchFrequency above.
uniform float hatchThickness;
uniform float hatchDarken;
// Erases (masks) hatching in heavily-occluded crevices, 0..1 -- 0 leaves hatching exactly as
// shading-band density alone produces it (today's default); 1 fully masks hatch by gb.ao, so a
// deeply occluded crevice shows solid dark fill instead of crosshatch lines. This is the OPPOSITE
// of an earlier, removed experiment that ADDED density in occluded areas (which turned out
// invisible -- crevices were usually already at max density from shading band alone, so more
// density on top of already-maxed density changed nothing). Real ink illustration generally
// doesn't hatch the very darkest areas at all -- fine line detail is illegible there, so a solid
// fill reads better than more lines -- which is the reasoning this mask is modeling.
uniform float aoMask;

// Real-time SSAO -- see the file header's "NOTE on AO Mask" for how this combines with gb.ao.
// Gated the same way the built-in lighting shader gates it (OSGX_PBRIBL_AO, set automatically by
// PBRIBLLightingPassOptions::aoTexture when non-null) -- so this shader still compiles/runs
// unchanged if no SSAO pass is ever wired in. `#pragma import_defines` IS required here (confirmed
// against OSG's own osg::Shader::_shaderDefines/getDefineString(): a StateSet define is only
// written into a given Shader's compiled source if that Shader itself declares the define name via
// import_defines -- otherwise `#ifdef OSGX_PBRIBL_AO` is always false regardless of what the
// StateSet has set). Same reasoning LIGHTING_FRAGMENT_SHADER_SRC's own
// `#pragma import_defines ( OSGX_PBRIBL_DIAGNOSTICS, OSGX_PBRIBL_NO_TONEMAP, OSGX_PBRIBL_AO )` line
// exists (PBRIBL.cpp) -- every shader that reads a StateSet-driven define needs its own copy of
// this pragma, not just one shader in the Program.
#pragma import_defines ( OSGX_PBRIBL_AO )
#ifdef OSGX_PBRIBL_AO
uniform sampler2D aoTex;
#endif

// One diagonal stripe pattern over whatever 2D coordinate space `p` is given (world-space in
// main() below, see hatchCoord's own comment for why) -- 1.0 in a thin band once per period
// (where an ink line falls), 0.0 everywhere else. `angleDeg` rotates the stripe direction; two
// calls at +45/-45 give a crosshatch. Pure procedural math, no texture asset -- see the file
// header's "approach #1" note on hatching feasibility.
float hatchStripe(vec2 p, float angleDeg, float frequency, float thickness) {
	float a = radians(angleDeg);
	float coord = p.x * cos(a) + p.y * sin(a);

	return smoothstep(1.0 - thickness, 1.0, sin(coord * frequency));
}

// Ink-outline test: compares this fragment's own view-space normal/depth (N0/depth0) against its
// 4-neighborhood in the G-buffer -- flags a silhouette (neighbor is background) or a sharp crease
// (neighbor's normal/depth diverges past threshold). Pure screen-space post-process, no extra
// geometry pass -- the same shape the moodboard's own "EDGE DETECTION" pipeline-inspector panel
// showed. `depth0` is view-space Z (negative, camera looking down -Z), so the depth threshold
// scales with distance from camera rather than being one fixed world-space epsilon.
float detectEdge(vec2 uv, vec3 N0, float depth0) {
	vec2 texel = EDGE_THICKNESS / vec2(textureSize(gNormal, 0));
	vec2 offsets[4] = vec2[](
		vec2(texel.x, 0.0), vec2(-texel.x, 0.0), vec2(0.0, texel.y), vec2(0.0, -texel.y)
	);
	float edge = 0.0;

	for(int i = 0; i < 4; i++) {
		vec3 N = texture(gNormal, uv + offsets[i]).rgb;

		if(dot(N, N) < 0.0001) { edge = 1.0; continue; } // neighbor is background -> silhouette

		float depth = texture(gPosition, uv + offsets[i]).z;

		edge = max(edge, step(EDGE_NORMAL_THRESHOLD, 1.0 - dot(N0, normalize(N))));
		edge = max(edge, step(EDGE_DEPTH_THRESHOLD * abs(depth0), abs(depth0 - depth)));
	}

	return edge;
}

void main() {
	osgx_GBuffer gb = osgx_GetGBuffer(vUV);

	// Same "cleared-but-never-written pixel" background test the built-in default uses.
	if(dot(gb.normal, gb.normal) < 0.0001) discard;

	vec3 N_view_n = normalize(gb.normal);
	mat3 invView = transpose(mat3(osgx_mainViewMatrix));
	vec3 N = invView * N_view_n;
	vec3 V = normalize(invView * normalize(-gb.position));

	// Quantized "toon" diffuse band -- no osgx_DirectLighting(), no evaluateIBL(), no
	// osgx_Tonemap() anywhere in this file. Proves the override really does replace the whole
	// pass rather than layering onto the built-in.
	float NdotL = max(dot(N, sunDirection), 0.0);
	float band = ceil(NdotL * float(BANDS)) / float(BANDS);
	vec3 shaded = gb.albedo * mix(0.25, 1.0, band) * mix(SHADOW_TINT, LIGHT_TINT, band);

	// Cross-hatch, applied to the base diffuse term ONLY -- deliberately computed and multiplied
	// in BEFORE the rim/specular accents added below, so those stay clean, unhatched "highlight"
	// accents instead of getting muddied by ink lines. `hatchAmount` mirrors real ink-hatching
	// density logic: the brightest band gets none, the darkest gets both stripe directions
	// crossed, everything between gets a single direction.
	//
	// hatchCoord is WORLD-space, not screen-space (gl_FragCoord) -- screen-space was the first
	// attempt, and it looked wrong: the pattern stayed locked to the viewport instead of the
	// surface, visibly sliding across the model as the camera orbited (a decal stuck to the
	// monitor, not painted on the object). World position is genuinely tied to the physical
	// surface point, so the pattern now stays put as the camera moves. Which 2D plane to project
	// onto (XY/XZ/YZ) is picked per-fragment by whichever world axis the surface normal points
	// along LEAST -- a cheap "axis-dominant" trick (not full smooth-blended triplanar) that avoids
	// the worst stretching a single fixed plane would cause on faces nearly perpendicular to it
	// (e.g. the helmet's curved dome, mostly Z-facing, would smear badly under a pure XZ or YZ
	// projection).
	vec3 worldPos = (osgx_mainViewMatrixInverse * vec4(gb.position, 1.0)).xyz;
	vec3 absN = abs(N);
	vec2 hatchCoord = (absN.z >= absN.x && absN.z >= absN.y)
		? worldPos.xy
		: (absN.x >= absN.y ? worldPos.yz : worldPos.xz)
	;
	float hatchAmount = 1.0 - band;
	float hatch = hatchStripe(hatchCoord, 45.0, hatchFrequency, hatchThickness) *
		step(0.01, hatchAmount);

	hatch = max(hatch, hatchStripe(hatchCoord, -45.0, hatchFrequency, hatchThickness) *
		step(0.6, hatchAmount));
	// Combined occlusion: baked (gb.ao) * real-time SSAO (aoTex, when wired in) -- same
	// multiplicative convention the built-in lighting shader uses for the identical combination.
	// See aoMask's own comment above -- this ERASES hatch coverage in occluded crevices rather
	// than adding more, so those areas fall through to the (already-darkened-by-shading-band)
	// base color as a solid fill instead of extra crosshatch lines.
	float ao = gb.ao;

#ifdef OSGX_PBRIBL_AO
	ao *= texture(aoTex, vUV).r;
#endif

	hatch *= mix(1.0, ao, aoMask);
	shaded *= mix(1.0, hatchDarken, hatch);

	float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0);

	shaded += gb.albedo * rim * 0.4;

	// Metal-aware "comic shine" -- gb.roughness/gb.metallic. Roughness controls the highlight's
	// TIGHTNESS (a Blinn-Phong exponent, not a physically exact GGX-to-Phong conversion -- this is
	// a stylized approximation, not a PBR one); metallic controls its COLOR -- bare metal tints
	// its highlight by the surface's own albedo (metals have no separate "white" specular layer),
	// while a dielectric surface gets a neutral white one. Two smoothstep-thresholded tiers (a
	// small bright core plus a wider, dimmer rim) instead of a smooth falloff -- a hard-edged
	// "graphic" highlight shape matches the flat quantized bands far better than a photorealistic
	// soft glow would.
	//
	// osgx_SpecularAA(N_view_n, gb.roughness) -- NOT optional, confirmed live: at a low roughness
	// (high specPower), pow(NdotH, specPower) is extremely sensitive to the tiny per-pixel normal
	// variation a normal map introduces, and the narrow smoothstep thresholds below then amplify
	// that into scattered "boxy" aliased blotches instead of one smooth highlight, worst on
	// heavily normal-mapped metallic surfaces. The built-in PBR path (PBR.hpp/PBRIBL.cpp) already
	// calls this for exactly this reason; this shader just wasn't calling it until now.
	float aaRoughness = osgx_SpecularAA(N_view_n, gb.roughness);
	float specPower = mix(128.0, 4.0, aaRoughness);
	vec3 H = normalize(sunDirection + V);
	float spec = pow(max(dot(N, H), 0.0), specPower);
	float specCore = smoothstep(0.35, 0.6, spec);
	float specRim = smoothstep(0.08, 0.3, spec) * 0.4;
	vec3 specColor = mix(vec3(1.0), gb.albedo, gb.metallic);

	shaded += specColor * max(specCore, specRim);

	float edge = detectEdge(vUV, N_view_n, gb.position.z);

	shaded = mix(shaded, vec3(0.02), edge);

	// Soft-clamped emissive (see EMISSIVE_EXPOSURE's own comment), combined BEFORE the gamma
	// encode below -- both the shaded surface and the glow need the same linear-to-display
	// conversion, same as the built-in default's own color = surface + direct + emissive;
	// color = osgx_Tonemap(color); ordering (PBRIBL.cpp), just without the tonemap curve itself.
	vec3 color = shaded + (vec3(1.0) - exp(-gb.emissive * EMISSIVE_EXPOSURE));

	// This shader deliberately never calls osgx_Tonemap() (no tone CURVE -- see the file header),
	// but still needs the gamma ENCODE step every display-referred fragment needs before writing
	// to the backbuffer -- omitting it (as this shader did until now) makes midtones read
	// noticeably darker/muddier than intended. Two separate decisions, same as
	// PBRIBLLightingPassOptions::tonemap's own comment explains for the built-in path.
	color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));

	fragColor = vec4(color, gb.alphaCoverage);
}
)GLSL";

}

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);

	args.getApplicationUsage()->setCommandLineUsage(
		std::string(args.getApplicationName()) + " <model.gltf> [--samples <count>] [hatch options]"
	);
	args.getApplicationUsage()->addCommandLineOption(
		"--samples <count>", "Request this many default-framebuffer MSAA samples (default: 4)"
	);
	args.getApplicationUsage()->addCommandLineOption(
		"--hatch-density <multiplier>",
		"Scales the hatch line frequency (default: 6.0). The auto-derived default (from the "
		"loaded model's own bounding radius) should look reasonable on any model size without "
		"this; use it to go denser/sparser to taste."
	);
	args.getApplicationUsage()->addCommandLineOption(
		"--hatch-thickness <fraction>",
		"Line thickness as a fraction of one stripe period, 0..1 (default: 0.18)."
	);
	args.getApplicationUsage()->addCommandLineOption(
		"--hatch-darken <factor>",
		"How much a hatch line darkens the surface under it, 0..1 (default: 0.55). 1.0 disables "
		"the visible effect entirely -- a quick way to A/B hatching on/off without recompiling."
	);
	args.getApplicationUsage()->addCommandLineOption(
		"--ao-mask <strength>",
		"Erases hatching in occluded crevices by ambient occlusion (baked material AO combined "
		"with real-time SSAO), 0..1 (default: 0.5). 0 leaves hatching exactly as shading-band "
		"density alone produces it; 1 fully masks by AO."
	);
	int samples = 4;
	float hatchDensity = 6.0f;
	float hatchThickness = 0.18f;
	float hatchDarken = 0.55f;
	float aoMask = 0.5f;

	args.read("--samples", samples);
	args.read("--hatch-density", hatchDensity);
	args.read("--hatch-thickness", hatchThickness);
	args.read("--hatch-darken", hatchDarken);
	args.read("--ao-mask", aoMask);

	if(args.argc() < 2 || samples < 0) {
		args.getApplicationUsage()->write(std::cerr);

		return 1;
	}

	osg::DisplaySettings::instance()->setNumMultiSamples(static_cast<unsigned int>(samples));
	osgViewer::Viewer viewer(args);

	// Dear ImGui's single global context isn't safe to touch from more than one OSG draw thread --
	// see osgx::imgui::Widget's own class comment; harmless to set unconditionally even when
	// OSGX_IMGUI is off.
	viewer.setThreadingModel(osgViewer::Viewer::SingleThreaded);
	viewer.setUpViewInWindow(50, 50, WIDTH, HEIGHT);

	osgDB::Registry::instance()->addFileExtensionAlias("glb", "gltf");

	const auto modelPath = findModelFile(args[1]);
	osg::ref_ptr<osg::Node> model;

	if(!modelPath.empty()) model = osgDB::readRefNodeFile(modelPath.string());

	if(!model) {
		std::cerr << "Failed to load: " << args[1] << std::endl;

		return 1;
	}

	// Target hatch stripes-per-bounding-radius, regardless of the loaded model's real-world
	// scale -- confirmed live that a single fixed world-space frequency looks wildly different
	// (huge on DamagedHelmet, invisible on Fox) across differently-scaled assets.
	// --hatch-density scales this multiplicatively for further to-taste tuning.
	//
	// The 197 below is back-solved, not guessed: the very first world-space attempt used a FIXED
	// HATCH_FREQUENCY=120 and looked genuinely good on DamagedHelmet before per-model scale
	// differences were noticed. That model's own boundRadius, measured live, is 1.6446 -- so the
	// target this formula needs to reproduce that exact good-looking result is K = 120 * 1.6446 =
	// 197.35. K stays constant relative to boundRadius by construction, so this reproduces the
	// SAME visual density on every model, Fox's 53x larger boundRadius included.
	osg::ComputeBoundsVisitor boundsVisitor;

	model->accept(boundsVisitor);

	const auto& bounds = boundsVisitor.getBoundingBox();
	const float boundRadius = bounds.valid() ? bounds.radius() : 1.0f;
	// Shared by the initial CLI-driven value below AND the ImGui density slider further down --
	// one formula, not two copies that could drift.
	auto hatchFrequencyFor = [boundRadius](float density) {
		return density * 197.35f / (boundRadius > 0.001f ? boundRadius : 0.001f);
	};
	const float hatchFrequency = hatchFrequencyFor(hatchDensity);
	// Live-draggable via ImGui below -- default matches the shader's own original hardcoded
	// value, normalize(vec3(1, 1, 2)).
	osg::Vec3 sunDirection(0.4082483f, 0.4082483f, 0.8164966f);

	// No HDR/manifest environment loaded here at all -- this style never samples IBL, and
	// PBRIBLLightingScene::create()'s `environment` parameter is optional specifically for
	// callers like this one (see that function's own comment). Passing a default-constructed
	// (invalid) PBRIBLEnvironment skips the envMap/brdfLUT/diffuseEnv binding entirely rather
	// than forcing an HDR bake or KTX2 load purely to populate textures nothing samples.
	osgx::gltf::pbribl::PBRIBLEnvironment environment;
	auto gbuffer = osgx::gltf::pbribl::PBRIBLGBuffer::create(model, WIDTH, HEIGHT);

	if(!gbuffer.valid()) {
		std::cerr << "Failed to build the G-buffer geometry pass" << std::endl;

		return 1;
	}

	// SSAO: built before lightingOptions/PBRIBLLightingScene::create() specifically so its
	// aoTexture can be set on lightingOptions normally, rather than the "wire it in by hand
	// afterward" workaround an SSAO-after-the-lighting-pass ordering would need (see
	// PBRIBLLightingPassOptions::aoTexture's own comment, PBRIBL.hpp, for how that seam expects to
	// be used). Reads gbuffer's normal/position directly -- both already exist once the geometry
	// pass above is built. Radius scaled off the model's own bound, same "derive from the model's
	// own bounds" precedent hatchFrequencyFor() above already uses -- a fixed radius tuned for one
	// model looks wrong at a very different scale.
	auto ssaoProjection = osgx::make_ref<osg::Uniform>(
		"projectionMatrix", osg::Matrixf::identity()
	);
	const float ssaoRadius = std::max(0.05f, boundRadius * 0.15f);

	auto ssao = osgx::SSAO::create(
		gbuffer.normalTexture, gbuffer.positionTexture, ssaoProjection.get(), WIDTH, HEIGHT, ssaoRadius
	);

	if(!ssao.valid()) {
		std::cerr << "Failed to build the SSAO pass" << std::endl;

		return 1;
	}

	osgx::gltf::pbribl::PBRIBLLightingPassOptions lightingOptions;

	// The seam CUSTOM_DEFERRED_LIGHTING_FRAGMENT_SHADER's own `#ifdef OSGX_PBRIBL_AO` block reads
	// -- setting this here (same as any other PBRIBLLightingPassOptions consumer) is what makes
	// PBRIBLLightingScene::create() bind aoTex/unit 8/OSGX_PBRIBL_AO on the StateSet at all; the
	// custom shader still has to declare and sample `aoTex` itself, since Hook::DeferredLighting
	// REPLACES main() rather than inheriting the built-in's own declarations.
	lightingOptions.aoTexture = ssao.aoTexture.get();

	lightingOptions.hooks = {{
		osgx::Hook::DeferredLighting,
		new osg::Shader(
			osg::Shader::FRAGMENT,
			// resolveShaderLibs() is what expands `#pragma osgx::gltf DEFERRED_LIGHTING_INPUTS,
			// GET_GBUFFER` above into real GLSL -- passing raw, un-expanded source straight to
			// osg::Shader() leaves the literal `#pragma` line in place, which the driver then
			// chokes on (and every symbol the pragma was supposed to declare -- gb,
			// osgx_mainViewMatrix, fragColor -- comes back "undefined").
			osgx::gltf::pbribl::resolveShaderLibs(CUSTOM_DEFERRED_LIGHTING_FRAGMENT_SHADER)
		)
	}};

	auto lighting = osgx::gltf::pbribl::PBRIBLLightingScene::create(
		gbuffer, environment, viewer.getCamera(), 1.0f, 1.0f, lightingOptions
	);

	if(!lighting.valid()) {
		std::cerr << "Failed to build the lighting pass" << std::endl;

		return 1;
	}

	// hatchFrequency/hatchThickness/hatchDarken/aoMask/sunDirection all live on the lighting pass's
	// own StateSet -- that's where CUSTOM_DEFERRED_LIGHTING_FRAGMENT_SHADER's `uniform`
	// declarations expect to find them, same as any other uniform PBRIBLLightingScene::create()
	// itself wires up (osgx_mainViewMatrix, etc.). Kept as named pointers (not fire-and-forget) so
	// the ImGui section below can push live edits into the SAME osg::Uniform objects.
	auto* lightingSS = dynamic_cast<osg::Camera*>(lighting.node.get())->getOrCreateStateSet();
	auto* hatchFrequencyUniform = new osg::Uniform("hatchFrequency", hatchFrequency);
	auto* hatchThicknessUniform = new osg::Uniform("hatchThickness", hatchThickness);
	auto* hatchDarkenUniform = new osg::Uniform("hatchDarken", hatchDarken);
	auto* aoMaskUniform = new osg::Uniform("aoMask", aoMask);
	auto* sunDirectionUniform = new osg::Uniform("sunDirection", sunDirection);

	lightingSS->addUniform(hatchFrequencyUniform);
	lightingSS->addUniform(hatchThicknessUniform);
	lightingSS->addUniform(hatchDarkenUniform);
	lightingSS->addUniform(aoMaskUniform);
	lightingSS->addUniform(sunDirectionUniform);

	auto root = osgx::make_ref<osg::Group>();

	// gbuffer.gbuffer.camera is the FIRST PRE_RENDER camera in this scene graph (added before
	// lighting.node below) -- see UpdateLightingPassCallback's own comment (and
	// PBRIBLLightingScene::create()'s) for why the update() call has to land here, not on
	// lighting.node's own preDrawCallback or as a post-frame() application call.
	gbuffer.gbuffer.camera->setPreDrawCallback(
		new UpdateLightingPassCallback(&lighting, viewer.getCamera(), ssaoProjection.get())
	);

	// Add order matters: gbuffer.gbuffer.camera, ssao.rawCamera, and ssao.blurCamera are all
	// PRE_RENDER at the same default order number (0), so OSG breaks the tie by scene-graph add
	// order -- ssao.rawCamera/blurCamera MUST come after gbuffer.gbuffer.camera (they read its
	// normal/position output) and before lighting.node (which reads ssao.aoTexture back via
	// aoTex, wired above through lightingOptions.aoTexture).
	root->addChild(gbuffer.gbuffer.camera);
	root->addChild(ssao.rawCamera);
	root->addChild(ssao.blurCamera);
	root->addChild(lighting.node);

	viewer.setSceneData(root);
	viewer.setCameraManipulator(new osgGA::TrackballManipulator());
	viewer.addEventHandler(new osgViewer::StatsHandler());
	// viewer.getCamera()->setClearColor(osg::Vec4f(25.0f / 255.0f, 50.0f / 255.0f, 75.0f / 255.0f, 1.0f));
	viewer.getCamera()->setClearColor(osg::Vec4f(180.0f / 255.0f, 180.0f / 255.0f, 180.0f / 255.0f, 1.0f));

	std::cout
		<< "osgx-gbuffer-comic: osgx::Hook::DeferredLighting comic-style shading -- quantized bands, "
		<< "noisy hatching, ink outlines, no lights/shadow/IBL evaluation" << std::endl
		<< " AO Mask now combines baked material AO with real-time osgx::SSAO" << std::endl
	;

#ifdef OSGX_IMGUI
	// drawCamera pinned to lighting.node's own Camera explicitly -- lighting.node is a nested
	// POST_RENDER camera (a scene-graph child of root, not a viewer slave), and it draws AFTER
	// the master camera's own PostDrawCallback fires. Left at the default (drawCamera=nullptr),
	// the panel would draw via the master camera and then get painted over every frame -- same
	// gotcha osgx-gbuffer.cpp's own Widget setup documents at its own call site.
	auto* gui = new osgx::imgui::Widget(viewer, dynamic_cast<osg::Camera*>(lighting.node.get()));

	gui->addSection("Comic Style", [
		hatchFrequencyFor, &hatchDensity, &hatchThickness, &hatchDarken, &aoMask, &sunDirection,
		hatchFrequencyUniform, hatchThicknessUniform, hatchDarkenUniform, aoMaskUniform,
		sunDirectionUniform
	](osg::RenderInfo&) {
		if(ImGui::SliderFloat("Hatch Density", &hatchDensity, 0.1f, 10.0f)) {
			hatchFrequencyUniform->set(hatchFrequencyFor(hatchDensity));
		}

		if(ImGui::SliderFloat("Hatch Thickness", &hatchThickness, 0.0f, 0.5f)) {
			hatchThicknessUniform->set(hatchThickness);
		}

		if(ImGui::SliderFloat("Hatch Darken", &hatchDarken, 0.0f, 1.0f)) {
			hatchDarkenUniform->set(hatchDarken);
		}

		if(ImGui::SliderFloat("AO Mask", &aoMask, 0.0f, 1.0f)) {
			aoMaskUniform->set(aoMask);
		}

		// A dragged slider can pass through (0,0,0), which normalizes to NaN -- same guard
		// osgx-gbuffer.cpp/osgx-shadow.cpp's own light-drag sections use: fall back to a fixed
		// safe direction for the UNIFORM specifically, without resetting the slider's own
		// displayed (possibly still-degenerate) value.
		if(ImGui::SliderFloat3("Light Direction", sunDirection.ptr(), -1.0f, 1.0f)) {
			if(sunDirection.length2() > 1e-8f) {
				osg::Vec3 normalized = sunDirection;

				normalized.normalize();
				sunDirectionUniform->set(normalized);
			}

			else { sunDirectionUniform->set(osg::Vec3(0.0f, 0.0f, -1.0f)); }
		}
	}, osgx::imgui::SectionOptions::create(false, true));

	// Live radius/bias tuning -- same shape osgx-gbuffer.cpp's own "SSAO" section uses, reading
	// the CURRENT uniform value each frame rather than tracking a separate local float, since
	// osgx::SSAO::create() already returns these as real osg::Uniform*s meant to be set at any
	// time (no pass rebuild).
	gui->addSection("SSAO", [ssao](osg::RenderInfo&) {
		float radius = 0.0f, bias = 0.0f;

		ssao.radius->get(radius);
		ssao.bias->get(bias);

		bool changed = false;

		changed |= ImGui::SliderFloat("Radius", &radius, 0.01f, 2.0f);
		changed |= ImGui::SliderFloat("Bias", &bias, 0.0f, 0.1f);

		if(changed) {
			ssao.radius->set(radius);
			ssao.bias->set(bias);
		}
	}, osgx::imgui::SectionOptions::create(false, true));
#endif

	return viewer.run();
}
