"""Load a Card's Python modules under a private package prefix."""

import builtins
import importlib
import importlib.machinery
import sys
import types


def load(host, sdk, source, wire, packages):
    import site

    site.addsitedir(packages)
    prefix = f"_tsugumi_card_{id(host):x}"
    roots = [sdk, source, wire]
    package = types.ModuleType(prefix)
    package.__path__ = roots
    package.__source_root__ = source
    sys.modules[prefix] = package
    sys.modules[f"{prefix}._tsugumi_host"] = host
    setattr(package, "_tsugumi_host", host)

    path_finder = importlib.machinery.PathFinder

    def local_import(name, globals=None, locals=None, fromlist=(), level=0):
        root = name.partition(".")[0]
        if not level and root and (
            root == "_tsugumi_host" or path_finder.find_spec(root, roots) is not None
        ):
            module = builtins.__import__(
                f"{prefix}.{name}", globals, locals, fromlist, 0
            )
            return module if fromlist else sys.modules[f"{prefix}.{root}"]
        return builtins.__import__(name, globals, locals, fromlist, level)

    class CardLoader(importlib.machinery.SourceFileLoader):
        def exec_module(self, module):
            namespace = dict(vars(builtins), __import__=local_import)
            module.__dict__["__builtins__"] = namespace
            super().exec_module(module)

    class CardFinder:
        def find_spec(self, fullname, path=None, target=None):
            if not fullname.startswith(f"{prefix}."):
                return None
            spec = path_finder.find_spec(fullname, path, target)
            if spec and isinstance(spec.loader, importlib.machinery.SourceFileLoader):
                spec.loader = CardLoader(fullname, spec.origin)
            return spec

    sys.meta_path.insert(0, CardFinder())
    return importlib.import_module(f"{prefix}.tsugumi._runtime").invoke
