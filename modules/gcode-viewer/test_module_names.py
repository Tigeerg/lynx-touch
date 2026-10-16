#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Headless check: каждый модуль использует только то, что определил или импортировал.

Слои разъехались по файлам (этап 0 переноса на C++), и обращение к имени из
соседнего слоя без импорта не ломает загрузку: исключение глушится в `except`,
работа молча деградирует. Так уже было трижды (`_step_shape` в step_surfaces,
`struct` в geometry, `_holder_profile` и `stage_label` в collision).

Разбор через `ast`, а не регулярки: строки и комментарии не учитываются, поэтому
многострочные импорты с комментариями внутри не дают ложных срабатываний.

Запуск: python modules/gcode-viewer/test_module_names.py (exit 0 = пойдено)
"""
import ast
import builtins
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

MODULES = ["gv_types", "gv_common", "gcode_parser", "ui_tags", "cache_store", "stl_import", "packing",
           "step_import", "step_surfaces", "tangents", "silhouette", "normals", "outline",
           "path_prep", "tooling", "collision", "geometry", "controller", "mock_backend"]

STDLIB = {"math", "os", "re", "json", "hashlib", "struct", "threading", "array", "bisect",
          "logging", "sys", "time", "typing", "collections", "dataclasses", "functools"}


def analyse(path):
    """(определено, импортировано, использовано) для одного модуля."""
    tree = ast.parse(io.open(path, encoding="utf-8").read())
    defined, imported, used = set(), set(), set()
    for node in ast.walk(tree):
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            defined.add(node.name)
            args = getattr(node, "args", None)
            if args is not None:
                for a in (list(args.posonlyargs) + list(args.args) + list(args.kwonlyargs)
                          + [args.vararg, args.kwarg]):
                    if a is not None:
                        defined.add(a.arg)
        elif isinstance(node, ast.Lambda):
            for a in (list(node.args.posonlyargs) + list(node.args.args)
                      + list(node.args.kwonlyargs) + [node.args.vararg, node.args.kwarg]):
                if a is not None:
                    defined.add(a.arg)
        elif isinstance(node, ast.Name):
            (defined if isinstance(node.ctx, ast.Store) else used).add(node.id)
        elif isinstance(node, ast.Import):
            for a in node.names:
                imported.add((a.asname or a.name).split(".")[0])
        elif isinstance(node, ast.ImportFrom):
            for a in node.names:
                imported.add(a.asname or a.name)
        elif isinstance(node, ast.ExceptHandler) and node.name:
            defined.add(node.name)
        elif isinstance(node, (ast.Global, ast.Nonlocal)):
            defined.update(node.names)
    return defined, imported, used


def public_defs(path):
    """Имена верхнего уровня: их и берут соседние слои."""
    src = io.open(path, encoding="utf-8").read()
    names = set(re.findall(r"^(?:def|class)\s+(\w+)", src, re.M))
    names |= set(re.findall(r"^([A-Z_][A-Z0-9_]*)\s*=", src, re.M))
    return names


def main() -> int:
    known_globals = set(dir(builtins)) | STDLIB
    public = {}
    data = {}
    for mod in MODULES:
        path = os.path.join(HERE, mod + ".py")
        if not os.path.exists(path):
            continue
        data[mod] = analyse(path)
        public[mod] = public_defs(path)

    bad = 0
    for mod in sorted(data):
        defined, imported, used = data[mod]
        have = known_globals | defined | imported
        foreign = sorted(n for n in used - have
                         if any(n in names for other, names in public.items() if other != mod))
        if foreign:
            print("FAIL %-14s не импортировано из соседних слоёв: %s" % (mod, ", ".join(foreign)))
            bad += len(foreign)
        else:
            print("ok   %-14s имён в модуле: %d" % (mod, len(used)))
    print("RESULT: %s" % ("OK" if not bad else "FAIL (%d)" % bad))
    return 0 if not bad else 1


if __name__ == "__main__":
    sys.exit(main())
