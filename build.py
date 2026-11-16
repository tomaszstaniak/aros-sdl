#!/usr/bin/env python3
"""Build the AROS SDL kit for one target.

    python3 build.py <target> [--only sdl3|sdl3_image ...]

Targets: aros-x86_64-abiv1-pc, aros-aarch64-abiv1-raspi, aros-x86_64-abiv11-pc.
Compilers and SDKs come from local.json (see local.example.json); environment
variables AROS_CC, AROS_CXX, AROS_AR and AROS_SDK override them.

SDL3 is built as contrib builds its static library (SDL3-aros-staticlib): the
SDL release archive, contrib's AROS diff, then our patches/sdl3/series, and the
file list of contrib's SDL3/main/mmakefile.src. SDL3_image is built with its
own CMake build, configured as contrib's SDL3/SDL3_image/mmakefile.src does.

The kit appears in build/<target>/kit: include/, lib/ (libSDL3_static.a,
libSDL3_image.a, libiconv.a), lib/cmake/ for find_package(), licences and BUILD.json.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
MANIFEST = json.loads((ROOT / "upstreams.json").read_text())
CONTRIB = ROOT / "upstream/contrib"
DISTFILES = ROOT / "distfiles"
TARGETS = ["aros-x86_64-abiv1-pc", "aros-aarch64-abiv1-raspi", "aros-x86_64-abiv11-pc"]
# Contrib stamps a build date into SDL (ADATE). A fixed one keeps builds
# reproducible: the date of the pinned contrib commit.
ADATE = "15.11.2026"

# Code-generation flags the AROS build passes for each CPU (contrib builds
# with the target's configured TARGET_CFLAGS).
ARCH_CFLAGS = {
    "aros-x86_64-abiv1-pc": ["-m64", "-mcmodel=large", "-mno-red-zone", "-mno-ms-bitfields"],
    "aros-x86_64-abiv11-pc": ["-m64", "-mcmodel=large", "-mno-red-zone", "-mno-ms-bitfields"],
    "aros-aarch64-abiv1-raspi": [],
}
COMMON_CFLAGS = ["-O2", "-fno-asynchronous-unwind-tables", "-fno-omit-frame-pointer"]


def log(msg):
    print(msg, flush=True)


def run(cmd, cwd=None, logfile=None, env=None):
    cmd = [str(c) for c in cmd]
    out = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True)
    if logfile:
        with open(logfile, "a") as f:
            f.write("$ " + " ".join(cmd) + "\n" + out.stdout + out.stderr)
    if out.returncode:
        sys.stderr.write(out.stdout[-3000:] + out.stderr[-3000:])
        raise SystemExit(f"failed ({out.returncode}): {' '.join(cmd)[:200]}")
    return out.stdout


def sha256(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def toolchain(target):
    local = ROOT / "local.json"
    cfg = json.loads(local.read_text()).get(target, {}) if local.exists() else {}
    t = {k: os.environ.get("AROS_" + k.upper(), cfg.get(k, "")) for k in ("cc", "cxx", "ar", "sdk")}
    for k, v in t.items():
        if not v or not Path(v).exists():
            raise SystemExit(f"{target}: '{k}' not set or missing ({v!r}); see local.example.json")
    t["cflags"] = ["--sysroot=" + t["sdk"]] + ARCH_CFLAGS[target] + COMMON_CFLAGS
    return t


def check_contrib():
    pin = MANIFEST["repositories"]["contrib"]["commit"]
    if not (CONTRIB / ".git").exists():
        raise SystemExit("upstream/contrib missing; see docs/patching.md")
    head = run(["git", "-C", CONTRIB, "rev-parse", "HEAD"]).strip()
    if head != pin:
        raise SystemExit(f"upstream/contrib is at {head}, not the pin {pin}")
    if run(["git", "-C", CONTRIB, "status", "--porcelain"]).strip():
        raise SystemExit("upstream/contrib has local changes")
    return pin


def distfile(key):
    d = MANIFEST["distfiles"][key]
    p = DISTFILES / d["file"]
    if not p.exists():
        DISTFILES.mkdir(exist_ok=True)
        run(["curl", "-sSLf", "-o", p, d["url"]])
    if sha256(p) != d["sha256"]:
        raise SystemExit(f"{p}: SHA-256 differs from upstreams.json")
    return p


def series(repo):
    s = ROOT / "patches" / repo / "series"
    if not s.exists():
        raise SystemExit(f"missing {s} (an empty file is a valid series)")
    names = [l.split("#")[0].strip() for l in s.read_text().splitlines()]
    return [ROOT / "patches" / repo / n for n in names if n]


def unpack(archive, dest, patches):
    top = dest / archive.name[: -len(".tar.gz")]
    if top.exists():
        shutil.rmtree(top)
    with tarfile.open(archive) as t:
        t.extractall(dest, filter="data")
    for p in patches:
        run(["patch", "-p1", "-s", "-N", "-i", p], cwd=top)
    return top


def mm_files(text, var):
    """Every file a mmakefile variable lists, over its := and += assignments."""
    names = []
    for m in re.finditer(rf"^{var}\s*(?::=|\+=)\s*\\\n((?:.*\\\n)*.*\n)", text, re.M):
        names += re.findall(r"\$\(ARCHSRCDIR\)/(\S+)", m.group(1))
    if not names:
        raise SystemExit(f"mmakefile: no {var} list")
    return names


# SDKs without oop/ headers (see patch 0007)
NO_OOP_HEADERS = {"aros-x86_64-abiv11-pc"}
HOSTS = {"aros-x86_64-abiv1-pc": "x86_64-aros", "aros-x86_64-abiv11-pc": "x86_64-aros",
         "aros-aarch64-abiv1-raspi": "aarch64-aros"}


def build_libiconv(target, t, work, kit, logfile):
    """GNU libiconv as contrib builds it (development/libs/libiconv), static only.
    SDL3's AROS configuration uses it (SDL_USE_LIBICONV)."""
    src = unpack(distfile("libiconv"), work,
                 [CONTRIB / "development/libs/libiconv/libiconv-1.19-aros.diff"] + series("libiconv"))
    bld = work / "libiconv-build"
    shutil.rmtree(bld, ignore_errors=True)
    bld.mkdir()
    # Without the target ranlib, libtool runs the host one, which empties the archive.
    env = dict(os.environ, CC=t["cc"], AR=t["ar"], RANLIB=t["ar"][:-2] + "ranlib",
               CFLAGS=" ".join(t["cflags"] + ["-std=gnu11", f"-ffile-prefix-map={work}=/kit"]))
    run([src / "configure", "--host=" + HOSTS[target], "--disable-shared", "--enable-static",
         "--disable-nls", "--disable-rpath", "--prefix=" + str(kit)], cwd=bld, logfile=logfile, env=env)
    run(["make", "-j8", "lib/localcharset.h"], cwd=bld, logfile=logfile, env=env)
    run(["make", "-C", "lib", "-j8"], cwd=bld, logfile=logfile, env=env)
    (kit / "include").mkdir(parents=True, exist_ok=True)
    shutil.copy(bld / "lib/.libs/libiconv.a", kit / "lib/libiconv.a")
    shutil.copy(bld / "include/iconv.h", kit / "include/iconv.h")
    shutil.copy(src / "COPYING.LIB", kit / "licenses/libiconv-COPYING.LIB")
    lib = kit / "lib/libiconv.a"
    if "libiconv_open" not in run([t["ar"][:-2] + "nm", lib]):
        raise SystemExit(f"{lib}: no libiconv_open symbol")
    return {"library_sha256": sha256(lib)}


