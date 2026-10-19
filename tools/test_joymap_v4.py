#!/usr/bin/env python3
"""Build/run actual-source joymap regressions on Linux; no hardware/config writes.

Most checks call production helpers directly. Joy-Con/OSD use source-derived
statements because input_cb owns a large unrelated hardware dispatch graph.
Menu return checks are source guards, not a UI emulator. Generated seams must
match an exact known source shape; unknown source shapes fail closed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


PAD_SOURCES = ("joymap_pad.cpp", "joymap_pad.h", "joymap_pads.h")


def git_read(root, arguments):
    # Older Git requires global safe.directory; isolate it from the user configuration.
    with tempfile.TemporaryDirectory(prefix="joymap-v4-git-") as scratch:
        config = Path(scratch) / "git.config"
        config.write_text(f'[safe]\n\tdirectory = "{root}"\n')
        git_env = dict(os.environ, GIT_CONFIG_GLOBAL=str(config))
        return subprocess.run(["git", "-C", str(root), *arguments], env=git_env,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15)


def braced_block(source, start):
    opening = source.index("{", start)
    depth = 0
    for pos in range(opening, len(source)):
        depth += (source[pos] == "{") - (source[pos] == "}")
        if depth == 0:
            return source[start:pos + 1]
    raise ValueError("unterminated source block")


def generate_seams(source):
    if "static void apply_combined_joycon_orientation(" in source:
        joycon = "apply_combined_joycon_orientation(dev, input[dev].mmap, loaded_saved_map);"
        if joycon not in source:
            raise ValueError("combined orientation helper is not wired into map load")
    else:
        start = source.index("if (JOYCON_COMBINED(dev))", source.index("// gamecontrollerdb's Joy-Con"))
        joycon = braced_block(source, start)
    if "static int menu_axis_direction(" in source:
        osd = "const int direction = menu_axis_direction(map, KEY_EMU + ((map & MAP_AXIS_MASK) << 1) + endpoint); return direction < 0 ? -1 : direction + axis * 2;"
        if source.count("menu_axis_direction(") < 3:
            raise ValueError("menu direction helper is not wired into both axes")
    else:
        start = source.index("if (input[dev].mmap[SYS_AXIS_X])", source.index("for (int i = SYS_MS_RIGHT; i <= SYS_MS_UP; i++)", source.index("void input_cb")))
        x_block = braced_block(source, start)
        y_start = source.index("if (input[dev].mmap[SYS_AXIS_Y])", start)
        y_block = braced_block(source, y_start)
        osd = ("input_event event = {}; input_event *ev = &event; ev->value = 1; "
               "ev->code = KEY_EMU + ((map & MAP_AXIS_MASK) << 1) + endpoint; "
               "input[dev].mmap[SYS_AXIS_X] = axis ? 0 : map; "
               "input[dev].mmap[SYS_AXIS_Y] = axis ? map : 0; joymap_test_direction = -1; "
               + x_block.replace("joy_digital", "joymap_capture_direction") + "\n"
               + y_block.replace("joy_digital", "joymap_capture_direction") + "\nreturn joymap_test_direction;")
    emission_start = source.index("if (last_state != axis_edge)", source.index("if (!trigger_axis_edge("))
    emission = braced_block(source, emission_start).replace("input_cb(&ev, pai, i)", "joymap_capture_event(&ev, pai, i)")
    hold_poll = ""
    if "static void osd_hold_poll()" in source:
        body = braced_block(source, source.index("static void osd_hold_poll()"))
        for old, new in (("static void osd_hold_poll()", "static void joymap_test_osd_hold_poll()"),
                         ("joy_digital(", "joymap_capture_joy("), ("Info(", "joymap_capture_info("),
                         ("user_io_osd_is_visible()", "joymap_test_osd_visible"), ("is_menu()", "joymap_test_is_menu")):
            if old not in body:
                raise ValueError(f"osd_hold_poll() no longer contains {old}")
            body = body.replace(old, new)
        hold_poll = ("#define JOYMAP_TEST_HAS_HOLD_POLL 1\n"
                     "static std::vector<std::pair<int, int> > joymap_test_joy;\n"
                     "static void joymap_capture_joy(int, uint32_t, uint32_t, char press, int bnum, int = 0) { joymap_test_joy.emplace_back(bnum, press); }\n"
                     "static void joymap_capture_info(const char *, int = 0, int = 0, int = 0, int = 0) {}\n"
                     "static int joymap_test_osd_visible;\nstatic int joymap_test_is_menu = 1;\n" + body + "\n")
    return (hold_poll + "static int joymap_test_direction;\n"
            "static void joymap_capture_direction(int, int, int, int, int direction) { joymap_test_direction = direction; }\n"
            "static void joymap_test_combined_orientation(int dev, int loaded_saved_map) { " + joycon + " }\n"
            "static int joymap_test_osd_direction(int dev, int axis, uint32_t map, int endpoint) { " + osd + " }\n"
            "static std::vector<std::pair<int, int> > joymap_test_events;\n"
            "static void joymap_capture_event(input_event *event, input_absinfo *, int) { joymap_test_events.emplace_back(event->code, event->value); }\n"
            "static void joymap_test_emit_axis(int last_state, int axis_edge) { input_event ev = {}; ev.code = ABS_Z; input_absinfo *pai = NULL; int i = 0; joymap_test_events.clear(); " + emission + " }\n")


def menu_checks(source):
    selector = re.search(r"menusub\s*=\s*([^;]+);\s*SelectFile\(\"\", 0, SCANO_CORES, MENU_CORE_FILE_SELECTED1, MENU_SYSTEM1\);", source)
    misc_start = source.index("case MENU_MISC2:")
    misc = source[misc_start:source.index("case MENU_", misc_start + 6)]
    back = re.search(r"if \(is_menu\(\)\)\s*\{\s*menusub\s*=\s*([^;]+);\s*menustate = MENU_SYSTEM1;", misc)
    return [(selector is not None and selector.group(1).strip() == "MENU_SYSTEM_SUB_EXIT", "8 core-selector return uses Exit enum (source guard)"),
            (back is not None and back.group(1).strip() == "MENU_SYSTEM_SUB_HELP", "8 Misc return uses Help enum (source guard)")]


def menu_row_checks(source):
    """Source guard: Define buttons writes rows 1-7 only through its row cache."""
    def case(name):
        start = source.index(f"\tcase {name}:")
        return source[start:source.index("\tcase MENU_", start + 1)]
    # the "define it in the Menu core first" screen redraws everything and leaves the state
    screen = re.sub(r"for \(int i = 0; i < OsdGetSize\(\); i\+\+\) OsdWrite\(i\);.*?menustate = MENU_JOYDIGMAP2;", "",
                    case("MENU_JOYDIGMAP1"), flags=re.S)
    reset = all("joymap_rows_valid = 0;" in case(name) for name in ("MENU_JOYDIGMAP", "MENU_JOYRESET"))
    return [(not re.search(r"OsdWrite\([1-7][,)]", screen) and reset,
             "R1 Define buttons rows 1-7 use the row cache, reset on entry (source guard)")]


def hold_wiring_checks(source):
    """Source guards for the hold-mode wiring inside input_cb()/input_poll()."""
    cb_start = source.index("static void input_cb(struct input_event *ev, struct input_absinfo *absinfo, int dev, bool menu_event)\n{")
    cb = source[cb_start:source.index("int input_poll(")]
    poll = source[source.index("int input_poll("):]
    combo = re.search(r"if \(ev->value == 1 && mapping_key_mapped && mapping_key_mapped != ev->code && is_menu\(\) && "
                      r"mapping_button == SYS_MAP_POS_OSD && mapping_type && !mapping_osd_hold_latched\)\s*\{\s*"
                      r"mapping_osd_hold_timer = 0;", cb)
    return [("int osd_event = osd_combo_event(dev, ev);" in cb, "H5 input_cb takes Menu events from osd_combo_event (source guard)"),
            (re.search(r"if \(getchar\) return ret;\s*osd_hold_poll\(\);", poll) is not None, "H5 input_poll runs the hold timers (source guard)"),
            (combo is not None, "H5 a combo cancels the hold capture and is refused once hold is set (source guard)"),
            ("if (osd_hold_release" not in source and "osd_hold_fired" not in source, "H5 no early return on a hold release (source guard)"),
            ("if (!input[dev].has_mmap)\n\t{\n\t\tinput[dev].osd_hold_timer = 0;" in source, "H5 a reloaded system map drops a pending hold (source guard)")]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="ignored build/evidence directory")
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--revision", help="read-only git source snapshot for baseline comparison")
    parser.add_argument("--sanitize", action="store_true", help="enable AddressSanitizer and UndefinedBehaviorSanitizer")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    try:
        relative = output.relative_to(root / "bin")
    except ValueError:
        parser.error("--output must be a dedicated ignored directory below this checkout's bin/")
    if not relative.parts or not relative.parts[0].startswith("joymap-v4-"):
        parser.error("--output must be below bin/joymap-v4-<topic>/, not the bin root")
    ignored = git_read(root, ["check-ignore", "--no-index", "--", str(output)])
    tracked = git_read(root, ["ls-files", "--", str(output)])
    if ignored.returncode or tracked.returncode or tracked.stdout.strip():
        parser.error("--output must be ignored and contain no tracked files")
    output.mkdir(parents=True, exist_ok=True)
    source_root = root
    if args.revision:
        source_root = output / "source-snapshot"
        source_root.mkdir(exist_ok=True)
        for name in ("input.cpp", "input.h", "gamecontroller_db.cpp", "gamecontroller_db.h", "menu.cpp", "joymapping.cpp", *PAD_SOURCES):
            result = git_read(root, ["show", f"{args.revision}:{name}"])
            if result.returncode and name in PAD_SOURCES:
                (source_root / name).unlink(missing_ok=True)
                continue
            if result.returncode:
                print(result.stderr.decode(errors="replace"), file=sys.stderr)
                return result.returncode
            (source_root / name).write_bytes(result.stdout)
    source = (source_root / "input.cpp").read_text()
    gcdb = (source_root / "gamecontroller_db.cpp").read_text()
    (output / "joymap_test_seams.h").write_text(generate_seams(source))
    flags = ["-std=gnu++14", "-O1", "-g", "-funsigned-char", "-ffunction-sections", "-fdata-sections", "-I", str(root), "-I", str(root / "lib/libchdr/include"), "-I", str(output)]
    has_pad = all((source_root / name).exists() for name in PAD_SOURCES)
    if has_pad:
        flags.append("-DJOYMAP_TEST_HAS_PAD=1")
    if "void gcdb_capture_axis_baseline(" in gcdb:
        flags.append("-DJOYMAP_TEST_HAS_BASELINES=1")
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    binary = output / "joymap_v4_tests"
    wrappers = []
    for name, production_name in (("joymap_v4_tests.cpp", "input.cpp"), ("joymap_gcdb_test.cpp", "gamecontroller_db.cpp"), ("joymapping_test.cpp", "joymapping.cpp"),
                                  *((("joymap_pad_test.cpp", "joymap_pad.cpp"),) if has_pad else ())):
        wrapper = (root / "tools" / name).read_text().replace(f'#include "../{production_name}"', f'#include "{source_root / production_name}"', 1)
        target = output / name
        target.write_text(wrapper)
        wrappers.append(str(target))
    command = [args.cxx, *flags, *wrappers, "-Wl,--gc-sections", "-Wl,--wrap=ioctl", "-o", str(binary)]
    tracked_sources = {name: hashlib.sha256((source_root / name).read_bytes()).hexdigest() for name in ("input.cpp", "input.h", "gamecontroller_db.cpp", "gamecontroller_db.h", "menu.cpp", "joymapping.cpp", *(PAD_SOURCES if has_pad else ()))}
    test_sources = {name: hashlib.sha256((root / "tools" / name).read_bytes()).hexdigest() for name in ("test_joymap_v4.py", "joymap_v4_tests.cpp", "joymap_gcdb_test.cpp", "joymapping_test.cpp", "joymap_pad_test.cpp")}
    compiler = subprocess.check_output([args.cxx, "--version"], text=True, timeout=15).splitlines()[0]
    coverage = {"runtime": "Production helpers; synthetic evdev ioctl/GCDB text and in-memory config save/load",
                "source_derived_runtime": "Exact production synthetic-key transition statements with callback capture; baseline-only inline Joy-Con/OSD replay",
                "source_guards": "Both menu return call sites; post-change Joy-Con/OSD helper wiring",
                "excluded": "Physical USB hotplug, full OSD interaction, on-disk configuration persistence, FPGA core delivery"}
    (output / "source-manifest.json").write_text(json.dumps({"revision": args.revision, "sources_sha256": tracked_sources, "tests_sha256": test_sources, "compiler": compiler, "command": command, "coverage": coverage}, indent=2) + "\n")
    print("Building production-linked synthetic-evdev test harness", flush=True)
    try:
        built = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    except subprocess.TimeoutExpired as error:
        partial = error.stdout or ""
        if isinstance(partial, bytes):
            partial = partial.decode(errors="replace")
        (output / "host-build.log").write_text(partial + "\nCompilation timed out after 60 seconds.\n")
        print("FAIL: compilation timed out after 60 seconds")
        return 124
    (output / "host-build.log").write_text(built.stdout)
    if built.returncode:
        print(built.stdout)
        return built.returncode
    results = []
    results.append(subprocess.run([str(binary)], timeout=30).returncode == 0)
    menu_source = (source_root / "menu.cpp").read_text()
    for passed, label in menu_checks(menu_source) + menu_row_checks(menu_source) + hold_wiring_checks(source):
        print(f"{'PASS' if passed else 'FAIL'}: {label}", flush=True)
        results.append(passed)
    negative = subprocess.run([str(binary), "--negative-control"], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
    control_passed = negative.returncode != 0 and "FAIL: negative control" in negative.stdout
    print(f"{'PASS' if control_passed else 'FAIL'}: deliberately wrong runtime expectation rejected", flush=True)
    results.append(control_passed)
    mutant = (source_root / "menu.cpp").read_text().replace("MENU_SYSTEM_SUB_EXIT", "WRONG_RETURN_ENUM")
    rejected = not menu_checks(mutant)[0][0]
    print(f"{'PASS' if rejected else 'FAIL'}: deliberately wrong menu-return enum rejected", flush=True)
    results.append(rejected)
    uncached = not menu_row_checks(menu_source.replace("joymap_write_row(1, s);", "OsdWrite(1, s, 0, 0);", 1))[0][0]
    print(f"{'PASS' if uncached else 'FAIL'}: deliberately uncached Define buttons row rejected", flush=True)
    results.append(uncached)
    unwired = not hold_wiring_checks(re.sub(r"(if \(getchar\) return ret;\s*)osd_hold_poll\(\);", r"\1", source, count=1))[1][0]
    print(f"{'PASS' if unwired else 'FAIL'}: deliberately unwired hold poll rejected", flush=True)
    results.append(unwired)
    if not args.revision:
        current = subprocess.run([sys.executable, str(root / "tools/joymap_pads/pad2h.py"), "--check"], timeout=60).returncode == 0
        print(f"{'PASS' if current else 'FAIL'}: D5 joymap_pads.h matches tools/joymap_pads/*.bmp", flush=True)
        results.append(current)
        mutant = output / "joymap_pads_mutant.h"
        mutant.write_text((root / "joymap_pads.h").read_text().replace("0x00,", "0x01,", 1))
        rejected = subprocess.run([sys.executable, str(root / "tools/joymap_pads/pad2h.py"), "--check", str(mutant)],
                                  stderr=subprocess.DEVNULL, timeout=60).returncode != 0
        print(f"{'PASS' if rejected else 'FAIL'}: deliberately changed joymap_pads.h rejected", flush=True)
        results.append(rejected)
    print(f"Joymap v4 suite: {'PASS' if all(results) else 'FAIL'}", flush=True)
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
