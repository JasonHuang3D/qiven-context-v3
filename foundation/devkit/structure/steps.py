"""The structure rules as steps of the build (structure/CMakeLists.txt), in
every tree that sets QIVEN_STRUCTURE: the daily tree and the structure
preset's. A finding fails its step, so the build, naming the file and, where
one holds it, the line, as a compiler names an error. OUT is the build's
structure/ directory (records.py names its files).

    python -I steps.py compile ROOT OUT DUMPBIN -- COMPILER ARGS...
        a compile's launcher: runs the compile, writes its object's record
        and applies the rules that record decides alone (rules.object_rules);
        a finding fails the compile and removes its object. Ninja reruns it
        exactly when the object recompiles.
    python -I steps.py libraries OUT DUMPBIN
        names.json, the runtimes' and the network's names, before any compile.
    python -I steps.py target ROOT OUT TARGET
        a target's step, after the build's every target: the rules its own
        objects' records decide, and those over the names its link closure
        defines (rules.defined_name_rules); rerun when the target relinks,
        a closure object's record, the graph or a rule changes (its depfile).
    python -I steps.py links ROOT OUT BUILD CMAKE LINKS
        the link rules (rules.link_rules) over the tree's graph, CMake's
        --graphviz with the tree's cache, LINKS's -D lines; rerun when LINKS,
        written by the configure where its content changes (the targets'
        link properties and the cache), or a rule changes.

Each step writes what it found and the allowance rows it used beside its
output, targets/<target>.json and links.json, for the control of the rows
(tests/test_boundaries.py).
"""

import json
import os
import subprocess
import sys
from pathlib import Path

# Under -I the checkout is off sys.path: its root appended, so that the standard library still wins.
sys.path.append(str(Path(__file__).resolve().parents[3]))
from foundation.devkit.python import process
from foundation.devkit.python import system
from foundation.devkit.python import table
from foundation.devkit.structure import layers, rules
from foundation.devkit.structure import records as R

def compile_argv(argv):
    """The object (/Fo) and the source, the last argument naming a file, of a compile's argv."""
    obj = next((a[3:] for a in argv if a[:3] in ("/Fo", "-Fo")), None)
    source = next((a for a in reversed(argv[1:]) if not a.startswith(("/", "-")) and Path(a).is_file()), None)
    return obj, source


def report(found, where):
    """Prints found's errors; whether it found any."""
    for line in found.errors():
        print(line, flush=True)
    if found.found:
        print(f"structure: {len(found.found)} findings in {where}", flush=True)
    return bool(found.found)


def compile_step(root, out, dumpbin, argv):
    try:
        done = process.run(argv, limit=R.TOOL_SECONDS)
    except process.TimedOut as e:
        raise SystemExit(f"structure: {e}") from e
    sys.stdout.buffer.write(done.out)
    sys.stdout.flush()
    sys.stderr.buffer.write(done.err)
    sys.stderr.flush()
    if done.status != 0:
        return done.status
    obj, source = compile_argv(argv)
    if obj is None or source is None:
        raise SystemExit(f"structure: no object (/Fo) or no source in the compile {subprocess.list2cmdline(argv)}")
    obj = Path(os.path.abspath(obj))
    absolute = os.path.abspath(source)
    record = R.object_record(root, absolute, absolute, obj, subprocess.list2cmdline(argv),
                             system.output_text(done.out, "replace"), dumpbin, table.trees(root).vendored)
    R.write_json(out / "objects" / (R.key_of(record["target"], record["source"], obj) + ".json"), record)
    names = json.loads((out / "names.json").read_text(encoding="utf-8"))
    found = rules.Findings(layers.of(root))
    one = R.Records(root, [R.Object(record["source"], record["target"], [tuple(i) for i in record["includes"]],
                                    set(record["undefined"]), set(record["weak"]), set(record["defined"]),
                                    record["command"], record["vendored"])], names)
    rules.object_rules(one, root, found)
    if report(found, record["source"] or str(obj)):
        obj.unlink(missing_ok=True)  # the compile failed: no object stands for it
        return 1
    return 0


def libraries_step(out, dumpbin):
    R.write_json(out / "names.json", R.library_names(dumpbin))
    return 0


def finish(out, name, found, inputs):
    """Writes the step's findings and used rows, its depfile over inputs and, where it found
    nothing, its stamp; the step's status."""
    stamp = out / (name + ".stamp")
    R.write_json(out / (name + ".json"), {"findings": found.errors(), "used": sorted(list(r) for r in found.used)})

    def escaped(path):
        return Path(path).as_posix().replace(" ", "\\ ")
    if inputs is not None:
        depfile = f"{escaped(stamp)}: " + " \\\n  ".join(escaped(p) for p in sorted(set(inputs))) + "\n"
        R.write_if_changed(out / (name + ".d"), depfile)
    if report(found, name):
        stamp.unlink(missing_ok=True)
        return 1
    stamp.write_text("", encoding="utf-8")
    return 0


def target_step(root, out, target):
    by_target = R.objects_by_target(root, out)
    names = json.loads((out / "names.json").read_text(encoding="utf-8"))
    text = (out / "targets.dot").read_text(encoding="utf-8")
    edges = R.Records(root, [], names, text)
    paths = [p for t in sorted(edges.closure(target) | {target}) for p in by_target.get(t, ())]
    missing = [str(p) for p in paths if not p.is_file()]
    if missing:
        raise SystemExit(f"structure: {target}'s closure has objects with no record: {', '.join(missing[:5])}")
    objects = [R.read_object(p) for p in paths]
    own = R.Records(root, [o for o in objects if o.target == target], names)
    found = rules.Findings(layers.of(root))
    rules.object_rules(own, root, found)
    rules.defined_name_rules(R.Records(root, objects, names, text), found)
    inputs = paths + [root / f for f in rules.own_files(own, root)]
    return finish(out / "targets", target, found, inputs)


def links_step(root, out, build, cmake, links):
    defines = [line for line in links.read_text(encoding="utf-8").splitlines() if line.startswith("-D")]
    text = R.graph(root, build, out / "graph-build", cmake, defines)
    R.write_if_changed(out / "targets.dot", text)
    objects = []
    vendored = table.trees(root).vendored
    for entry in json.loads((out / "compile_commands.json").read_text(encoding="utf-8")):
        source = R.rel_posix(entry["file"], root)
        objects.append(R.Object(source, R.target_of(Path(entry["directory"], entry["output"])), [], set(), set(),
                                set(), entry["command"], source in vendored))
    found = rules.Findings(layers.of(root))
    rules.link_rules(R.Records(root, objects, None, text), found)
    return finish(out, "links", found, None)


def main(argv):
    step = argv[1]
    if step == "compile":
        cut = argv.index("--")
        root, out, dumpbin = Path(os.path.normpath(argv[2])), Path(argv[3]), argv[4]
        return compile_step(root, out, dumpbin, argv[cut + 1:])
    if step == "libraries":
        return libraries_step(Path(argv[2]), argv[3])
    root, out = Path(os.path.normpath(argv[2])), Path(argv[3])
    if step == "target":
        return target_step(root, out, argv[4])
    if step == "links":
        return links_step(root, out, Path(argv[4]), argv[5], Path(argv[6]))
    raise SystemExit(f"structure: no step {step}")


if __name__ == "__main__":
    sys.exit(main(sys.argv))
