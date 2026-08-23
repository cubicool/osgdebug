#!/usr/bin/env python3

import argparse
import json
import sys

def die(message):
	print(f"osggltf: {message}", file=sys.stderr)
	return 1

def display_name(value, fallback):
	return value if value else fallback

def flag(value):
	return "yes" if value else "no"

def load_summaries(args):
	try:
		import osgx
		gltf = osgx.gltf
	except (ImportError, AttributeError) as exc:
		raise RuntimeError(
			"could not import osgx.gltf; build osgx_python with OSGX_BUILD_GLTF "
			"and set PYTHONPATH to the build directory"
		) from exc

	return [
		gltf.inspect(model, load_images=args.load_images or args.command == "overview")
		for model in args.model
	]

def print_counts(summary):
	counts = summary["counts"]
	order = [
		"scenes",
		"nodes",
		"meshes",
		"materials",
		"textures",
		"images",
		"skins",
		"animations",
		"accessors",
		"bufferViews",
		"buffers",
	]

	for key in order:
		print(f"{key:12} {counts[key]}")

def command_summary(args, summaries):
	for index, summary in enumerate(summaries):
		if index:
			print()
			print("=" * 72)
			print()

		asset = summary["asset"]

		print(summary["path"])
		print()
		print("asset")
		print(f"  version    {asset['version']}")
		print(f"  generator  {asset['generator'] or '-'}")
		print()
		print("counts")
		print_counts(summary)
		print()
		print_intent_lines(summary)

def material_textures(material):
	pbr = material["pbrMetallicRoughness"]

	return (
		(pbr["baseColorTexture"], True),
		(pbr["metallicRoughnessTexture"], False),
		(material["normalTexture"], False),
		(material["occlusionTexture"], False),
		(material["emissiveTexture"], True),
	)

def texture_bytes(texture):
	return texture["imageWidth"] * texture["imageHeight"] * 4

def command_overview(args, summaries):
	for summary_index, summary in enumerate(summaries):
		if summary_index:
			print()
			print("=" * 72)
			print()

		primitives = [
			primitive
			for mesh in summary["meshes"]
			for primitive in mesh["primitives"]
		]
		positions = [
			primitive["attributes"]["POSITION"]["count"]
			for primitive in primitives
			if primitive["hasPosition"]
		]
		indices = [
			primitive["indices"]["count"]
			for primitive in primitives
			if primitive["indices"].get("valid")
		]
		texture_images = {}
		texture_variants = {}
		material_uses = {}

		for primitive in primitives:
			material_index = primitive["material"]

			if 0 <= material_index < len(summary["materials"]):
				material_uses[material_index] = material_uses.get(material_index, 0) + 1

		for material in summary["materials"]:
			for texture, srgb in material_textures(material):
				if texture is None or not texture["valid"]:
					continue

				source = texture["source"]
				texture_images[source] = texture
				texture_variants[(source, srgb)] = texture

		decoded_texture_bytes = sum(
			texture_bytes(texture)
			for texture in texture_images.values()
		)
		deduplicated_texture_bytes = sum(
			texture_bytes(texture)
			for texture in texture_variants.values()
		)
		undeduplicated_texture_bytes = sum(
			texture_bytes(texture) * material_uses.get(material_index, 0)
			for material_index, material in enumerate(summary["materials"])
			for texture, _ in material_textures(material)
			if texture is not None and texture["valid"]
		)
		max_texture_dimension = max(
			(
				max(texture["imageWidth"], texture["imageHeight"])
				for texture in texture_images.values()
			),
			default=0,
		)
		alpha_modes = {}

		for material in summary["materials"]:
			mode = material["alphaMode"]
			alpha_modes[mode] = alpha_modes.get(mode, 0) + 1

		print(summary["path"])
		print()
		print("rendering complexity")
		print(f"  primitives                 {len(primitives)}")
		print(f"  mesh nodes                 {sum(node['mesh'] >= 0 for node in summary['nodes'])}")
		print(f"  maximum node children      {max((len(node['children']) for node in summary['nodes']), default=0)}")
		print(f"  position vertices          {sum(positions):,}")
		print(f"  indices                    {sum(indices):,}")
		print(f"  primitives with tangents   {sum(primitive['hasTangent'] for primitive in primitives)}")
		print(
			"  primitives with UV1       "
			f"{sum('TEXCOORD_1' in primitive['attributes'] for primitive in primitives)}"
		)
		print(f"  material alpha modes       {', '.join(f'{mode}={count}' for mode, count in alpha_modes.items()) or '-'}")
		print()
		print("texture pressure")
		print(f"  referenced images          {len(texture_images)}")
		print(f"  texture color variants     {len(texture_variants)}")
		print(f"  largest decoded dimension  {max_texture_dimension}px")
		print(f"  tinygltf decoded sources   {decoded_texture_bytes / 1024 / 1024:.0f} MiB")
		print(f"  deduplicated OSG copies    {deduplicated_texture_bytes / 1024 / 1024:.0f} MiB")
		print(f"  estimated CPU peak         {(decoded_texture_bytes + deduplicated_texture_bytes) / 1024 / 1024:.0f} MiB")
		print(f"  GPU textures + mipmaps     {deduplicated_texture_bytes * 4 / 3 / 1024 / 1024:.0f} MiB")
		print(f"  no-dedup OSG copy risk     {undeduplicated_texture_bytes / 1024 / 1024:.0f} MiB")
		print("  note                       RGBA upper bounds; excludes geometry, render targets, driver copies, and allocator overhead")

