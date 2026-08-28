#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


MODULE_PATH = Path(__file__).with_name("validate-imported-napi-wasm.py")
SPEC = importlib.util.spec_from_file_location("validate_imported_napi_wasm", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
validator = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = validator
SPEC.loader.exec_module(validator)


def uleb(value: int) -> bytes:
    output = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        output.append(byte | (0x80 if value else 0))
        if not value:
            return bytes(output)


def name(value: str) -> bytes:
    encoded = value.encode("utf-8")
    return uleb(len(encoded)) + encoded


def function_import(module: str, field: str) -> bytes:
    return name(module) + name(field) + b"\x00\x00"


def module(*imports: tuple[str, str], trailer: bytes = b"") -> bytes:
    payload = uleb(len(imports)) + b"".join(function_import(*item) for item in imports)
    return b"\x00asm\x01\x00\x00\x00" + b"\x02" + uleb(len(payload)) + payload + trailer


class ValidateImportedNapiWasmTests(unittest.TestCase):
    def test_accepts_engine_free_import_contract(self) -> None:
        imports = validator.validate_artifact(
            module(
                ("wasi_snapshot_preview1", "fd_write"),
                ("napi", "napi_create_object"),
                ("napi", "node_api_create_syntax_error"),
                ("napi_extension_wasmer_v0", "unofficial_napi_create_env"),
            )
        )
        self.assertEqual(len(imports), 4)

    def test_rejects_missing_standard_napi_imports(self) -> None:
        with self.assertRaisesRegex(validator.ValidationError, "no standard"):
            validator.validate_artifact(
                module(("napi_extension_wasmer_v0", "unofficial_napi_create_env"))
            )

    def test_rejects_missing_extension_imports(self) -> None:
        with self.assertRaisesRegex(validator.ValidationError, "no EdgeJS"):
            validator.validate_artifact(module(("napi", "napi_create_object")))

    def test_rejects_napi_imported_from_env(self) -> None:
        with self.assertRaisesRegex(validator.ValidationError, "wrong module"):
            validator.validate_artifact(
                module(
                    ("napi", "napi_create_object"),
                    ("napi_extension_wasmer_v0", "unofficial_napi_create_env"),
                    ("env", "napi_get_global"),
                )
            )

    def test_rejects_embedded_engine_symbol(self) -> None:
        custom_payload = name("fixture") + b"JS_NewRuntime"
        custom_section = b"\x00" + uleb(len(custom_payload)) + custom_payload
        with self.assertRaisesRegex(validator.ValidationError, "engine symbols"):
            validator.validate_artifact(
                module(
                    ("napi", "napi_create_object"),
                    ("napi_extension_wasmer_v0", "unofficial_napi_create_env"),
                    trailer=custom_section,
                )
            )

    def test_rejects_non_wasm_input(self) -> None:
        with self.assertRaisesRegex(validator.ValidationError, "not a WebAssembly"):
            validator.validate_artifact(b"not wasm")

    def test_compares_native_and_host_js_provider_operations(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for relative in ("src/guest/napi.rs", "src/guest/napi_js.rs"):
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(
                    'let napi_namespace = namespace! {\n'
                    '  "napi_create_object" => provider,\n'
                    '};\n'
                    'let napi_extension_wasmer_namespace = namespace! {\n'
                    '  "unofficial_napi_create_env" => provider,\n'
                    '};\n',
                    encoding="utf-8",
                )
            standard, extension = validator.read_provider_operations(root)
            imports = validator.validate_artifact(
                module(
                    ("napi", "napi_create_object"),
                    ("napi_extension_wasmer_v0", "unofficial_napi_create_env"),
                )
            )
            validator.validate_provider_imports(imports, standard, extension)

    def test_rejects_provider_missing_an_edge_import(self) -> None:
        imports = validator.validate_artifact(
            module(
                ("napi", "napi_create_object"),
                ("napi_extension_wasmer_v0", "unofficial_napi_create_env"),
            )
        )
        with self.assertRaisesRegex(validator.ValidationError, "does not implement"):
            validator.validate_provider_operations(imports, set())


if __name__ == "__main__":
    unittest.main()