def build_sdl3(target, t, work, kit, logfile):
    src = unpack(distfile("sdl3"), work, [CONTRIB / "SDL3/main/SDL3-3.4.12-aros.diff"] + series("sdl3"))
    mk = (CONTRIB / "SDL3/main/mmakefile.src").read_text(errors="replace")
    files = mm_files(mk, "FILES")
    obj = work / "sdl3-obj"
    obj.mkdir(exist_ok=True)
    flags = t["cflags"] + [
        "-std=gnu99", "-Wno-stringop-truncation", "-DSDL3_AROS_STATIC", f'-DADATE="{ADATE}"',
        f"-ffile-prefix-map={work}=/kit",
        f"-I{src}/include", f"-I{src}/include/build_config", f"-I{src}", f"-I{src}/src",
        f"-I{CONTRIB}/SDL3/main", f"-I{kit}/include",
    ]
    sources = [CONTRIB / "SDL3/main/SDL3_static.c"] + [src / (f + ".c") for f in files]
    if target in NO_OOP_HEADERS:
        # patch 0007: dummy joystick driver where the SDK lacks oop/ headers
        flags.append("-DSDL_AROS_NO_OOP_HEADERS")
        sources.append(src / "src/joystick/dummy/SDL_sysjoystick.c")
    objs = []
    for s in sources:
        name = (str(s.relative_to(src)) if s.is_relative_to(src) else s.name)
        o = obj / (name.replace("/", "_").replace("._", "")[:-2] + ".o")
        run([t["cc"]] + flags + ["-c", s, "-o", o], logfile=logfile)
        objs.append(o)
    lib = kit / "lib/libSDL3_static.a"
    lib.unlink(missing_ok=True)
    run([t["ar"], "rcsD", lib] + objs, logfile=logfile)
    shutil.copytree(src / "include/SDL3", kit / "include/SDL3", dirs_exist_ok=True)
    # find_package(SDL3) for consumers: contrib's shim, pointed at this kit.
    cm = kit / "lib/cmake/SDL3"
    cm.mkdir(parents=True, exist_ok=True)
    shim = (CONTRIB / "SDL3/SDL3_image/cmake/SDL3Config.cmake").read_text()
    head = (
        "# Paths of this kit, relative to this file.\n"
        'get_filename_component(_aros_sdl_kit "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)\n'
        'if(NOT DEFINED SDL3_INCLUDE_DIR)\n  set(SDL3_INCLUDE_DIR "${_aros_sdl_kit}/include")\nendif()\n'
        'if(NOT DEFINED SDL3_LIBRARY)\n  set(SDL3_LIBRARY "${_aros_sdl_kit}/lib/libSDL3_static.a")\nendif()\n\n'
    )
    shim = shim.replace('set(SDL3_VERSION "3.4.10")', 'set(SDL3_VERSION "3.4.12")')
    tail = (
        "\n# Static SDL3 needs the kit's libiconv; some projects link ${SDL3_LIBRARIES}.\n"
        'set_property(TARGET SDL3::SDL3-static APPEND PROPERTY INTERFACE_LINK_LIBRARIES\n'
        '             "${_aros_sdl_kit}/lib/libiconv.a")\n'
        "set(SDL3_LIBRARIES SDL3::SDL3)\n"
    )
    # Where the SDK reaches gl.library through link stubs (ABIv11), static SDL3
    # needs libGL; on mainline the OpenGL calls go through the library base.
    nm = run([t["ar"][:-2] + "nm", "-u", lib])
    if "glAMakeCurrent" in nm:
        tail += ('set_property(TARGET SDL3::SDL3-static APPEND PROPERTY INTERFACE_LINK_LIBRARIES GL)\n')
    (cm / "SDL3Config.cmake").write_text(head + shim + tail)
    ver = (CONTRIB / "SDL3/SDL3_image/cmake/SDL3ConfigVersion.cmake").read_text()
    (cm / "SDL3ConfigVersion.cmake").write_text(ver.replace('"3.4.10"', '"3.4.12"'))
    shutil.copy(src / "LICENSE.txt", kit / "licenses/SDL3-LICENSE.txt")
    return {"objects": len(objs), "library_sha256": sha256(lib)}


