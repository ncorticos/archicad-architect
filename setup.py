#!/usr/bin/env python3
"""
Archicad Architect — setup for each user (macOS and Windows).

Run:  python3 setup.py            (asks for what it needs and remembers it in local.json)
      python3 setup.py --help

What it does, step by step (each step can be skipped):
  1. Gets GRAPHISOFT's add-on build tools (git submodule addon/Tools).
  2. Writes addon/RFIX/ClaudeToolsFix.grc with YOUR MDID (Developer ID + Local ID).
  3. Builds the add-on with GRAPHISOFT's BuildAddOn.py, which downloads the Archicad 30 API DevKit
     by itself (or uses yours with --devkit).
  4. Installs it into YOUR Archicad Add-Ons folder (Archicad must be closed).
  5. Shows (or writes) the MCP server entry for Claude Desktop / Claude Code.

Needs: Python 3, git, CMake, and Xcode (macOS) or Visual Studio 2022 with C++ (Windows).

Nothing personal is committed: your values live in local.json (git-ignored).
"""
import argparse, glob, json, os, platform, shutil, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
ADDON = ROOT / "addon"
LOCAL = ROOT / "local.json"
IS_MAC = platform.system() == "Darwin"
IS_WIN = platform.system() == "Windows"


def ask(cfg, key, prompt, default=None, check=None):
    val = cfg.get(key)
    while not val:
        hint = f" [{default}]" if default else ""
        val = input(f"{prompt}{hint}: ").strip().strip('"').strip("'") or (default or "")
        if check and val and not check(val):
            print("   not valid, try again"); val = ""
    cfg[key] = val
    return val


def run(cmd, **kw):
    print("  $", " ".join(str(c) for c in cmd))
    subprocess.run(cmd, check=True, **kw)


def guess_addons_dir():
    base = Path("/Applications/GRAPHISOFT") if IS_MAC else Path("C:/Program Files/GRAPHISOFT")
    for d in sorted(base.glob("Archicad 30*")) if base.exists() else []:
        for name in ("Add-Ons", "Extensões", "Erweiterungen", "Extensions", "Complementos", "Estensioni"):
            if (d / name).is_dir():
                return str(d / name)
    return None


def step_tools():
    if (ADDON / "Tools" / "CMakeCommon.cmake").exists():
        print("1. Build tools: already there."); return
    print("1. Build tools: getting GRAPHISOFT/archicad-addon-cmake-tools (git submodule)...")
    run(["git", "submodule", "update", "--init", "--depth", "1", "addon/Tools"], cwd=ROOT)


def step_mdid(cfg):
    tpl = (ADDON / "RFIX" / "ClaudeToolsFix.grc.in").read_text()
    out = tpl.replace("@DEVELOPER_ID@", cfg["developer_id"]).replace("@LOCAL_ID@", cfg["local_id"])
    (ADDON / "RFIX" / "ClaudeToolsFix.grc").write_text(out)
    print("2. MDID written to addon/RFIX/ClaudeToolsFix.grc.")


def built_product():
    pat = "ClaudeTools.bundle" if IS_MAC else "ClaudeTools.apx"
    hits = [h for h in glob.glob(str(ADDON / "Build" / "**" / pat), recursive=True) if "Package" not in h]
    return Path(sorted(hits, key=os.path.getmtime)[-1]) if hits else None


def step_build(cfg):
    cmd = [sys.executable, str(ADDON / "Tools" / "BuildAddOn.py"), "-c", str(ADDON / "config.json"), "-v", "30", "-q"]
    if cfg.get("devkit"):
        cmd += ["-d", cfg["devkit"], "-n", cfg.get("devkit_build", "3000")]
        if cfg.get("lp_converter"):
            cmd += ["-x", cfg["lp_converter"]]
    print("3. Building (the first time it downloads the API DevKit, a few hundred MB)...")
    run(cmd, cwd=ADDON)
    product = built_product()
    if not product:
        sys.exit("Build finished but the add-on was not found under addon/Build/.")
    print("   Built:", product)
    return product


def archicad_running():
    try:
        if IS_MAC:
            return subprocess.run(["pgrep", "-f", "Archicad 30.app/Contents/MacOS"], capture_output=True).returncode == 0
        out = subprocess.run(["tasklist"], capture_output=True, text=True).stdout
        return "Archicad.exe" in out
    except Exception:
        return False


