#!/usr/bin/env python3
"""Protocol smoke test for GaleshiScriptWorker.

Compiles a tiny script module, starts the worker process, exercises a
representative subset of the C++ API and answers each request with a mock
host response. This validates packet layout, worker callbacks and request
encoding without running the full editor.
"""
from __future__ import annotations

import os
import pathlib
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
WORKER = pathlib.Path(
    os.environ.get(
        "GALESHI_SCRIPT_WORKER",
        ROOT / "build" / "debug" / "bin" / "GaleshiScriptWorker",
    )
)
CXX = os.environ.get("CXX", "c++")
PACKET = struct.Struct("=IIIII7fI512s512s")
UINT32_MAX = 0xFFFFFFFF

LOAD = 1
START = 2
FRAME = 3
QUIT = 4
LOG = 5
REQUEST = 6
RESPONSE = 7
LOAD_RESULT = 8
READY = 9
FRAME_DONE = 10

# CppWorkerRequest values used by the test script.
CREATE_ENTITY = 16
SET_POSITION = 6
GET_AXIS_RAW = 27
ATTACH_BOX = 32
PLAY_SOUND = 41
SET_POST_PROCESS_FLAG = 49
CREATE_UI_CANVAS = 60
BEHAVIOR_TREE_ADD_NODE = 154
SET_MATERIAL_FLOAT = 161
SET_RENDERER_DEFERRED = 172
LANDSCAPE_LAYER_COUNT = 178
POLL_UI_EVENT = 85

SCRIPT_SOURCE = r'''
#include "GaleshiEngine/Scripting/CppScriptApi.hpp"

static const GaleshiScriptApi* g_api = nullptr;

extern "C" void GaleshiScriptRegister(const GaleshiScriptApi* api) {
    g_api = api;
}

extern "C" void GaleshiScriptUpdate(float dt) {
    (void)dt;
    if (!g_api) return;
    const uint32_t entity = g_api->create_entity(g_api->user, "RoundTrip");
    GaleshiScriptVec3 position{1.0f, 2.0f, 3.0f};
    g_api->set_position(g_api->user, entity, position);
    (void)g_api->get_axis_raw(g_api->user, "Horizontal");
    (void)g_api->attach_box(g_api->user, entity,
                            GaleshiScriptVec3{0.5f, 0.5f, 0.5f}, false);
    (void)g_api->play_sound(g_api->user, "hit.wav", true);
    (void)g_api->set_post_process_flag(g_api->user, "bloom", true);
    (void)g_api->create_ui_canvas(g_api->user, "HUD", "HUD");
    (void)g_api->behavior_tree_add_node(
        g_api->user, entity, "Wait", 3, 0, 0);
    (void)g_api->set_material_float(g_api->user, entity, "roughness",
                                    0.5f);
    (void)g_api->set_renderer_deferred(g_api->user, true);
    (void)g_api->landscape_layer_count(g_api->user);
    GaleshiScriptUIEvent event{};
    (void)g_api->poll_ui_event(g_api->user, &event);
    g_api->log(g_api->user, "roundtrip-done");
}
'''


def packet(type_: int, *, ident: int = 0, kind: int = 0,
           entity: int = UINT32_MAX, valid: int = 0,
           values=(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0),
           text: str = "", text2: str = "") -> bytes:
    text_b = text.encode("utf-8")[:511]
    text2_b = text2.encode("utf-8")[:511]
    return PACKET.pack(
        type_, ident, kind, entity, valid,
        *values, UINT32_MAX, text_b, text2_b
    )