def print_intent_lines(summary):
	intent = summary["intent"]

	print("intent")
	print(f"  skinning       {flag(intent['hasSkinning'])}")
	print(f"  animation      {flag(intent['hasAnimation'])}")
	print(f"  morph targets  {flag(intent['hasMorphTargets'])}")
	print(f"  pbr textures   {flag(intent['hasPBRTextures'])}")
	print(f"  spec-gloss     {flag(intent['hasSpecGloss'])}")

	if intent["hasSkinning"]:
		print()

		if intent["hasJoints0"] and intent["hasWeights0"] and summary["skins"]:
			print("This model appears to intend GPU skinning.")
		elif intent["hasJoints0"] or intent["hasWeights0"]:
			print("This model has partial skinning attributes; inspect meshes/skins carefully.")
		else:
			print("This model declares skins but no JOINTS_0/WEIGHTS_0 attributes were found.")

def command_intent(args, summaries):
	for index, summary in enumerate(summaries):
		if index:
			print()

		if len(summaries) > 1:
			print(summary["path"])

		print_intent_lines(summary)

def command_skins(args, summaries):
	for index, summary in enumerate(summaries):
		skins = summary["skins"]

		if index:
			print()

		if len(summaries) > 1:
			print(summary["path"])
			print()

		if not skins:
			print("No skins.")
			continue

		for skin in skins:
			name = display_name(skin["name"], f"skin[{skin['index']}]")
			ibm = skin["inverseBindMatrices"]

			print(f"{name}")
			print(f"  joints                  {skin['jointCount']}")
			print(f"  users                   {skin['userCount']}")
			print(f"  skeleton                {skin['skeleton']} {skin['skeletonName'] or ''}".rstrip())
			print(f"  inverseBindMatrices     accessor[{ibm['index']}]")
			print(f"  inverse bind count ok   {flag(skin['inverseBindMatricesMatchJointCount'])}")

			if skin["users"]:
				print("  used by")
				for user in skin["users"]:
					node_name = display_name(user["nodeName"], f"node[{user['node']}]")
					mesh_name = display_name(user.get("meshName"), f"mesh[{user['mesh']}]")
					print(f"    {node_name} -> {mesh_name}")

			print()

