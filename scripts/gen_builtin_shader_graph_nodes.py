import json
import os

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = os.path.join(REPO, "assets", "shader_graph_nodes")


def write(name, obj):
    path = os.path.join(BASE, name)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(obj, f, indent=2)
        f.write("\n")


def node(fid, ek, ins, outs, tpl=None):
    o: dict = {
        "format": "kaleido_shader_graph_node",
        "id": fid,
        "version": 1,
        "evalKind": ek,
        "inputs": ins,
        "outputs": outs,
    }
    if tpl:
        o["glslTemplate"] = tpl
    return o


def pi(i, name, t, opt=False):
    d = {"id": i, "name": name, "type": t}
    if opt:
        d["optional"] = True
    return d


def po(i, name, t):
    return {"id": i, "name": name, "type": t}


def main():
    os.makedirs(BASE, exist_ok=True)
    write(
        "builtin_input_uv.json",
        node(
            "builtin/input/uv",
            "InputUV",
            [],
            [po(1, "u", "float"), po(2, "v", "float"), po(3, "uv", "vec2")],
        ),
    )
    write("builtin_input_time.json", node("builtin/input/time", "InputTime", [], [po(1, "out", "float")]))
    write("builtin_input_world_pos.json", node("builtin/input/world_pos", "InputWorldPos", [], [po(1, "out", "vec3")]))
    write("builtin_input_normal.json", node("builtin/input/normal", "InputNormal", [], [po(1, "out", "vec3")]))
    write(
        "builtin_const_float.json",
        node("builtin/const/float", "ConstFloat", [], [po(1, "out", "float")], "    float {{OUT}} = {{V0}};\n"),
    )
    write("builtin_const_vec2.json", node("builtin/const/vec2", "ConstVec2", [], [po(1, "out", "vec2")]))
    write("builtin_const_vec3.json", node("builtin/const/vec3", "ConstVec3", [], [po(1, "out", "vec3")]))
    write("builtin_param_float.json", node("builtin/param/float", "ParamFloat", [], [po(1, "out", "float")]))
    write(
        "builtin_math_sub.json",
        node("builtin/math/sub", "Sub", [pi(1, "a", "float"), pi(2, "b", "float")], [po(1, "out", "float")]),
    )
    write(
        "builtin_math_mul.json",
        node("builtin/math/mul", "Mul", [pi(1, "a", "float"), pi(2, "b", "float")], [po(1, "out", "float")]),
    )
    write(
        "builtin_math_div.json",
        node("builtin/math/div", "Div", [pi(1, "a", "float"), pi(2, "b", "float")], [po(1, "out", "float")]),
    )
    for ek in ("Sin", "Cos", "Frac", "Saturate"):
        lid = "builtin/math/" + ek.lower()
        write(
            "builtin_math_" + ek.lower() + ".json",
            node(lid, ek, [pi(1, "in", "float")], [po(1, "out", "float")]),
        )
    write(
        "builtin_math_lerp.json",
        node(
            "builtin/math/lerp",
            "Lerp",
            [pi(1, "a", "float"), pi(2, "b", "float"), pi(3, "t", "float")],
            [po(1, "out", "float")],
        ),
    )
    write(
        "builtin_vector_compose_vec3.json",
        node(
            "builtin/vector/compose_vec3",
            "ComposeVec3",
            [pi(1, "x", "float"), pi(2, "y", "float"), pi(3, "z", "float")],
            [po(1, "out", "vec3")],
        ),
    )
    for ax, op in (("x", "SplitVec3X"), ("y", "SplitVec3Y"), ("z", "SplitVec3Z")):
        lid = "builtin/vector/split_vec3_" + ax
        write(
            "builtin_vector_split_vec3_" + ax + ".json",
            node(lid, op, [pi(1, "vec3", "vec3")], [po(1, "out", "float")]),
        )
    write(
        "builtin_noise_perlin3d.json",
        node("builtin/noise/perlin3d", "NoisePerlin3D", [pi(1, "p", "vec3")], [po(1, "out", "float")]),
    )
    write(
        "builtin_math_remap.json",
        node(
            "builtin/math/remap",
            "Remap",
            [
                pi(1, "value", "float"),
                pi(2, "inMin", "float", True),
                pi(3, "inMax", "float", True),
                pi(4, "outMin", "float", True),
                pi(5, "outMax", "float", True),
            ],
            [po(1, "out", "float")],
        ),
    )
    write(
        "builtin_output_surface.json",
        node("builtin/output/surface", "OutputSurface", [pi(1, "baseColor", "vec3", True)], []),
    )
    print("wrote", len(os.listdir(BASE)), "files under", BASE)


if __name__ == "__main__":
    main()
