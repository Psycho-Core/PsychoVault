#!/usr/bin/env python3
"""I build PsychoVault with the platform compiler and no browser dependencies.

My Windows build emits one PsychoVault.exe image for both the visible broker
and its hidden sandboxed renderer-child mode; portable tests exercise the same
engine, page host, and broker contracts.
"""
from __future__ import annotations
import argparse, os, pathlib, shutil, subprocess, sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
VERSION=(ROOT/"Psycho_VERSION.txt").read_text(encoding="utf-8").strip() or "0.0.0-dev"
# The running exe knows its own version so the updater can compare honestly.
VERSION_DEFINE='PSYCHO_VAULT_VERSION="%s"' % VERSION
ENGINE=["engine/Psycho_base.cpp","engine/Psycho_dom.cpp","engine/Psycho_css.cpp","engine/Psycho_js_parser.cpp",
        "engine/Psycho_js_runtime.cpp","engine/Psycho_raster.cpp","engine/Psycho_image.cpp",
        "engine/Psycho_layout.cpp","engine/Psycho_page.cpp","engine/Psycho_regex.cpp","engine/Psycho_cookies.cpp",
        "engine/Psycho_storage.cpp","engine/Psycho_jpeg.cpp","engine/Psycho_svg.cpp","engine/Psycho_canvas.cpp",
        "engine/Psycho_media.cpp","engine/Psycho_mse.cpp","engine/Psycho_page_media.cpp",
        "engine/Psycho_security.cpp"]
PLATFORM=["platform/Psycho_host.cpp","platform/Psycho_network.cpp","platform/Psycho_profile.cpp",
          "platform/Psycho_proxy.cpp","platform/Psycho_extension.cpp","platform/Psycho_download.cpp",
          "platform/Psycho_transport.cpp","platform/Psycho_tls.cpp","platform/Psycho_vault.cpp",
          "platform/Psycho_spell.cpp","platform/Psycho_sandbox.cpp"]
# P1 of my master plan: my runtime (broker + typed IPC bus + broker-side tests).
RUNTIME=["runtime/app/Psycho_instance.cpp","runtime/app/Psycho_instance_win32.cpp","runtime/ipc/Psycho_bus.cpp","runtime/broker/Psycho_broker.cpp","runtime/broker/Psycho_renderer_harness.cpp","runtime/broker/Psycho_renderer_process.cpp","runtime/broker/Psycho_renderer_farm.cpp","runtime/process/Psycho_process.cpp","runtime/process/Psycho_process_win32.cpp","runtime/process/Psycho_process_transport.cpp","runtime/renderer/Psycho_renderer_worker.cpp","runtime/renderer/Psycho_renderer_main.cpp","runtime/window/Psycho_window.cpp","runtime/window/Psycho_window_win32.cpp","runtime/surface/Psycho_surface.cpp","runtime/surface/Psycho_surface_win32.cpp","runtime/tabs/Psycho_tabs.cpp","runtime/content/Psycho_content.cpp","runtime/internal/Psycho_internal.cpp","runtime/bridge/Psycho_bridge.cpp","runtime/apphost/Psycho_apphost.cpp","runtime/services/Psycho_services.cpp","runtime/keys/Psycho_keys.cpp","runtime/ime/Psycho_ime.cpp","runtime/focus/Psycho_focus.cpp","runtime/downloads/Psycho_downloads.cpp","runtime/permissions/Psycho_permissions.cpp","runtime/private/Psycho_private.cpp","runtime/diagnostics/Psycho_diagnostics.cpp","runtime/crash/Psycho_crash.cpp","runtime/logging/Psycho_logging.cpp","runtime/commandline/Psycho_commandline.cpp"]
RUNTIME_TESTS=["runtime/tests/Psycho_runtime_tests.cpp","runtime/tests/Psycho_runtime_integration_tests.cpp","runtime/tests/Psycho_renderer_harness_tests.cpp"]
def run(cmd,env=None):
    print("+",subprocess.list2cmdline([str(x) for x in cmd]),flush=True)
    subprocess.run([str(x) for x in cmd],cwd=ROOT,env=env,check=True)
def msvc_env():
    if shutil.which("cl"):return os.environ.copy()
    candidates=[]
    pf86=os.environ.get("ProgramFiles(x86)",r"C:\Program Files (x86)")
    pf=os.environ.get("ProgramFiles",r"C:\Program Files")
    candidates.append(pathlib.Path(pf86)/"Microsoft Visual Studio/Installer/vswhere.exe")
    candidates.append(pathlib.Path(pf)/"Microsoft Visual Studio/Installer/vswhere.exe")
    vswhere=next((p for p in candidates if p.exists()),None)
    if vswhere is None:
        raise RuntimeError("vswhere not found; looked in "+", ".join(str(p) for p in candidates))
    try:
        install=subprocess.check_output([str(vswhere),"-latest","-products","*","-requires","Microsoft.VisualStudio.Component.VC.Tools.x86.x64","-property","installationPath"],text=True).strip().splitlines()[0]
    except Exception as e:
        raise RuntimeError(f"vswhere failed at {vswhere}: {e}")
    dev=pathlib.Path(install)/"Common7/Tools/VsDevCmd.bat"
    if not dev.exists():raise RuntimeError(f"VsDevCmd.bat not found at {dev}")
    try:
        out=subprocess.check_output(["cmd","/c",f'"{dev}" -arch=x64 >nul && set'],text=True)
    except Exception as e:
        raise RuntimeError(f"VsDevCmd failed at {dev}: {e}")
    env=os.environ.copy()
    for line in out.splitlines():
        if "=" in line:
            k,v=line.split("=",1);env[k]=v
    if not shutil.which("cl",path=env.get("PATH")):raise RuntimeError("cl still not found after VsDevCmd")
    return env