def build_sdl3_image(target, t, work, kit, logfile):
    src = unpack(distfile("sdl3_image"), work, series("sdl3_image"))
    bld = work / "sdl3_image-build"
    if bld.exists():
        shutil.rmtree(bld)
    opts = [
        "-DBUILD_SHARED_LIBS=OFF", "-DSDLIMAGE_VENDORED=OFF", "-DSDLIMAGE_DEPS_SHARED=OFF",
        "-DSDLIMAGE_SAMPLES=OFF", "-DSDLIMAGE_TESTS=OFF", "-DSDLIMAGE_INSTALL=ON",
        "-DSDLIMAGE_STRICT=OFF", "-DSDLIMAGE_AVIF=OFF", "-DSDLIMAGE_JXL=OFF",
        "-DSDLIMAGE_TIF=OFF", "-DSDLIMAGE_WEBP=OFF",
        # CMake treats AROS as able to share libraries and SDL3_image then builds
        # PIC code; the AROS loader has no GOT, so such objects do not load.
        "-DCMAKE_POSITION_INDEPENDENT_CODE=OFF",
        f"-DSDL3_DIR={kit}/lib/cmake/SDL3",
    ]
    cflags = " ".join(t["cflags"][1:] + [f"-ffile-prefix-map={work}=/kit"])
    run(["cmake", "-S", src, "-B", bld, "-G", "Unix Makefiles",
         f"-DCMAKE_TOOLCHAIN_FILE={ROOT}/cmake/aros.cmake", "-DCMAKE_BUILD_TYPE=Release",
         f"-DAROS_CC={t['cc']}", f"-DAROS_AR={t['ar']}", f"-DAROS_SDK={t['sdk']}",
         f"-DAROS_CFLAGS={cflags}", f"-DCMAKE_INSTALL_PREFIX={kit}"] + opts, logfile=logfile)
    run(["cmake", "--build", bld, "-j8"], logfile=logfile)
    run(["cmake", "--install", bld], logfile=logfile)
    cache = (bld / "CMakeCache.txt").read_text()
    formats = sorted(set(re.findall(r"^SDLIMAGE_(\w+):BOOL=ON", cache, re.M)))
    shutil.copy(src / "LICENSE.txt", kit / "licenses/SDL3_image-LICENSE.txt")
    lib = kit / "lib/libSDL3_image.a"
    # Bundled codecs (qoi.h, tiny_jpeg, the XMP helper) export plain names
    # such as qoi_encode; a shared SDL3_image hides them, a static one clashes
    # with programs that carry their own copy. Keep only the IMG_ API global.
    nm = run([t["ar"][:-2] + "nm", "-g", "--defined-only", lib])
    hide = sorted({l.split()[2] for l in nm.splitlines()
                   if len(l.split()) == 3 and not l.split()[2].startswith("IMG_")})
    if hide:
        run([t["ar"][:-2] + "objcopy"] + [f"--localize-symbol={n}" for n in hide] + [lib])
    # The installed static target names only SDL3::Headers: link SDL3 after
    # SDL3_image, or its calls into SDL (SDL_crc32, ...) stay unresolved.
    tgt = kit / "lib/cmake/SDL3_image/SDL3_image-static-targets.cmake"
    txt = tgt.read_text()
    if "SDL3::SDL3-static" not in txt:
        txt = txt.replace('INTERFACE_LINK_LIBRARIES "SDL3::Headers;',
                          'INTERFACE_LINK_LIBRARIES "SDL3::Headers;SDL3::SDL3-static;')
        tgt.write_text(txt)
    return {"library_sha256": sha256(lib), "cmake_options_on": formats, "localized_symbols": hide}