def command_animations(args, summaries):
	for index, summary in enumerate(summaries):
		animations = summary["animations"]

		if index:
			print()

		if len(summaries) > 1:
			print(summary["path"])
			print()

		if not animations:
			print("No animations.")
			continue

		for animation in animations:
			name = display_name(animation["name"], f"animation[{animation['index']}]")
			paths = ", ".join(animation["targetPaths"]) or "-"
			interpolations = ", ".join(animation["interpolations"]) or "-"

			print(f"{name}")
			print(f"  duration        {animation['duration']:.6g}s")
			print(f"  samplers        {animation['samplerCount']}")
			print(f"  channels        {animation['channelCount']}")
			print(f"  target paths    {paths}")
			print(f"  interpolation   {interpolations}")
			print(f"  morph targets   {flag(animation['hasMorphTargetAnimation'])}")
			print(f"  cubic spline    {flag(animation['hasCubicSpline'])}")

			unsupported = [
				channel
				for channel in animation["channels"]
				if not channel["supportedByCurrentLoader"]
			]

			if unsupported:
				print("  unsupported channels")
				for channel in unsupported:
					target = display_name(
						channel["targetNodeName"],
						f"node[{channel['targetNode']}]",
					)
					print(f"    {target}: {channel['targetPath']}")

			print()

def command_materials(args, summaries):
	for index, summary in enumerate(summaries):
		materials = summary["materials"]

		if index:
			print()

		if len(summaries) > 1:
			print(summary["path"])
			print()

		if not materials:
			print("No materials.")
			continue

		for material in materials:
			name = display_name(material["name"], f"material[{material['index']}]")
			pbr = material["pbrMetallicRoughness"]

			print(f"{name}")
			print(f"  baseColorFactor          {pbr['baseColorFactor']}")
			print(f"  metallicFactor           {pbr['metallicFactor']:.6g}")
			print(f"  roughnessFactor          {pbr['roughnessFactor']:.6g}")
			print(f"  baseColorMap             {flag(material['hasBaseColorMap'])}")
			print(f"  metallicRoughnessMap     {flag(material['hasMetallicRoughnessMap'])}")
			print(f"  normalMap                {flag(material['hasNormalMap'])}")
			print(f"  occlusionMap             {flag(material['hasOcclusionMap'])}")
			print(f"  emissiveMap              {flag(material['hasEmissiveMap'])}")
			print(f"  specGloss                {flag(material['hasSpecGloss'])}")
			print(f"  alphaMode                {material['alphaMode']}")
			print(f"  low roughness factor     {flag(material['usesLowRoughnessFactor'])}")
			print(f"  reflective from factors  {flag(material['likelyReflectiveFromFactors'])}")
			print()

def command_json(args, summaries):
	if len(summaries) == 1:
		print(json.dumps(summaries[0], indent=args.indent, sort_keys=True))
	else:
		print(json.dumps(summaries, indent=args.indent, sort_keys=True))

