import osgx

from OpenSceneGraph import *

def test_node_callbacks_group_runs_all_in_order():
	# NodeCallbacksGroup runs each callback side by side (not chained), in list order --
	# the "usual" alternative to Callback.nestedCallbacks chaining, see osgx/Callbacks.hpp.
	# NodeLambdaCallback (Callbacks.hpp) isn't bound yet, so exercise the group with plain
	# osg.NodeCallback subclasses instead, driven through real C++ dispatch via
	# Node.accept(UpdateVisitor()) -- not a direct Python call (see test/osg_Callback.py's
	# test_direct_python_call_is_not_proof_of_real_dispatch for why that distinction matters).
	calls = []

	class Recording(osg.NodeCallback):
		def __init__(self, label):
			super().__init__()

			self.label = label

		def __call__(self, node, nv):
			calls.append(self.label)

			return True

	group = osgx.NodeCallbacksGroup([Recording("a"), Recording("b"), Recording("c")])

	n = osg.Node()

	n.updateCallback = group
	n.accept(osgUtil.UpdateVisitor())

	assert calls == ["a", "b", "c"]

def test_node_callbacks_group_add_remove():
	calls = []

	class Recording(osg.NodeCallback):
		def __init__(self, label):
			super().__init__()

			self.label = label

		def __call__(self, node, nv):
			calls.append(self.label)

			return True

	a, b = Recording("a"), Recording("b")
	group = osgx.NodeCallbacksGroup()

	group.add(a)
	group.add(b)

	n = osg.Node()

	n.updateCallback = group
	n.accept(osgUtil.UpdateVisitor())

	assert calls == ["a", "b"]

	calls.clear()
	group.remove(a)
	n.accept(osgUtil.UpdateVisitor())

	assert calls == ["b"]

def test_node_callbacks_group_callbacks_proxy_surface():
	# Direct coverage of the .callbacks SequenceProxy itself -- indexing, len, del, insert --
	# not just the add()/remove() convenience aliases exercised above.
	a, b, c = osg.NodeCallback(), osg.NodeCallback(), osg.NodeCallback()

	group = osgx.NodeCallbacksGroup([a, b, c])

	assert len(group.callbacks) == 3
	assert group.callbacks[0] is a
	assert group.callbacks[1] is b
	assert group.callbacks[2] is c

	del group.callbacks[1]

	assert len(group.callbacks) == 2
	assert group.callbacks[0] is a
	assert group.callbacks[1] is c

	d = osg.NodeCallback()

	group.callbacks.insert(1, d)

	assert [x for x in group.callbacks] == [a, d, c]

def test_node_callbacks_group_list_ctor_retains_identity_with_no_local_ref():
	# The regression this whole proxy rework exists for: an element passed INLINE in the
	# constructor's list literal, with no local Python variable retaining it, must keep its
	# Python-side identity (subclass, __dict__, trampoline override) alive for as long as it's
	# actually a member -- see osgx-callbacks.cpp's module comment. A plain ref_ptr alone
	# keeps the underlying C++ object alive but NOT the Python wrapper; this test specifically
	# proves the wrapper (and therefore the Python __call__ override) survives too.
	calls = []

	class Recording(osg.NodeCallback):
		def __call__(self, node, nv):
			calls.append("fired")

			return True

	group = osgx.NodeCallbacksGroup([Recording()]) # no local variable retains the Recording()

	n = osg.Node()

	n.updateCallback = group
	n.accept(osgUtil.UpdateVisitor())

	assert calls == ["fired"]

def test_camera_draw_callbacks_group_constructs():
	# DrawCallback needs a real draw traversal to actually invoke -- headless coverage here
	# is deliberately limited to construction/add/remove, not firing.
	cb0 = osg.Camera.DrawCallback()
	cb1 = osg.Camera.DrawCallback()

	group = osgx.CameraDrawCallbacksGroup([cb0, cb1])

	group.add(osg.Camera.DrawCallback())
	group.remove(cb0)

def test_drawable_draw_callbacks_group_constructs():
	cb0 = osg.Drawable.DrawCallback()
	cb1 = osg.Drawable.DrawCallback()

	group = osgx.DrawableDrawCallbacksGroup([cb0, cb1])

	group.add(osg.Drawable.DrawCallback())
	group.remove(cb0)