def check_no_got(t, lib):
    """AROS executables cannot use a GOT: refuse PIC objects in a kit library."""
    rel = run([t["ar"][:-2] + "readelf", "-r", "-W", lib])
    n = len(re.findall(r"R_X86_64_(?:GOT\w*|PLTOFF64)|R_AARCH64_\w*GOT\w*", rel))
    if n:
        raise SystemExit(f"{lib}: {n} GOT/PLT relocations (PIC code), AROS cannot load these")


PARTS = {"libiconv": build_libiconv, "sdl3": build_sdl3, "sdl3_image": build_sdl3_image}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("target", choices=TARGETS)
    ap.add_argument("--only", action="append", choices=list(PARTS))
    a = ap.parse_args()
    t = toolchain(a.target)
    pin = check_contrib()
    out = ROOT / "build" / a.target
    work, kit = out / "work", out / "kit"
    if not a.only:
        shutil.rmtree(out, ignore_errors=True)
    work.mkdir(parents=True, exist_ok=True)
    (kit / "lib").mkdir(parents=True, exist_ok=True)
    (kit / "licenses").mkdir(parents=True, exist_ok=True)
    logfile = out / "build.log"
    info_path = kit / "BUILD.json"
    info = json.loads(info_path.read_text()) if info_path.exists() else {}
    info.update({"target": a.target, "contrib": pin,
                 "compiler": run([t["cc"], "--version"]).splitlines()[0],
                 "cflags": t["cflags"][1:]})
    info.setdefault("parts", {})
    for name in a.only or list(PARTS):
        log(f"{a.target}: {name}")
        d = MANIFEST["distfiles"][name]
        part = PARTS[name](a.target, t, work, kit, logfile)
        for lib in (kit / "lib").glob("*.a"):
            check_no_got(t, lib)
        info["parts"][name] = {"source": d["file"], "sha256": d["sha256"],
                               "patches": [p.name for p in series(name)],
                               **part}
    info_path.write_text(json.dumps(info, indent=2) + "\n")
    log(f"kit: {kit}")


if __name__ == "__main__":
    main()
