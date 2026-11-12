"""Compile the pinned JPEGDEC translation unit for speed without editing its cache."""

from hashlib import sha256
from pathlib import Path

PINNED = {
    "JPEGDEC.cpp": "cce4a7cd6fe8fe26825276f9c65ed9c96bf1fe31a15c79e0169ee2385543d210",
    "jpeg.inl": "34c6eb16b56b9ee86108b8ca22ac42bbdf0c31bc3bb4ceb61f844f9ee344e2a9",
}


def speed_source(source):
    for name, expected in PINNED.items():
        if sha256((source.parent / name).read_bytes()).hexdigest() != expected:
            raise RuntimeError(
                "JPEG preview optimization requires the reviewed JPEGDEC revision"
            )
    return b'#pragma GCC optimize ("O3")\n' + source.read_bytes()


def install(env):
    def replace(build_env, node):
        source = Path(node.srcnode().get_abspath())
        data = speed_source(source)
        target = Path(build_env.subst("$BUILD_DIR")) / "jpegdec-speed" / "JPEGDEC.cpp"
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or target.read_bytes() != data:
            target.write_bytes(data)
        print("JPEGDEC: pinned source compiled with O3; other firmware retains Os")
        return build_env.File(str(target))

    env.AddBuildMiddleware(replace, "*/JPEGDEC/src/JPEGDEC.cpp")


if "Import" in globals():
    Import("env")
    install(env)