def command_tui(args, summaries):
	try:
		from rich import box
		from rich.table import Table
		from rich.text import Text
		from textual.app import App, ComposeResult
		from textual.containers import Horizontal, VerticalScroll
		from textual.widgets import Footer, Header, Static, Tree
	except ImportError as exc:
		raise RuntimeError(
			"could not import textual; install it for your user Python environment"
		) from exc

	class GLTFInspectorApp(App):
		ICONS = {
			"models": "🗂️",
			"model": "📦",
			"asset": "📄",
			"counts": "#",
			"intent": "✨",
			"scenes": "🎬",
			"scene": "🎬",
			"nodes": "🌳",
			"node": "📦",
			"joint node": "🦴",
			"mesh node": "🧊",
			"meshes": "🧊",
			"mesh": "🧊",
			"primitive": "△",
			"materials": "🎨",
			"material": "🎨",
			"skins": "🕸️",
			"skin": "🕸️",
			"joint": "🦴",
			"animations": "🎞️",
			"animation": "🎞️",
			"animation channel": "〰️",
		}

		CSS = """
		Screen {
			layout: vertical;
		}

		#body {
			height: 1fr;
		}

		#tree {
			width: 42%;
			min-width: 34;
			border: solid $accent;
		}

		#detail-scroll {
			width: 1fr;
			border: solid $panel;
		}

		#detail {
			padding: 1;
		}
		"""

		BINDINGS = [
			("q", "quit", "Quit"),
		]

		def __init__(self, models):
			super().__init__()
			self.models = models
			self.joint_node_indexes_by_model = [
				{
					joint["node"]
					for skin in model["skins"]
					for joint in skin["joints"]
				}
				for model in self.models
			]
			self.joint_node_indexes = set()

		def compose(self) -> ComposeResult:
			yield Header()
			with Horizontal(id="body"):
				yield self.build_tree()
				with VerticalScroll(id="detail-scroll"):
					if len(self.models) == 1:
						default_kind, default_value = "model", self.models[0]
					else:
						default_kind, default_value = "models", self.models

					yield Static(self.format_detail(default_kind, default_value), id="detail")
			yield Footer()

		def build_tree(self):
			if len(self.models) == 1:
				model = self.models[0]
				tree = Tree(self.icon_label("model", model["path"]), id="tree")
				tree.root.data = ("model", model)
				tree.root.expand()
				self.joint_node_indexes = self.joint_node_indexes_by_model[0]
				self.build_model_children(tree.root, model)

				return tree

			root_label = self.icon_label("models", f"{len(self.models)} models")
			tree = Tree(root_label, id="tree")
			tree.root.data = ("models", self.models)
			tree.root.expand()

			for index, model in enumerate(self.models):
				model_node = tree.root.add(
					self.icon_label("model", model["path"]),
					data=("model", model),
				)
				model_node.expand()
				self.joint_node_indexes = self.joint_node_indexes_by_model[index]
				self.build_model_children(model_node, model)

			return tree

		def build_model_children(self, parent, data):
			self.add_leaf(parent, "asset", data["asset"])
			self.add_leaf(parent, "counts", data["counts"])
			self.add_leaf(parent, "intent", data["intent"])

			scenes = self.add_group(parent, "scenes", data["scenes"])
			for scene in data["scenes"]:
				label = display_name(scene["name"], f"scene[{scene['index']}]")
				self.add_leaf(scenes, label, scene, "scene")

			nodes = self.add_group(parent, "nodes", data["nodes"])
			self.add_node_tree(nodes, data)

			meshes = self.add_group(parent, "meshes", data["meshes"])
			for mesh in data["meshes"]:
				label = display_name(mesh["name"], f"mesh[{mesh['index']}]")
				mesh_node = self.add_group(meshes, label, mesh, "mesh", mesh["primitives"])
				for primitive in mesh["primitives"]:
					bits = []
					if primitive["hasJoints0"]:
						bits.append("JOINTS_0")
					if primitive["hasWeights0"]:
						bits.append("WEIGHTS_0")
					suffix = f" ({', '.join(bits)})" if bits else ""
					self.add_leaf(
						mesh_node,
						f"primitive[{primitive['index']}]{suffix}",
						primitive,
						"primitive",
					)

			materials = self.add_group(parent, "materials", data["materials"])
			for material in data["materials"]:
				label = display_name(material["name"], f"material[{material['index']}]")
				self.add_leaf(materials, label, material, "material")

			skins = self.add_group(parent, "skins", data["skins"])
			for skin in data["skins"]:
				label = display_name(skin["name"], f"skin[{skin['index']}]")
				skin_node = self.add_group(skins, label, skin, "skin", skin["joints"])
				for joint in skin["joints"]:
					joint_label = display_name(joint["nodeName"], f"node[{joint['node']}]")
					self.add_leaf(
						skin_node,
						f"joint[{joint['index']}] {joint_label}",
						joint,
						"joint",
					)

			animations = self.add_group(
				parent,
				"animations",
				data["animations"],
			)
			for animation in data["animations"]:
				label = display_name(animation["name"], f"animation[{animation['index']}]")
				anim_node = self.add_group(
					animations,
					label,
					animation,
					"animation",
					animation["channels"],
				)
				for channel in animation["channels"]:
					target = display_name(
						channel["targetNodeName"],
						f"node[{channel['targetNode']}]",
					)
					self.add_leaf(
						anim_node,
						f"channel[{channel['index']}] {target}.{channel['targetPath']}",
						channel,
						"animation channel",
					)

		def add_group(self, parent, label, value, kind=None, children=None):
			kind = kind or label
			children = value if children is None else children
			label = self.icon_label(kind, label)

			if children:
				return parent.add(label, data=(kind, value))

			return parent.add_leaf(label, data=(kind, value))

		def add_leaf(self, parent, label, value, kind=None):
			kind = kind or label
			label = self.icon_label(kind, label)
			parent.add_leaf(label, data=(kind or label, value))

		def make_table(self, title):
			return Table(
				title=title,
				show_header=True,
				header_style="bold",
				box=box.SQUARE,
				show_lines=True,
			)

		def make_nested_table(self):
			return Table(
				show_header=True,
				header_style="bold",
				box=None,
				pad_edge=False,
			)

		def icon_label(self, kind, label):
			icon = self.ICONS.get(kind)
			if not icon:
				return label

			return f"{icon} {label}"

		def add_node_tree(self, parent, data):
			nodes_by_index = {node["index"]: node for node in data["nodes"]}
			child_indexes = {
				child
				for node in data["nodes"]
				for child in node["children"]
			}
			scene_roots = []

			for scene in data["scenes"]:
				scene_roots.extend(scene["nodes"])

			root_indexes = [
				index
				for index in scene_roots
				if index in nodes_by_index
			]

			if not root_indexes:
				root_indexes = [
					node["index"]
					for node in data["nodes"]
					if node["index"] not in child_indexes
				]

			visited = set()

			for index in root_indexes:
				self.add_node_branch(parent, nodes_by_index, index, visited)

			for node in data["nodes"]:
				if node["index"] not in visited:
					self.add_node_branch(parent, nodes_by_index, node["index"], visited)

		def add_node_branch(self, parent, nodes_by_index, index, visited):
			if index in visited:
				return

			node = nodes_by_index.get(index)
			if node is None:
				return

			display_kind = self.node_display_kind(node)
			label = self.node_label(node)
			children = [
				child
				for child in node["children"]
				if child in nodes_by_index and child not in visited
			]
			node_item = self.add_group(parent, label, node, display_kind, children)
			node_item.data = ("node", node)
			visited.add(index)

			for child in children:
				self.add_node_branch(node_item, nodes_by_index, child, visited)

		def node_label(self, node):
			label = display_name(node["name"], f"node[{node['index']}]")
			marker = ""
			if node["mesh"] >= 0:
				marker += f" mesh={node['mesh']}"
			if node["skin"] >= 0:
				marker += f" skin={node['skin']}"

			return f"{label}{marker}"

		def node_display_kind(self, node):
			if node["index"] in self.joint_node_indexes:
				return "joint node"

			if node["mesh"] >= 0:
				return "mesh node"

			return "node"

		def on_tree_node_selected(self, event: Tree.NodeSelected) -> None:
			self.show_node_detail(event.node)

		def on_tree_node_highlighted(self, event: Tree.NodeHighlighted) -> None:
			self.show_node_detail(event.node)

		def show_node_detail(self, node):
			detail = self.query_one("#detail", Static)
			kind, value = node.data or ("value", None)
			detail.update(self.format_detail(kind, value))

		def format_detail(self, kind, value):
			if value is None:
				return "No data."

			collection_kinds = {
				"scenes",
				"nodes",
				"meshes",
				"materials",
				"skins",
				"animations",
			}
			record_kinds = {
				"model",
				"scene",
				"node",
				"mesh",
				"primitive",
				"material",
				"skin",
				"joint",
				"animation",
				"animation channel",
			}

			if kind == "asset":
				table = self.make_table("asset")
				table.add_column("key")
				table.add_column("value")

				table.add_row("version", value["version"])
				table.add_row("minVersion", value["minVersion"] or "-")
				table.add_row("generator", value["generator"] or "-")
				table.add_row("copyright", value["copyright"] or "-")

				return table

			if kind == "counts":
				table = self.make_table("counts")
				table.add_column("field")
				table.add_column("count", justify="right")

				for key in [
					"scenes",
					"nodes",
					"meshes",
					"materials",
					"textures",
					"images",
					"skins",
					"animations",
					"accessors",
					"bufferViews",
					"buffers",
				]:
					table.add_row(key, str(value[key]))

				return table

			if kind == "intent":
				table = self.make_table("intent")
				table.add_column("signal")
				table.add_column("value")

				rows = [
					("skinning", value["hasSkinning"]),
					("animation", value["hasAnimation"]),
					("morph targets", value["hasMorphTargets"]),
					("pbr textures", value["hasPBRTextures"]),
					("spec-gloss", value["hasSpecGloss"]),
					("JOINTS_0", value["hasJoints0"]),
					("WEIGHTS_0", value["hasWeights0"]),
				]

				for label, enabled in rows:
					style = "green" if enabled else "dim"
					table.add_row(label, Text(flag(enabled), style=style))

				return table

			if kind == "models":
				return self.index_path_table(value)

			if kind == "model":
				return self.model_table(value)

			if kind in collection_kinds:
				return self.index_name_table(kind, value)

			if kind in record_kinds:
				return self.key_value_table(kind, value)

			if isinstance(value, list):
				return self.index_name_table(kind, value)

			if isinstance(value, dict):
				return self.key_value_table(kind, value)

			return f"{kind}\n\n{json.dumps(value, indent=2, sort_keys=True)}"

		def model_table(self, value):
			table = self.make_table("model")
			table.add_column("key")
			table.add_column("value")

			table.add_row("path", value["path"])
			table.add_row("version", value["asset"]["version"])
			table.add_row("generator", value["asset"]["generator"] or "-")

			for key in [
				"scenes",
				"nodes",
				"meshes",
				"materials",
				"skins",
				"animations",
			]:
				table.add_row(key, str(value["counts"][key]))

			return table

		def index_path_table(self, models):
			table = self.make_table("models")
			table.add_column("index", justify="right")
			table.add_column("path")

			for index, model in enumerate(models):
				table.add_row(str(index), model["path"])

			return table

		def index_name_table(self, title, items):
			table = self.make_table(title)
			table.add_column("index", justify="right")
			table.add_column("name")

			for item in items:
				index = item.get("index", "-")
				fallback = f"{title[:-1]}[{index}]"
				table.add_row(str(index), display_name(item.get("name"), fallback))

			return table

		def key_value_table(self, title, value):
			table = self.make_table(title)
			table.add_column("key")
			table.add_column("value")

			for key in sorted(value):
				table.add_row(key, self.format_table_value(value[key]))

			return table

		def dict_table(self, value):
			table = self.make_nested_table()
			table.add_column("key")
			table.add_column("value")

			for key in sorted(value):
				table.add_row(key, self.format_table_value(value[key]))

			return table

		def format_table_value(self, value):
			if value is None:
				return "-"

			if isinstance(value, bool):
				return flag(value)

			if isinstance(value, list):
				if not value:
					return "-"

				if all(isinstance(item, dict) for item in value):
					return f"{len(value)} items"

				return ", ".join(str(item) for item in value)

			if isinstance(value, dict):
				if not value:
					return "-"

				return self.dict_table(value)

			return str(value)

	GLTFInspectorApp(summaries).run()

