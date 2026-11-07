"""A test's key and the checkout's code id, by the test framework's design.

A file's digest is git's blob id (git.blob_ids). A test's key is the SHA-256
of its configuration and the files of each module its labels name, documents
and Python test files out, then a Python test's own file; a check test's
module enters whole. The code id is the SHA-256 of every file but the
documents. This directory's README states the form of each.
"""

import fnmatch
import hashlib
import json
import posixpath
from collections import namedtuple

from foundation.devkit.python import git
from foundation.devkit.python import table as module_table

Files = namedtuple("Files", "modules unowned")
CHECKS = ("rules", "exemptions")  # a module's checks, each named <check>.<M>


class NoModule(Exception):
    """A test whose labels name no module of the table."""


def is_document(path):
    """Whether path is a document, which no test's key holds."""
    return path.endswith(".md")


def is_python_test(path):
    """Whether path is a Python test file, which only its own tests' keys hold."""
    return fnmatch.fnmatchcase(posixpath.basename(path), "test_*.py")


def files(root, table, env=None):
    """The Files of git's listing under root: each module of the table to its
    files' blob ids, each file under its first owner, and the files of none."""
    ids = git.blob_ids(root, env=env)
    owner = module_table.owners(table, sorted(ids))
    modules = {row["name"]: {} for row in table["modules"]}
    unowned = {}
    for path in sorted(ids):
        (modules[owner[path][0]] if owner[path] else unowned)[path] = ids[path]
    return Files(modules, unowned)


def modules_of(name, labels, modules):
    """(the test's modules, in name order; those that enter whole): its labels
    that name a module of `modules`, and a check test's module M."""
    named = sorted({label for label in labels if label in modules})
    if not named:
        raise NoModule(f"{name}: no label names a module of the table: {', '.join(labels) or 'no label'}")
    whole = frozenset(m for m in named if any(name == f"{check}.{m}" for check in CHECKS))
    return named, whole


def own_file(name, paths):
    """A Python test's own test_*.py among paths, by its name's dotted path,
    the longest that matches; None for none."""
    held = [p for p in paths if is_python_test(p) and name.startswith(p[:-3].replace("/", ".") + ".")]
    return max(held, key=len, default=None)


def test_key(preset, name, labels, files):
    """The key of the test `name` with `labels` in the configuration `preset`,
    over `files`, a Files: 64 hex digits."""
    named, whole = modules_of(name, labels, files.modules)
    parts, own = [], None
    for module in named:
        held = files.modules[module]
        kept = sorted(held.items()) if module in whole else \
            sorted((p, i) for p, i in held.items() if not is_document(p) and not is_python_test(p))
        parts.append([module, [list(item) for item in kept]])
        if module not in whole and own is None:
            path = own_file(name, held)
            own = None if path is None else [path, held[path]]
    return _digest([preset, parts, own])


def bare_key(preset, name):
    """The key of a test of no module, in a step that runs its tests whole (the
    fuzz targets): its configuration and name alone, no file. 64 hex digits."""
    return _digest([preset, name, None])


def code_id(files):
    """The checkout's code id over `files`, a Files: 64 hex digits."""
    every = {path: i for held in (*files.modules.values(), files.unowned) for path, i in held.items()}
    return _digest([[path, every[path]] for path in sorted(every) if not is_document(path)])


def _digest(value):
    """The SHA-256 of value's JSON, in hex."""
    return hashlib.sha256(json.dumps(value, separators=(",", ":")).encode("utf-8")).hexdigest()
