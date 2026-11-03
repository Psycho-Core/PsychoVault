#!/usr/bin/env python3
"""I build PsychoVault with the platform compiler and no browser dependencies."""
from __future__ import annotations
import argparse, os, pathlib, shutil, subprocess, sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
ENGINE=["engine/Psycho_base.cpp","engine/Psycho_dom.cpp","engine/Psycho_css.cpp","engine/Psycho_js_parser.cpp","engine/Psycho_js_runtime.cpp","engine/Psycho_raster.cpp","engine/Psycho_image.cpp","engine/Psycho_layout.cpp","engine/Psycho_page.cpp"]
PLATFORM=["platform/Psycho_process.cpp","platform/Psycho_network.cpp","platform/Psycho_profile.cpp"]
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
    ap=argparse.ArgumentParser();ap.add_argument("--compiler",choices=["auto","msvc","gcc"],default="auto");ap.add_argument("--outdir",default="build");ap.add_argument("--sanitizers",action="store_true");args=ap.parse_args()
    out=(ROOT/args.outdir).resolve();out.mkdir(parents=True,exist_ok=True)
    windows=os.name=="nt";compiler=args.compiler
    if compiler=="auto":compiler="msvc" if windows else "gcc"
    env=msvc_env() if compiler=="msvc" else os.environ.copy()
    cxx="cl" if compiler=="msvc" else os.environ.get("CXX","g++")
    def target(name,sources,libs=(),subsystem=None,entry=None):
        obj=out/"obj"/name;obj.parent.mkdir(parents=True,exist_ok=True);obj.mkdir(exist_ok=True)
        if compiler=="msvc":
            cmd=[cxx,"/nologo","/std:c++20","/EHsc","/O2","/MT","/W3","/WX","/permissive-","/DUNICODE","/D_UNICODE","/D_CRT_SECURE_NO_WARNINGS","/sdl","/GS","/guard:cf","/I"+str(ROOT),"/Fo"+str(obj)+os.sep]
            cmd+= [str(ROOT/s) for s in sources]
            cmd+=["/link","/DYNAMICBASE","/NXCOMPAT","/HIGHENTROPYVA","/INCREMENTAL:NO"]
            if subsystem:cmd+=["/SUBSYSTEM:"+subsystem]
            if entry:cmd+=["/ENTRY:"+entry]
            cmd+= [l+".lib" for l in libs]
            cmd+=["/OUT:"+str(out/(name+".exe"))]
        else:
            cmd=[cxx,"-std=c++20","-O2","-g","-Wall","-Wextra","-Werror","-I"+str(ROOT)]
            if args.sanitizers:cmd+=["-fsanitize=address,undefined","-fno-omit-frame-pointer"]
            cmd+= [str(ROOT/s) for s in sources]
            cmd+=["-pthread","-o",str(out/name)]
            cmd+= ["-l"+l for l in libs]
        run(cmd,env)
    winlibs=["kernel32","user32","gdi32","advapi32","userenv","bcrypt","winhttp","shell32","ole32","dwmapi"]
    target("pv-engine",ENGINE+["tools/Psycho_engine_cli.cpp"],winlibs if windows else [])
    target("pv-tests",ENGINE+PLATFORM+["tests/Psycho_test_main.cpp"],winlibs if windows else [])
    target("pv-renderer",ENGINE+["platform/Psycho_process.cpp","tools/Psycho_renderer_main.cpp"],winlibs + ["ws2_32"] if windows else [],"WINDOWS" if windows else None,"mainCRTStartup" if windows else None)
    if windows:
        target("PsychoVault",ENGINE+PLATFORM+["shell/Psycho_win_main.cpp"],winlibs,"WINDOWS")
        target("pv-sandbox-test",ENGINE+["platform/Psycho_process.cpp","tools/Psycho_sandbox_test.cpp"],winlibs)
        target("pv-sandbox-minimal",["tools/Psycho_sandbox_minimal.cpp"],["kernel32","advapi32","ws2_32"],"WINDOWS","mainCRTStartup")
    print("Built",out)
if __name__=="__main__":
    try:main()
    except subprocess.CalledProcessError as e:sys.exit(e.returncode)
    except Exception as e:print("build failed:",e,file=sys.stderr);sys.exit(1)
