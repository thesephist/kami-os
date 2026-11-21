"""Run regressions for the changes in this fork; SDK checks require a build first."""

from pathlib import Path
import argparse
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument(
    "--sdk", action="store_true", help="also test the pinned JPEGDEC library in .pio"
)
args = parser.parse_args()
checks = [
    "check_architecture_dependencies",
    "test_architecture_dependencies",
    "check_tracked_secrets",
    "test_cine_isp_config",
    "test_cine_service",
    "test_cine_exif",
    "test_cine_video_start",
    "test_camera_settings",
    "test_cine_isp_transaction",
    "test_cine_decoder_ownership",
    "test_cine_jpeg_preview",
    "test_cine_subtle_overlay_view",
    "test_cine_interactions",
    "test_diagnostic_capabilities",
    "test_cine_flow",
    "test_navigation_presentation",
    "test_camera_dma_compat",
    "test_camera_session",
    "test_camera_still",
    "test_photos_navigation",
    "test_photo_library",
    "test_media_gallery",
    "test_photo_storage",
    "test_photo_upload_timestamp",
    "test_photos_sync_policy",
    "test_photos_video_upload",
    "test_video_container",
    "test_video_recording",
    "test_video_frame_audit",
    "test_video_enqueue",
    "test_video_playback_async",
]
if args.sdk:
    checks.append("test_photo_preview")
for check in checks:
    print(f"Running {check}", flush=True)
    subprocess.run(
        [sys.executable, str(ROOT / "tools" / (check + ".py"))], cwd=ROOT, check=True
    )
subprocess.run(["git", "diff", "--check"], cwd=ROOT, check=True)
print(f"PASS {len(checks)} fork checks", flush=True)