def build_parser():
	parser = argparse.ArgumentParser(
		prog="osggltf",
		description="Inspect glTF/GLB model structure and inferred intent.",
	)

	parser.add_argument(
		"--load-images",
		action="store_true",
		help="let tinygltf decode images while inspecting",
	)

	subparsers = parser.add_subparsers(dest="command")

	for command in ["summary", "overview", "intent", "skins", "animations", "materials"]:
		sub = subparsers.add_parser(command)
		sub.add_argument("model", nargs="+")
		sub.set_defaults(func=globals()[f"command_{command}"])

	sub = subparsers.add_parser("json")
	sub.add_argument("model", nargs="+")
	sub.add_argument("--indent", type=int, default=2)
	sub.set_defaults(func=command_json)

	sub = subparsers.add_parser("tui")
	sub.add_argument("model", nargs="+")
	sub.set_defaults(func=command_tui)

	return parser

def main(argv=None):
	commands = {
		"summary",
		"overview",
		"intent",
		"skins",
		"animations",
		"materials",
		"json",
		"tui",
	}

	if argv is None:
		argv = sys.argv[1:]

	if argv and not argv[0].startswith("-") and argv[0] not in commands:
		argv = ["tui", *argv]

	parser = build_parser()
	args = parser.parse_args(argv)

	if not args.command:
		parser.print_help()
		return 2

	try:
		summaries = load_summaries(args)
		args.func(args, summaries)
	except Exception as exc:
		return die(str(exc))

	return 0

if __name__ == "__main__":
	raise SystemExit(main())
