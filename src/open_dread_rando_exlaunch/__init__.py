from pathlib import Path
import shutil


def include_depackager(exefs_path: Path) -> None:
    exefs_path.mkdir(parents=True, exist_ok=True)
    exlaunch_res = Path(__file__).parent.joinpath("deploy")
    shutil.copytree(exlaunch_res, exefs_path, dirs_exist_ok=True)