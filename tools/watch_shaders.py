#!/usr/bin/env python3
import os, time, subprocess, sys, shlex

def main():
    current_file = os.path.abspath(__file__)
    current_dir = os.path.dirname(current_file)
    parent_dir = os.path.dirname(current_dir)
    shader_dir = os.path.join(parent_dir, "src", "shaders")
    build_dir = os.path.join(parent_dir, "build")

    if len(sys.argv) > 2:
        print("Using customized shader dir and build dir")
        shader_dir = os.path.abspath(sys.argv[1])
        build_dir = os.path.abspath(sys.argv[2])
    else:
        print("Using default shader dir and build dir")

    print(f"[HotReload] Watching shaders in {shader_dir}")
    print(f"[HotReload] Build dir: {build_dir}")

    timestamps = {}

    while True:
        updated = False
        for root, _, files in os.walk(shader_dir):
            for f in files:
                if f.endswith(".glsl"):
                    path = os.path.join(root, f)
                    try:
                        mtime = os.path.getmtime(path)
                    except FileNotFoundError:
                        continue
                    if path not in timestamps or timestamps[path] != mtime:
                        updated = True
                        timestamps[path] = mtime
        if updated:
            print("[HotReload] Change detected, rebuilding shaders...")
            try:
                subprocess.check_call(
                    ["cmake", "--build", build_dir, "--target", "CompileShaders"]
                )
            except subprocess.CalledProcessError as e:
                print(f"[HotReload] Shader build failed: {e}")
        time.sleep(1)

if __name__ == "__main__":
    main()
