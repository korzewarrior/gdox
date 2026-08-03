"""Architecture-audit orchestration."""

from __future__ import annotations

from collections.abc import Callable
from pathlib import Path

from .checks_android import check_android
from .checks_build import check_build
from .checks_layers import check_layers
from .checks_media import check_media
from .checks_nbd import check_nbd
from .repository import Repository

ArchitectureCheck = Callable[[Repository], list[str]]
CHECKS: tuple[ArchitectureCheck, ...] = (
    check_layers,
    check_media,
    check_nbd,
    check_build,
    check_android,
)


def audit_repository(root: Path) -> list[str]:
    repository = Repository(root)
    failures: list[str] = []
    for check in CHECKS:
        failures.extend(check(repository))
    return failures
