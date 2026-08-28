#!/usr/bin/env python3
"""Validate the engine-free EdgeJS WASIX artifact and its N-API ABI."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import sys


NAPI_MODULE = "napi"
EXTENSION_MODULE = "napi_extension_wasmer_v0"
ENGINE_SENTINELS = (
    b"JS_NewRuntime",
    b"JS_NewContext",
    b"JS_EvalFunction",
    b"quickjs.c",
    b"libquickjs",
    b"_ZN2v8",
    b"V8::InitializePlatform",
    b"libv8_monolith",
)


class ValidationError(ValueError):
    pass


@dataclass(frozen=True)
class WasmImport:
    module: str
    name: str
    kind: int


class Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.offset = 0

    def read_byte(self) -> int:
        if self.offset >= len(self.data):
            raise ValidationError("unexpected end of WebAssembly data")
        value = self.data[self.offset]
        self.offset += 1
        return value

    def read_bytes(self, length: int) -> bytes:
        end = self.offset + length
        if end > len(self.data):
            raise ValidationError("unexpected end of WebAssembly data")
        value = self.data[self.offset:end]
        self.offset = end
        return value

    def read_uleb(self) -> int:
        value = 0
        shift = 0
        for _ in range(10):
            byte = self.read_byte()
            value |= (byte & 0x7F) << shift
            if byte & 0x80 == 0:
                return value
            shift += 7
        raise ValidationError("invalid unsigned LEB128 value")

    def read_name(self) -> str:
        raw = self.read_bytes(self.read_uleb())
        try:
            return raw.decode("utf-8")
        except UnicodeDecodeError as error:
            raise ValidationError("invalid UTF-8 import name") from error


def skip_limits(reader: Reader) -> None:
    flags = reader.read_uleb()
    reader.read_uleb()
    if flags & 0x01:
        reader.read_uleb()


def parse_import_section(payload: bytes) -> list[WasmImport]:
    reader = Reader(payload)
    imports: list[WasmImport] = []
    for _ in range(reader.read_uleb()):
        module = reader.read_name()
        name = reader.read_name()
        kind = reader.read_byte()
        if kind == 0:  # function
            reader.read_uleb()
        elif kind == 1:  # table
            reader.read_byte()
            skip_limits(reader)
        elif kind == 2:  # memory
            skip_limits(reader)
        elif kind == 3:  # global
            reader.read_byte()
            reader.read_byte()
        elif kind == 4:  # exception tag
            reader.read_byte()
            reader.read_uleb()
        else:
            raise ValidationError(f"unsupported import kind {kind}")
        imports.append(WasmImport(module, name, kind))
    if reader.offset != len(payload):
        raise ValidationError("trailing bytes in WebAssembly import section")
    return imports


def parse_imports(data: bytes) -> list[WasmImport]:
    if not data.startswith(b"\x00asm\x01\x00\x00\x00"):
        raise ValidationError("not a WebAssembly 1.0 module")
    reader = Reader(data[8:])
    imports: list[WasmImport] = []
    while reader.offset < len(reader.data):
        section_id = reader.read_byte()
        payload = reader.read_bytes(reader.read_uleb())
        if section_id == 2:
            imports.extend(parse_import_section(payload))
    return imports


def is_napi_name(name: str) -> bool:
    return name.startswith(("napi_", "node_api_"))


def validate_cmake_cache(path: Path) -> None:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise ValidationError(f"unable to read CMake cache {path}: {error}") from error
    settings = {
        key: value
        for line in lines
        if not line.startswith(("#", "//")) and "=" in line
        for key, value in (line.split("=", 1),)
    }
    provider = settings.get("EDGE_NAPI_PROVIDER:STRING")
    if provider != "imports":
        raise ValidationError(
            f"CMake cache selected EDGE_NAPI_PROVIDER={provider!r}, expected 'imports'"
        )


def read_provider_operations(napi_root: Path) -> tuple[set[str], set[str]]:
    provider_sets: list[tuple[set[str], set[str]]] = []
    for relative in ("src/guest/napi.rs", "src/guest/napi_js.rs"):
        path = napi_root / relative
        try:
            source = path.read_text(encoding="utf-8")
        except OSError as error:
            raise ValidationError(f"unable to read provider source {path}: {error}") from error
        operation_sets: list[set[str]] = []
        for namespace_name, pattern in (
            ("napi_namespace", r'"((?:napi_|node_api_)[^"]+)"\s*=>'),
            ("napi_extension_wasmer_namespace", r'"(unofficial_napi_[^"]+)"\s*=>'),
        ):
            match = re.search(
                rf"let {namespace_name} = namespace!\s*\{{(.*?)\n\s*\}};",
                source,
                re.DOTALL,
            )
            if match is None:
                raise ValidationError(f"provider {namespace_name} not found in {path}")
            operations = set(re.findall(pattern, match.group(1)))
            if not operations:
                raise ValidationError(f"provider {namespace_name} is empty in {path}")
            operation_sets.append(operations)
        provider_sets.append((operation_sets[0], operation_sets[1]))
    if provider_sets[0] != provider_sets[1]:
        raise ValidationError(
            "native and host-JavaScript provider operation inventories differ"
        )
    return provider_sets[0]


def read_provider_extension_operations(napi_root: Path) -> set[str]:
    return read_provider_operations(napi_root)[1]


def validate_provider_operations(imports: list[WasmImport], operations: set[str]) -> None:
    required = {
        item.name
        for item in imports
        if item.kind == 0 and item.module == EXTENSION_MODULE
    }
    missing = sorted(required - operations)
    if missing:
        raise ValidationError(
            "Wasmer provider does not implement Edge extension imports: "
            + ", ".join(missing[:12])
        )


def validate_provider_imports(
    imports: list[WasmImport],
    standard_operations: set[str],
    extension_operations: set[str],
) -> None:
    required_standard = {
        item.name
        for item in imports
        if item.kind == 0 and item.module == NAPI_MODULE and is_napi_name(item.name)
    }
    missing_standard = sorted(required_standard - standard_operations)
    if missing_standard:
        raise ValidationError(
            "Wasmer provider does not implement Edge standard imports: "
            + ", ".join(missing_standard[:12])
        )
    validate_provider_operations(imports, extension_operations)


def validate_artifact(data: bytes) -> list[WasmImport]:
    imports = parse_imports(data)
    function_imports = [item for item in imports if item.kind == 0]
    napi_imports = [
        item
        for item in function_imports
        if item.module == NAPI_MODULE and is_napi_name(item.name)
    ]
    extension_imports = [
        item
        for item in function_imports
        if item.module == EXTENSION_MODULE
        and item.name.startswith("unofficial_napi_")
    ]
    if not napi_imports:
        raise ValidationError("artifact has no standard function imports from module 'napi'")
    if not extension_imports:
        raise ValidationError(
            "artifact has no EdgeJS function imports from module "
            "'napi_extension_wasmer_v0'"
        )

    misplaced = [
        item
        for item in function_imports
        if (
            is_napi_name(item.name) and item.module != NAPI_MODULE
        ) or (
            item.name.startswith("unofficial_napi_")
            and item.module != EXTENSION_MODULE
        )
    ]
    if misplaced:
        details = ", ".join(f"{item.module}.{item.name}" for item in misplaced[:8])
        raise ValidationError(f"N-API imports use the wrong module: {details}")

    found_sentinels = [token.decode("ascii") for token in ENGINE_SENTINELS if token in data]
    if found_sentinels:
        raise ValidationError(
            "artifact contains embedded JavaScript engine symbols: "
            + ", ".join(found_sentinels)
        )
    return imports


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("wasm", type=Path)
    parser.add_argument("--cmake-cache", type=Path)
    parser.add_argument(
        "--provider-napi-root",
        type=Path,
        help="N-API source tree used by the Wasmer provider",
    )
    parser.add_argument("--print-imports", action="store_true")
    args = parser.parse_args()
    try:
        if args.cmake_cache:
            validate_cmake_cache(args.cmake_cache)
        data = args.wasm.read_bytes()
        imports = validate_artifact(data)
        if args.provider_napi_root:
            standard, extension = read_provider_operations(args.provider_napi_root)
            validate_provider_imports(imports, standard, extension)
    except (OSError, ValidationError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    if args.print_imports:
        for item in imports:
            print(f"{item.module}.{item.name}")
    napi_count = sum(
        item.kind == 0 and item.module == NAPI_MODULE and is_napi_name(item.name)
        for item in imports
    )
    extension_count = sum(
        item.kind == 0
        and item.module == EXTENSION_MODULE
        and item.name.startswith("unofficial_napi_")
        for item in imports
    )
    print(
        f"validated engine-free EdgeJS wasm: {napi_count} N-API imports, "
        f"{extension_count} extension imports"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