def step_install(cfg, product):
    dst_dir = Path(cfg["addons_dir"])
    if archicad_running():
        print("4. Archicad is running: close it and run `python3 setup.py --only install`."); return
    dst = dst_dir / product.name
    if dst.exists():
        old = ADDON / "Build" / (product.name + ".previous")
        if old.exists():
            shutil.rmtree(old) if old.is_dir() else old.unlink()
        shutil.move(str(dst), str(old))
        print(f"   previous version kept in {old}")
    (shutil.copytree if product.is_dir() else shutil.copy2)(product, dst)
    print(f"4. Installed in {dst}. Start Archicad and check: python3 client/ct.py Ping '{{}}'")


def mcp_entry():
    uv = shutil.which("uv") or str(Path.home() / (".local/bin/uv" if not IS_WIN else ".local/bin/uv.exe"))
    return {"command": uv, "args": ["run", "--script", str(ROOT / "mcp" / "server.py")]}


def step_mcp(write):
    entry = mcp_entry()
    print("5. MCP server entry (name: arquiteto-ndc):")
    print(json.dumps({"arquiteto-ndc": entry}, indent=2))
    print(f"   Claude Code CLI:  claude mcp add arquiteto-ndc -- {entry['command']} run --script \"{entry['args'][2]}\"")
    if not shutil.which("uv") and not Path(entry["command"]).exists():
        print("   (uv is not installed: https://docs.astral.sh/uv/getting-started/installation/)")
    if not write:
        print("   Add it to claude_desktop_config.json, or run again with --write-mcp-config."); return
    cfgp = (Path.home() / "Library/Application Support/Claude/claude_desktop_config.json" if IS_MAC
            else Path(os.environ.get("APPDATA", "")) / "Claude/claude_desktop_config.json")
    data = json.loads(cfgp.read_text()) if cfgp.exists() else {}
    shutil.copy2(cfgp, str(cfgp) + ".bak") if cfgp.exists() else None
    data.setdefault("mcpServers", {})["arquiteto-ndc"] = entry
    cfgp.parent.mkdir(parents=True, exist_ok=True)
    cfgp.write_text(json.dumps(data, indent=2, ensure_ascii=False))
    print(f"   Written to {cfgp} (backup .bak). Restart the Claude app.")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--devkit", help="optional: your own API DevKit 30 folder (the one that contains Support); default downloads it")
    ap.add_argument("--devkit-build", help="build number of your DevKit (default 3000)")
    ap.add_argument("--lp-converter", help="optional: LP_XMLConverter folder for your own DevKit")
    ap.add_argument("--developer-id"); ap.add_argument("--local-id")
    ap.add_argument("--addons-dir", help="Archicad 30 Add-Ons folder (Extensões in the Portuguese version)")
    ap.add_argument("--only", choices=["tools", "mdid", "build", "install", "mcp"], help="run one step")
    ap.add_argument("--no-install", action="store_true")
    ap.add_argument("--write-mcp-config", action="store_true", help="add the server to Claude Desktop's config")
    a = ap.parse_args()

    cfg = json.loads(LOCAL.read_text()) if LOCAL.exists() else {}
    for k, v in (("devkit", a.devkit), ("devkit_build", a.devkit_build), ("lp_converter", a.lp_converter),
                 ("developer_id", a.developer_id), ("local_id", a.local_id), ("addons_dir", a.addons_dir)):
        if v: cfg[k] = v
    steps = [a.only] if a.only else ["tools", "mdid", "build", "install", "mcp"]
    if a.no_install and "install" in steps: steps.remove("install")

    if "mdid" in steps:
        print("MDID: register (free) at https://archicadapi.graphisoft.com -> Profile -> 'Add-On ID'.")
        ask(cfg, "developer_id", "Your Developer ID (number)", check=str.isdigit)
        ask(cfg, "local_id", "Local ID for this add-on (number)", check=str.isdigit)
    if "install" in steps:
        ask(cfg, "addons_dir", "Archicad 30 Add-Ons folder", guess_addons_dir(), lambda p: Path(p).is_dir())
    LOCAL.write_text(json.dumps(cfg, indent=2, ensure_ascii=False))

    product = None
    for s in steps:
        if s == "tools": step_tools()
        elif s == "mdid": step_mdid(cfg)
        elif s == "build": product = step_build(cfg)
        elif s == "install":
            product = product or built_product()
            if product is None:
                sys.exit("Build first: python3 setup.py --only build")
            step_install(cfg, product)
        elif s == "mcp": step_mcp(a.write_mcp_config)
    print("Done. Your settings are in local.json (not shared).")


if __name__ == "__main__":
    main()