def main():
    ap=argparse.ArgumentParser();ap.add_argument("--compiler",choices=["auto","msvc","gcc"],default="auto");ap.add_argument("--outdir",default="build");ap.add_argument("--sanitizers",action="store_true");ap.add_argument("--target",choices=["all","engine","tests","conformance"],default="all",help="build one target, so a measurement job does not compile the shell as well");args=ap.parse_args()
    out=(ROOT/args.outdir).resolve();out.mkdir(parents=True,exist_ok=True)
    # My spelling vocabulary ships inside the exe: this header embeds the words
    # file as one raw string, so a portable build has no loose assets.
    words=(ROOT/"platform/Psycho_words.txt").read_text(encoding="utf-8")
    assert ")PVWORDS" not in words
    # MSVC refuses a single string literal past its size cap, so the words go in
    # as adjacent chunks - plain string concatenation, one token per line run.
    pieces=[words[i:i+12000] for i in range(0,len(words),12000)]
    body="\n".join('R"PVWORDS('+piece+')PVWORDS"' for piece in pieces)
    (out/"Psycho_embedded_words.hpp").write_text(
        "#pragma once\nnamespace pv {\ninline const char kPVEmbeddedWords[] =\n"
        + body + ";\n} // namespace pv\n", encoding="utf-8")
    windows=os.name=="nt";compiler=args.compiler
    if compiler=="auto":compiler="msvc" if windows else "gcc"
    env=msvc_env() if compiler=="msvc" else os.environ.copy()
    cxx="cl" if compiler=="msvc" else os.environ.get("CXX","g++")
    def target(name,sources,libs=(),subsystem=None,entry=None):
        obj=out/"obj"/name;obj.parent.mkdir(parents=True,exist_ok=True);obj.mkdir(exist_ok=True)
        if compiler=="msvc":
            cmd=[cxx,"/nologo","/std:c++20","/EHsc","/O2","/MT","/W3","/WX","/permissive-","/DUNICODE","/D_UNICODE","/D_CRT_SECURE_NO_WARNINGS","/sdl","/GS","/guard:cf","/I"+str(ROOT),"/I"+str(out),"/D"+VERSION_DEFINE,"/Fo"+str(obj)+os.sep]
            cmd+= [str(ROOT/s) for s in sources]
            cmd+=["/link","/DYNAMICBASE","/NXCOMPAT","/HIGHENTROPYVA","/INCREMENTAL:NO"]
            if subsystem:cmd+=[f"/SUBSYSTEM:"+subsystem]
            if entry:cmd+=[f"/ENTRY:"+entry]
            cmd+= [l+".lib" for l in libs]
            # A page and my interpreter need the same order of stack my Linux tests get.
            # The default 1 MiB is not enough for a recursive descent parser over real
            # script, and my runtime still stops a page well before this runs out.
            cmd+=["/STACK:8388608"]
            cmd+=[f"/OUT:"+str(out/(name+".exe"))]
        else:
            # gcc 15 reports an array-bounds error inside libstdc++'s own string move
            # when it inlines a vector of strings. It is a known false positive in the
            # header, so I keep the warning visible and stop it being fatal.
            cmd=[cxx,"-std=c++20","-O2","-g","-Wall","-Wextra","-Werror","-D"+VERSION_DEFINE,
                 "-Wno-error=array-bounds","-I"+str(ROOT),"-I"+str(out)]
            if args.sanitizers:cmd+=["-fsanitize=address,undefined","-fno-omit-frame-pointer"]
            cmd+= [str(ROOT/s) for s in sources]
            cmd+=["-pthread","-o",str(out/name)]
            cmd+= ["-l"+l for l in libs]
        run(cmd,env)
    winlibs=["kernel32","user32","gdi32","advapi32","userenv","bcrypt","winhttp","shell32","ole32","dwmapi","ws2_32","crypt32","secur32","avrt"]
    if args.target in ("all","engine"):
        target("pv-engine",ENGINE+["tools/Psycho_engine_cli.cpp"],winlibs if windows else [])
    if args.target in ("all","tests"):
        target("pv-tests",ENGINE+PLATFORM+RUNTIME+RUNTIME_TESTS+["tests/Psycho_test_main.cpp"],winlibs if windows else [])
    # The conformance runner is its own small target: my parser and interpreter plus
    # the tool that walks a Test262 tree, so a measurement never needs the shell.
    if args.target in ("all","conformance"):
        target("pv-conformance",["engine/Psycho_base.cpp","engine/Psycho_js_parser.cpp",
                                 "engine/Psycho_js_runtime.cpp","engine/Psycho_regex.cpp",
                                 "tools/Psycho_conformance.cpp"],winlibs if windows else [])
    if windows and args.target in ("all","tests"):
        # One visible executable also supplies its private renderer-child mode;
        # the restricted process receives only the inherited pipe handles.
        target("PsychoVault",ENGINE+PLATFORM+RUNTIME+["shell/Psycho_win_main.cpp"],winlibs,"WINDOWS")
    print("Built",out)
if __name__=="__main__":
    try:main()
    except subprocess.CalledProcessError as e:sys.exit(e.returncode)
    except Exception as e:print("build failed:",e,file=sys.stderr);sys.exit(1)
