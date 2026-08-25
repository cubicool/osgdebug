#vimrun! pytest -sv ../test/osgx_Material.py

import pytest

import osgx

from OpenSceneGraph import *


def test_material_defaults():
	m = osgx.Material()

	assert m.baseColor == osg.Vec4(1.0, 1.0, 1.0, 1.0)
	assert m.roughness == 1.0
	assert m.metallic == 1.0
	assert m.hasOcclusion is False

	assert m.baseColorMap is None
	assert m.normalMap is None
	assert m.metallicRoughnessMap is None
	assert m.emissiveMap is None


def test_material_factor_properties_round_trip():
	m = osgx.Material()

	m.baseColor = osg.Vec4(0.2, 0.4, 0.6, 1.0)
	m.roughness = 0.35
	m.metallic = 0.0
	m.hasOcclusion = True

	# roughness/metallic are C++ float, not double -- exact equality against a Python literal
	# would fail on the round trip through float32, same reason osgx_Shapes.py's geometry
	# checks use a tolerance rather than ==.
	assert m.baseColor == osg.Vec4(0.2, 0.4, 0.6, 1.0)
	assert m.roughness == pytest.approx(0.35)
	assert m.metallic == pytest.approx(0.0)
	assert m.hasOcclusion is True


def test_material_map_properties_round_trip_identity():
	# Setting a map preserves the EXACT osg.Texture2D identity, not a copy -- proves the
	# ref_ptr<Texture2D> holder round-trips through the property (see PBR.hpp/PBR.cpp), same
	# guarantee osgx-callbacks.cpp's SlotCache-backed proxies give for callback elements.
	m = osgx.Material()

	baseColorMap = osg.Texture2D()
	normalMap = osg.Texture2D()
	metallicRoughnessMap = osg.Texture2D()
	emissiveMap = osg.Texture2D()

	m.baseColorMap = baseColorMap
	m.normalMap = normalMap
	m.metallicRoughnessMap = metallicRoughnessMap
	m.emissiveMap = emissiveMap

	assert m.baseColorMap is baseColorMap
	assert m.normalMap is normalMap
	assert m.metallicRoughnessMap is metallicRoughnessMap
	assert m.emissiveMap is emissiveMap

	# Clearing a map (None -> nullptr ref_ptr) is a real, distinct state, not just "some texture".
	m.baseColorMap = None

	assert m.baseColorMap is None


def test_material_is_a_real_state_attribute():
	# The whole point of collapsing MaterialFactors/attachMaterialFactors() into osgx::Material
	# was making it a genuine osg::StateAttribute -- this is the cross-module inheritance proof:
	# osg.StateAttribute is bound in pyosg, osgx.Material is bound in a separate .so, and
	# isinstance() only works here because both modules share pybind11's process-wide type
	# registry at import time (see PBR.hpp's class comment / TODO.md's 2026-08-23 entry).
	m = osgx.Material()

	assert isinstance(m, osg.StateAttribute)

	# osgx::Material deliberately claims CAPABILITY (PBR.hpp's class comment explains why it
	# does not reuse an existing Type the way osgEarth's PBRTexture reuses TEXTURE).
	assert m.type == osg.StateAttribute.CAPABILITY


def test_material_attaches_to_a_state_set_and_round_trips_by_identity():
	# Real OSG StateSet storage, not a Python-side dict -- proves setAttributeAndModes()'s C++
	# side (osg::StateSet::_attributeList, keyed by TypeMemberPair) genuinely holds and returns
	# this exact object through its own ref_ptr<StateAttribute>, the same path real rendering
	# uses to find it during State::apply() (PBR.hpp/PBR.cpp's Material::apply()/compare()).
	m = osgx.Material()

	m.baseColor = osg.Vec4(0.1, 0.2, 0.3, 1.0)

	ss = osg.StateSet()

	ss.attributes[m.type] = m

	back = ss.attributes[m.type]

	assert back is m
	assert back.baseColor == osg.Vec4(0.1, 0.2, 0.3, 1.0)