def read_packet(f) -> tuple | None:
    data = f.read(PACKET.size)
    if len(data) != PACKET.size:
        return None
    return PACKET.unpack(data)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> int:
    if not WORKER.is_file():
        print(f"Script worker not found: {WORKER}", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="galeshi_cpp_roundtrip_") as td:
        source = pathlib.Path(td) / "roundtrip.cpp"
        library = pathlib.Path(td) / "roundtrip.so"
        source.write_text(SCRIPT_SOURCE)
        subprocess.run(
            [CXX, "-std=c++20", "-shared", "-fPIC",
             f"-I{ROOT / 'engine' / 'include'}", str(source),
             "-o", str(library)],
            check=True,
        )

        process = subprocess.Popen(
            [str(WORKER)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        assert process.stdin and process.stdout

        process.stdin.write(packet(LOAD, text=str(library)))
        process.stdin.flush()
        load = read_packet(process.stdout)
        require(load is not None and load[0] == LOAD_RESULT and load[4] == 1,
                "worker rejected the test module")

        process.stdin.write(packet(START))
        process.stdin.flush()
        ready = read_packet(process.stdout)
        require(ready is not None and ready[0] == READY,
                "worker did not report Ready")

        process.stdin.write(packet(FRAME, values=(0.016, 0, 0, 0, 0, 0, 0)))
        process.stdin.flush()

        seen: set[int] = set()
        log_done = False
        frame_done = False
        while True:
            response = read_packet(process.stdout)
            if response is None:
                raise RuntimeError("worker closed the protocol stream")
            (type_, ident, kind, entity, valid,
             x, y, z, w, x2, y2, z2, other, text, text2) = response
            text = text.split(b"\0", 1)[0].decode("utf-8", "replace")
            text2 = text2.split(b"\0", 1)[0].decode("utf-8", "replace")
            if type_ == LOG:
                if text == "roundtrip-done":
                    log_done = True
                continue
            if type_ == FRAME_DONE:
                frame_done = True
                break
            if type_ != REQUEST:
                continue
            seen.add(kind)
            reply_entity = UINT32_MAX
            reply_valid = 1
            reply_x = 0.0
            if kind == CREATE_ENTITY:
                require(text == "RoundTrip", "CreateEntity name mismatch")
                reply_entity = 42
            elif kind == SET_POSITION:
                require(entity == 42 and abs(x - 1.0) < 1e-5 and
                        abs(y - 2.0) < 1e-5 and abs(z - 3.0) < 1e-5,
                        "SetPosition payload mismatch")
            elif kind == GET_AXIS_RAW:
                reply_x = 0.5
            elif kind == ATTACH_BOX:
                require(entity == 42 and abs(x - 0.5) < 1e-5 and
                        valid == 0, "AttachBox payload mismatch")
            elif kind == PLAY_SOUND:
                require(text == "hit.wav" and valid == 1,
                        "PlaySound payload mismatch")
            elif kind == SET_POST_PROCESS_FLAG:
                require(text == "bloom" and valid == 1,
                        "SetPostProcessFlag payload mismatch")
            elif kind == CREATE_UI_CANVAS:
                require(text == "HUD" and text2 == "HUD",
                        "CreateUICanvas payload mismatch")
                reply_entity = 7
            elif kind == BEHAVIOR_TREE_ADD_NODE:
                require(entity == 42 and text == "Wait",
                        "BehaviorTreeAddNode payload mismatch")
                reply_entity = 0
            elif kind == SET_MATERIAL_FLOAT:
                require(entity == 42 and text == "roughness" and
                        abs(x - 0.5) < 1e-5,
                        "SetMaterialFloat payload mismatch")
            elif kind == SET_RENDERER_DEFERRED:
                require(valid == 1, "SetRendererDeferred payload mismatch")
            elif kind == LANDSCAPE_LAYER_COUNT:
                reply_entity = 2
            elif kind == POLL_UI_EVENT:
                reply_valid = 0
            process.stdin.write(
                packet(RESPONSE, ident=ident, entity=reply_entity,
                       valid=reply_valid, values=(reply_x, 0, 0, 0, 0, 0, 0))
            )
            process.stdin.flush()

        process.stdin.write(packet(QUIT))
        process.stdin.flush()
        process.wait(timeout=5)

        expected = {
            CREATE_ENTITY, SET_POSITION, GET_AXIS_RAW, ATTACH_BOX,
            PLAY_SOUND, SET_POST_PROCESS_FLAG, CREATE_UI_CANVAS,
            BEHAVIOR_TREE_ADD_NODE, SET_MATERIAL_FLOAT,
            SET_RENDERER_DEFERRED, LANDSCAPE_LAYER_COUNT, POLL_UI_EVENT,
        }
        missing = expected - seen
        require(not missing, f"missing requests: {sorted(missing)}")
        require(log_done, "script did not reach the end")
        require(frame_done, "worker did not finish the frame")
        print("C++ worker round-trip test passed.")
        return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"Round-trip test failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
